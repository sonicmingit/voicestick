"""VoiceStick Watch BLE transport. Recognition and key injection stay in the desktop client."""
import argparse
import asyncio
import ctypes
import json
import os
import struct
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

SERVICE = "2bd37c10-cd1e-4b13-9b14-e18a349c5970"
PIPE = "2bd37c11-cd1e-4b13-9b14-e18a349c5970"
ROOT = Path(os.environ["LOCALAPPDATA"]) / "VoiceStickS3"
STEPS = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
ADJUST = [-1,-1,-1,-1,2,4,6,8]


def pcm(packet):
    value, count = struct.unpack_from("<hH", packet)
    if not 1 <= count <= 2048 or len(packet) != 4 + count // 2:
        raise ValueError("invalid audio block")
    output = bytearray(struct.pack("<h", value))
    index = 32
    for i in range(count - 1):
        code = packet[4 + i // 2] >> (4 * (i % 2)) & 15
        step = STEPS[index]
        diff = (step >> 3) + ((step if code & 4 else 0) + (step >> 1 if code & 2 else 0) + (step >> 2 if code & 1 else 0))
        value = max(-32768, min(32767, value + (-diff if code & 8 else diff)))
        index = max(0, min(88, index + ADJUST[code & 7]))
        output.extend(struct.pack("<h", value))
    return output


class Radio:
    def __init__(self, client, token, nonce):
        self.client = client
        self.cipher = AESGCM(bytes.fromhex(token))
        self.nonce = nonce
        if len(nonce) != 8:
            raise ValueError("invalid BLE challenge")
        self.seq = self.received = 0
        self.lock = asyncio.Lock()
        self.parts = bytearray()
        self.messages = asyncio.Queue(256)
        self.failed = asyncio.Event()

    def notify(self, _, part):
        try:
            if part[0] & 1:
                self.parts.clear()
            self.parts.extend(part[1:])
            if len(self.parts) > 65536:
                raise ValueError("overflow")
            if part[0] & 2:
                self.received += 1
                iv = self.nonce + struct.pack(">I", self.received | 0x80000000)
                self.messages.put_nowait(self.cipher.decrypt(iv, bytes(self.parts), None))
        except Exception:
            self.failed.set()

    async def send(self, plain):
        async with self.lock:
            self.seq += 1
            iv = self.nonce + struct.pack(">I", self.seq)
            data = self.cipher.encrypt(iv, plain, None)
            size = max(19, min(510, self.client.mtu_size - 4))
            for i in range(0, len(data), size):
                chunk = data[i:i+size]
                flag = int(i == 0) | (2 if i + len(chunk) == len(data) else 0)
                await self.client.write_gatt_char(PIPE, bytes([flag])+chunk, response=True)

    async def reply(self, channel, state):
        await self.send(bytes([ord("J"),channel]) + json.dumps(state, ensure_ascii=False,separators=(",",":")).encode())


async def forward(radio, channel, token, commands):
    writer = None
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1",38466)
        writer.write(token.encode())
        expected = asyncio.Queue(256)

        async def upload():
            while True:
                cmd, payload = await asyncio.wait_for(commands.get(),5)
                if cmd == b"A":
                    payload = pcm(payload)
                writer.write(cmd + struct.pack(">H",len(payload)) + payload)
                await writer.drain()
                expected.put_nowait(cmd)
                if cmd not in (b"S",b"A"):
                    return

        async def download():
            last = ""
            while True:
                cmd = await asyncio.wait_for(expected.get(),5)
                line = await asyncio.wait_for(reader.readline(),30)
                if not line:
                    raise IOError("电脑接收器已断开")
                value = json.loads(line)
                text = value.get("text","")
                if cmd == b"E":
                    value["bleFinal"] = True
                if cmd != b"A" or value.get("phase")=="error" or (text and text!=last):
                    await radio.reply(channel,value)
                last = text
                if value.get("phase")=="error" or cmd not in (b"S",b"A"):
                    return

        sending = asyncio.create_task(upload())
        receiving = asyncio.create_task(download())
        try:
            done,_ = await asyncio.wait([sending,receiving],return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
            if not receiving.done():
                await receiving
        finally:
            sending.cancel()
            receiving.cancel()
            await asyncio.gather(sending,receiving,return_exceptions=True)
    except Exception as exc:
        await radio.reply(channel,{"phase":"error","lastError":str(exc) or "BLE 会话超时"})
    finally:
        if writer:
            writer.close()


async def connected(client, token, disconnected):
    radio = Radio(client,token,await client.read_gatt_char(PIPE))
    await client.start_notify(PIPE,radio.notify)
    await radio.send(b"H\0")
    channels = {}
    last_notice = 0.0
    try:
        while not disconnected.is_set() and not radio.failed.is_set():
            try:
                msg = await asyncio.wait_for(radio.messages.get(),1)
            except asyncio.TimeoutError:
                continue
            op, channel, payload = msg[:1], msg[1], msg[2:]
            if time.monotonic()-last_notice>2:
                (ROOT/"watch-ble-seen").touch()
                last_notice=time.monotonic()
            entry = channels.get(channel)
            if op in (b"S",b"P",b"K",b"R",b"U",b"V"):
                if entry and not entry[1].done():
                    continue
                queue = asyncio.Queue(128)
                entry = queue,asyncio.create_task(forward(radio,channel,token,queue))
                channels[channel] = entry
            if entry and not entry[1].done():
                entry[0].put_nowait((op,payload))
            for key,(_,task) in list(channels.items()):
                if task.done():
                    task.result()
                    del channels[key]
    finally:
        for _, task in channels.values():
            task.cancel()
        await asyncio.gather(*(t for _,t in channels.values()),return_exceptions=True)


async def run():
    while True:
        try:
            pair = json.loads((ROOT / "watch-pair.json").read_text(encoding="utf8"))
            device = await BleakScanner.find_device_by_filter(
                lambda _, adv: SERVICE in adv.service_uuids, timeout=6)
            if device:
                disconnected = asyncio.Event()
                async with BleakClient(device,disconnected_callback=lambda _:disconnected.set()) as client:
                    await connected(client,pair["token"],disconnected)
        except (OSError,RuntimeError,ValueError,KeyError,asyncio.TimeoutError):
            pass
        except Exception as exc:
            # WinRT reports transient radio failures as BleakError.
            from bleak.exc import BleakError
            if not isinstance(exc,BleakError):
                raise
        await asyncio.sleep(1)


async def main(parent):
    task = asyncio.create_task(run())
    try:
        if parent:
            kernel=ctypes.windll.kernel32
            kernel.OpenProcess.restype=ctypes.c_void_p
            handles=[kernel.OpenProcess(0x100000,False,pid) for pid in {parent,os.getppid()}]
            try:
                if not all(handles):
                    return
                while not task.done() and all(kernel.WaitForSingleObject(ctypes.c_void_p(h),0)==258 for h in handles):
                    await asyncio.sleep(1)
            finally:
                for handle in handles:
                    if handle:
                        kernel.CloseHandle(ctypes.c_void_p(handle))
        if not parent or task.done():
            await task
    finally:
        task.cancel()
        await asyncio.gather(task,return_exceptions=True)


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--parent",type=int)
    asyncio.run(main(parser.parse_args().parent))

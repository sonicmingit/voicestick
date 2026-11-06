package cn.sonic.voicestick.watch;

import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.os.ParcelUuid;
import android.util.Log;
import java.io.*;
import java.nio.ByteBuffer;
import java.security.SecureRandom;
import java.util.*;
import java.util.concurrent.*;
import javax.crypto.Cipher;
import javax.crypto.spec.*;

final class WatchBle extends BluetoothGattServerCallback implements AutoCloseable {
    static final UUID SERVICE=UUID.fromString("2bd37c10-cd1e-4b13-9b14-e18a349c5970");
    static final UUID PIPE=UUID.fromString("2bd37c11-cd1e-4b13-9b14-e18a349c5970");
    static final UUID CCC=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    static volatile WatchBle instance;
    final byte[] key;
    final BluetoothGattServer server;
    final BluetoothLeAdvertiser advertiser;
    final BluetoothGattCharacteristic pipe;
    final byte[] nonce=new byte[8];
    final SecureRandom random=new SecureRandom();
    final ByteArrayOutputStream inbound=new ByteArrayOutputStream();
    final ArrayBlockingQueue<Integer> acknowledgments=new ArrayBlockingQueue<>(8);
    final Object txLock=new Object();
    final Map<Integer,Channel> channels=new HashMap<>();
    volatile String state="BLE 准备中";
    private BluetoothDevice peer;
    private int mtu=23,tx,rx,session;
    private boolean authenticated,subscribed,closed;
    private final AdvertiseCallback advertising=new AdvertiseCallback(){
        @Override public void onStartSuccess(AdvertiseSettings s){state="BLE 等待电脑";Log.i("VoiceStickBLE","advertising ready");}
        @Override public void onStartFailure(int e){state="BLE 广播失败 "+e;Log.w("VoiceStickBLE","advertising failed "+e);}
    };
    WatchBle(Context context,String token) throws IOException {
        if(!token.matches("[0-9a-fA-F]{32}")) throw new IOException("请先用电脑连接并配对一次");
        key=new byte[16];
        for(int i=0;i<16;i++) key[i]=(byte)Integer.parseInt(token.substring(i*2,i*2+2),16);
        BluetoothManager manager=context.getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter=manager.getAdapter();
        if(adapter==null || !adapter.isEnabled()) throw new IOException("请先打开手表蓝牙");
        advertiser=adapter.getBluetoothLeAdvertiser();
        if(advertiser==null) throw new IOException("此手表未开放 BLE 广播");
        server=manager.openGattServer(context,this);
        if(server==null) throw new IOException("无法启动手表 GATT 服务");
        pipe=new BluetoothGattCharacteristic(PIPE,BluetoothGattCharacteristic.PROPERTY_READ|BluetoothGattCharacteristic.PROPERTY_WRITE|BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE|BluetoothGattCharacteristic.PROPERTY_NOTIFY,BluetoothGattCharacteristic.PERMISSION_READ|BluetoothGattCharacteristic.PERMISSION_WRITE);
        pipe.addDescriptor(new BluetoothGattDescriptor(CCC, BluetoothGattDescriptor.PERMISSION_READ|BluetoothGattDescriptor.PERMISSION_WRITE));
        BluetoothGattService service=new BluetoothGattService(SERVICE,BluetoothGattService.SERVICE_TYPE_PRIMARY);
        service.addCharacteristic(pipe);
        if(!server.addService(service)){server.close();throw new IOException("无法注册 BLE 服务");}
    }
    @Override public synchronized void onServiceAdded(int status,BluetoothGattService service){
        if(closed) return;
        if(status!=0){state="BLE 服务失败 "+status;return;}
        advertiser.startAdvertising(new AdvertiseSettings.Builder().setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY).setConnectable(true).build(),
                new AdvertiseData.Builder().addServiceUuid(new ParcelUuid(SERVICE)).build(),advertising);
    }
    private void resetPeer(){
        for(Channel c:channels.values()) c.replies.offer(new byte[0]);
        channels.clear();authenticated=false;subscribed=false;mtu=23;rx=tx=0;inbound.reset();
        acknowledgments.offer(-1);
    }
    @Override public synchronized void onConnectionStateChange(BluetoothDevice d,int status,int next){
        if(closed)return;
        if(next==BluetoothProfile.STATE_CONNECTED){
            if(peer!=null && !peer.equals(d)){server.cancelConnection(d);return;}
            resetPeer();peer=d;random.nextBytes(nonce);
        }else if(d.equals(peer)){resetPeer();peer=null;state="BLE 等待电脑";}
    }
    @Override public synchronized void onMtuChanged(BluetoothDevice d,int n){if(d.equals(peer))mtu=n;}
    @Override public synchronized void onCharacteristicReadRequest(BluetoothDevice d,int id,int offset,BluetoothGattCharacteristic c){
        boolean ok=d.equals(peer)&&offset>=0&&offset<=8;
        server.sendResponse(d,id,ok?0:7,offset,ok?Arrays.copyOfRange(nonce,offset,8):null);
    }
    @Override public synchronized void onDescriptorWriteRequest(BluetoothDevice d,int id,BluetoothGattDescriptor v,boolean prepared,boolean respond,int offset,byte[] data){
        boolean ok=d.equals(peer)&&!prepared&&offset==0&&CCC.equals(v.getUuid());
        if(ok) subscribed=Arrays.equals(data,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
        if(respond)server.sendResponse(d,id,ok?0:6,offset,null);
    }
    private byte[] crypt(byte[] bytes,int sequence,boolean outgoing) throws Exception {
        byte[] iv=ByteBuffer.allocate(12).put(nonce).putInt(outgoing?sequence|0x80000000:sequence).array();
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(outgoing?Cipher.ENCRYPT_MODE:Cipher.DECRYPT_MODE,new SecretKeySpec(key,"AES"),new GCMParameterSpec(128,iv));
        return cipher.doFinal(bytes);
    }
    @Override public synchronized void onCharacteristicWriteRequest(BluetoothDevice d,int id,BluetoothGattCharacteristic c,boolean prepared,boolean respond,int offset,byte[] bytes){
        boolean ok=false;
        try {
            if(!d.equals(peer)||prepared||offset!=0||bytes.length<2)throw new IOException();
            if((bytes[0]&1)!=0)inbound.reset();
            if(inbound.size()+bytes.length>65536)throw new IOException();
            inbound.write(bytes,1,bytes.length-1);
            if((bytes[0]&2)!=0){
                byte[] data=crypt(inbound.toByteArray(),rx+1,false);
                if(data.length<2)throw new IOException();
                ++rx;
                if(data[0]=='H' && data[1]==0 && subscribed){authenticated=true;state="BLE 已连接";Log.i("VoiceStickBLE","authenticated mtu="+mtu);}
                else if(authenticated && data[0]=='J'){Channel ch=channels.get(data[1]&255);if(ch!=null) ch.replies.offer(Arrays.copyOfRange(data,2,data.length));}
            }
            ok=true;
        }catch(Exception ignored){inbound.reset();}
        if(respond)server.sendResponse(d,id,ok?0:6,0,null);
    }
    @Override public synchronized void onNotificationSent(BluetoothDevice d,int status){if(d.equals(peer))acknowledgments.offer(status);}
    synchronized int captureBytes() {
        // ATT header (3), fragment flag (1), GCM tag (16), command/id (2),
        // predictor and sample count (4). Two ADPCM samples per payload byte.
        int samples=Math.min(960, ((mtu-26)*2 / 80)*80);
        return samples>=320 ? samples*2 : 1920;
    }
    synchronized Channel open() throws IOException {
        if(!authenticated || peer==null)throw new IOException(state);
        session=session%255+1;
        Channel c=new Channel(session);channels.put(session,c);return c;
    }
    final class Channel {
        final int id;
        final LinkedBlockingQueue<byte[]> replies=new LinkedBlockingQueue<>(64);
        Channel(int id){this.id=id;}
        void send(char op,byte[] payload,int len)throws Exception{
            byte[] body=op=='A'?WatchAdpcm.encode(payload,len):Arrays.copyOf(payload==null?new byte[0]:payload,len);
            synchronized(txLock){
                byte[] packet; BluetoothDevice d; int size;
                synchronized(WatchBle.this){
                    if(!authenticated || channels.get(id)!=this)throw new IOException("BLE 已断开");
                    byte[] msg=new byte[2+body.length];msg[0]=(byte)op;msg[1]=(byte)id;System.arraycopy(body,0,msg,2,body.length);
                    packet=crypt(msg,++tx,true);size=Math.min(510,mtu-4);d=peer;acknowledgments.clear();
                }
                try {
                    for(int pos=0;pos<packet.length;pos+=size){
                        int n=Math.min(size,packet.length-pos);
                        byte[] part=new byte[n+1];part[0]=(byte)((pos==0?1:0)|(pos+n==packet.length?2:0));
                        System.arraycopy(packet,pos,part,1,n);
                        if(server.notifyCharacteristicChanged(d,pipe,false,part)!=0 ||
                           !Integer.valueOf(0).equals(acknowledgments.poll(3,TimeUnit.SECONDS)))throw new IOException("BLE 发送超时");
                    }
                }catch(Exception e){server.cancelConnection(d);throw e;}
            }
        }
        byte[] read()throws Exception {
            byte[] r=replies.poll(30,TimeUnit.SECONDS);
            if(r==null||r.length==0)throw new IOException("BLE 已断开或超时");return r;
        }
        void close(){synchronized(WatchBle.this){channels.remove(id,this);replies.offer(new byte[0]);}}
    }
    public synchronized void close(){closed=true;resetPeer();peer=null;advertiser.stopAdvertising(advertising);server.close();}
}

package cn.sonic.voicestick.watch;

import android.media.AudioDeviceInfo;
import android.os.SystemClock;
import android.util.Log;
import java.util.Arrays;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import org.json.JSONObject;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;

final class WatchLink implements AutoCloseable {
    interface Update { void show(String text); }
    final Socket socket = new Socket();
    private volatile WatchBle.Channel radio;
    private DataOutputStream out;
    private BufferedReader in;

    void connect(String token) throws IOException {
        if (!token.matches("[a-fA-F0-9]{32}")) throw new IOException("请在电脑运行配对脚本");
        WatchBle ble=WatchBle.instance;
        if(ble!=null){ radio=ble.open(); return; }
        socket.connect(new InetSocketAddress("127.0.0.1",38465),1500);
        socket.setSoTimeout(3500);
        socket.setTcpNoDelay(true);
        out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
        in = new BufferedReader(new InputStreamReader(socket.getInputStream(),StandardCharsets.UTF_8));
        out.write(token.getBytes(StandardCharsets.US_ASCII));
    }
    JSONObject send(char cmd, byte[] audio, int length) throws Exception {
        writeFrame(cmd,audio,length);
        return readReply();
    }
    private void writeFrame(char cmd, byte[] audio,int length) throws Exception {
        if(radio!=null){radio.send(cmd,audio,length);return;}
        out.writeByte(cmd);
        out.writeShort(length);
        if (length > 0) out.write(audio,0,length);
        out.flush();
    }
    private JSONObject readReply() throws Exception {
        String response = radio==null?in.readLine():new String(radio.read(),StandardCharsets.UTF_8);
        if (response == null) throw new IOException("电脑已断开");
        JSONObject state = new JSONObject(response);
        if ("error".equals(state.optString("phase"))) throw new IOException(state.optString("lastError"));
        return state;
    }
    volatile boolean held = true;

    // One capture thread drains AudioRecord even when ADB or ASR is waiting.
    private final ArrayBlockingQueue<byte[]> frames = new ArrayBlockingQueue<>(64);
    private volatile boolean finished;
    private volatile Exception captureError;
    private int captured, sent, highWater, route = -1;
    private double strongestFrame;
    private long micStart, micEnd, maxWriteMs;
    private final long pressedAt = SystemClock.elapsedRealtime();

    private void capture() {
        AudioRecord mic = null;
        try {
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);
            int min = AudioRecord.getMinBufferSize(16000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (min <= 0) throw new IOException("麦克风不支持 16 kHz 录音");
            mic = new AudioRecord(MediaRecorder.AudioSource.MIC,16000,AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,Math.max(min * 2,8192));
            if (mic.getState() != AudioRecord.STATE_INITIALIZED) throw new IOException("麦克风不可用");
            if (!held) throw new IOException("按住时间太短");
            mic.startRecording();
            micStart = SystemClock.elapsedRealtime();
            AudioDeviceInfo device = mic.getRoutedDevice();
            if (device != null) route = device.getType();
            WatchBle ble=WatchBle.instance;
            byte[] buffer = new byte[ble==null?1920:ble.captureBytes()];
            while (held && SystemClock.elapsedRealtime()-micStart < 120000) {
                int n = mic.read(buffer,0,buffer.length);
                if (n<=0 || (n&1)!=0) throw new IOException("录音中断");
                double energy=0;
                for(int k=0;k<n;k+=2) {
                    int sample=(short)((buffer[k]&255) | (buffer[k+1]<<8));
                    energy+=(double)sample*sample;
                    // Raise quiet speech by 12 dB; compress louder peaks into the remaining headroom.
                    int amp=Math.abs(sample);
                    int scaled=amp<6144 ? amp*4 : 24576+(amp-6144)*8191/26624;
                    int adjusted=sample<0 ? -scaled : scaled;
                    buffer[k]=(byte)adjusted;
                    buffer[k+1]=(byte)(adjusted>>8);
                }
                strongestFrame=Math.max(strongestFrame,Math.sqrt(energy/(n/2)));
                captured+=n/2;
                if (!frames.offer(Arrays.copyOf(buffer,n))) throw new IOException("向电脑发送音频太慢，请检查无线连接后重试");
                highWater=Math.max(highWater,frames.size());
            }
        } catch(Exception e) { captureError=e; }
        finally {
            micEnd=SystemClock.elapsedRealtime();
            if(mic!=null) mic.release();
            finished=true;
        }
    }
    private volatile Exception sendError;
    private volatile int finalReplyNumber;
    private long endSent;
    private void upload() {
        try {
            int count=0;
            while (!finished || !frames.isEmpty()) {
                if (captureError!=null) throw captureError;
                byte[] chunk=frames.poll(80,TimeUnit.MILLISECONDS);
                if (chunk==null) continue;
                long begin=SystemClock.elapsedRealtime();
                writeFrame('A',chunk,chunk.length);
                maxWriteMs=Math.max(maxWriteMs,SystemClock.elapsedRealtime()-begin);
                sent+=chunk.length/2;
                count++;
            }
            if (captureError!=null) throw captureError;
            if (sent==0) throw new IOException("没有采到音频");
            endSent=SystemClock.elapsedRealtime();
            finalReplyNumber=count+1;
            writeFrame('E',null,0);
        } catch(Exception e) { sendError=e; close(); }
    }
    JSONObject record(String token, Update update) throws Exception {
        Thread reader = new Thread(this::capture,"watch-microphone");
        Thread sender = new Thread(this::upload,"watch-upload");
        reader.start();
        try {
            connect(token);
            send('S',null,0);
            socket.setSoTimeout(30000);
            sender.start();
            String last="";
            for (int count=1;;count++) {
                JSONObject response=readReply();
                if (radio==null ? count==finalReplyNumber : response.optBoolean("bleFinal")) return response;
                String text=response.optString("text");
                if (!text.isEmpty() && !text.equals(last)) { last=text; update.show(text); }
            }
        } catch(Exception e) { throw sendError==null ? e : sendError; }
        finally {
            close();
            sender.join();
            reader.join();
            long loudest=Math.round(20*Math.log10(Math.max(1,strongestFrame)/32768.0));
            Log.i("VoiceStickAudio","route="+route+" startMs="+(micStart==0?-1:micStart-pressedAt)
                    +" captureMs="+(micStart==0?0:micEnd-micStart)+" samples="+captured+" sent="+sent
                    +" loudestRmsDbfs="+loudest+" queueMax="+highWater+" maxWriteMs="+maxWriteMs+" afterCaptureMs="+(micStart==0?-1:SystemClock.elapsedRealtime()-micEnd)+" finalMs="+(endSent==0?-1:SystemClock.elapsedRealtime()-endSent));
        }
    }
    public void close() {
        held = false;
        if(radio!=null)radio.close();
        try { socket.close(); } catch (IOException ignored) {}
    }
}

package cn.sonic.voicestick.watch;

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
    private DataOutputStream out;
    private BufferedReader in;

    void connect(String token) throws IOException {
        if (!token.matches("[a-fA-F0-9]{32}")) throw new IOException("请在电脑运行配对脚本");
        socket.connect(new InetSocketAddress("127.0.0.1",38465),1500);
        socket.setSoTimeout(3500);
        socket.setTcpNoDelay(true);
        out = new DataOutputStream(socket.getOutputStream());
        in = new BufferedReader(new InputStreamReader(socket.getInputStream(),StandardCharsets.UTF_8));
        out.write(token.getBytes(StandardCharsets.US_ASCII));
    }
    JSONObject send(char cmd, byte[] audio, int length) throws Exception {
        out.writeByte(cmd);
        out.writeShort(length);
        if (length > 0) out.write(audio,0,length);
        out.flush();
        String response = in.readLine();
        if (response == null) throw new IOException("电脑已断开");
        JSONObject state = new JSONObject(response);
        if ("error".equals(state.optString("phase"))) throw new IOException(state.optString("lastError"));
        return state;
    }
    volatile boolean held = true;

    JSONObject record(Update update) throws Exception {
        int min = AudioRecord.getMinBufferSize(16000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (min <= 0) throw new IOException("麦克风不支持 16 kHz 录音");
        AudioRecord mic = new AudioRecord(MediaRecorder.AudioSource.MIC,16000,AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,Math.max(min * 2,8192));
        try {
            if (mic.getState() != AudioRecord.STATE_INITIALIZED) throw new IOException("麦克风不可用");
            if (!held) throw new IOException("按住时间太短");
            send('S',null,0);
            mic.startRecording();
            byte[] buffer = new byte[1920];
            long deadline = android.os.SystemClock.elapsedRealtime()+120000;
            String last = "";
            while (held && android.os.SystemClock.elapsedRealtime()<deadline) {
                int bytes = mic.read(buffer,0,buffer.length);
                if (bytes<=0) throw new IOException("录音中断");
                String text = send('A',buffer,bytes).optString("text");
                if (!text.isEmpty() && !text.equals(last)) { last=text; update.show(text); }
            }
            mic.stop();
            socket.setSoTimeout(30000);
            return send('E',null,0);
        } finally { mic.release(); }
    }
    public void close() {
        held = false;
        try { socket.close(); } catch (IOException ignored) {}
    }
}

package cn.sonic.voicestick.watch;

import android.Manifest;
import android.app.*;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import android.util.TypedValue;
import org.json.JSONObject;
import java.util.concurrent.*;

public final class WatchActivity extends Activity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private FrameLayout face;
    private TextView status, transcript;
    private ScrollView scroll;
    private String token;
    private WatchLink active;
    private boolean visible, busy, pinging, learning;
    private int boundKey, downKey = -1;
    private String[] actions = {"回车","撤销","粘贴"};
    private final int[] slots = {0,1};
    private final KeyButton[] buttons = new KeyButton[2];
    private final TextView[] captions = new TextView[2];
    private int side;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(5894);
        face = new FrameLayout(this);
        face.setBackgroundColor(0xff000000);
        setContentView(face);
        acceptPairing(getIntent());
        boundKey = getPreferences(0).getInt("key",-1);
        for (int i=0;i<2;i++) slots[i]=getPreferences(0).getInt("slot"+i,i);
        face.post(this::layout);
    }
    private void acceptPairing(Intent intent) {
        String value=intent.getStringExtra("pair");
        if (value!=null && value.matches("[0-9a-fA-F]{32}"))
            getPreferences(0).edit().putString("token",value).apply();
        token=getPreferences(0).getString("token","");
    }
    @Override protected void onNewIntent(Intent intent) { super.onNewIntent(intent); acceptPairing(intent); }
    private int px(int n) { return Math.round(n*side/480f); }
    private TextView label(String value, int size) {
        TextView text=new TextView(this);
        text.setText(value); text.setTextColor(-1);
        text.setTextSize(TypedValue.COMPLEX_UNIT_PX,px(size));
        text.setGravity(Gravity.CENTER);
        return text;
    }
    private void place(View view,int x,int y,int width,int height) {
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(px(width),px(height));
        p.leftMargin=px(x)+(face.getWidth()-side)/2;
        p.topMargin=px(y)+(face.getHeight()-side)/2;
        face.addView(view,p);
    }
    private void layout() {
        side=Math.min(face.getWidth(),face.getHeight());
        status=label("连接电脑…",24); place(status,110,27,260,56);
        status.setOnClickListener(v->settings());
        status.setContentDescription("连接状态和按键设置");
        scroll=new ScrollView(this);
        transcript=label("按住麦克风开始说话",22);
        transcript.setGravity(Gravity.START|Gravity.CENTER_VERTICAL);
        scroll.setFillViewport(true); scroll.addView(transcript);
        place(scroll,74,102,332,157);
        for(int i=0;i<2;i++) {
            final int index=i, x=i==0?65:347;
            buttons[i]=new KeyButton(false,index);
            place(buttons[i],x,304,68,68);
            buttons[i].setOnClickListener(v->shortcut(index));
            captions[i]=label("",19); place(captions[i],x-9,372,86,31);
            refresh(index);
        }
        KeyButton mic=new KeyButton(true,-1);
        place(mic,175,273,130,130);
        mic.setContentDescription("按住说话");
        mic.setOnTouchListener((v,e)-> {
            switch(e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: startVoice(); return true;
                case MotionEvent.ACTION_UP: finishVoice(); return true;
                case MotionEvent.ACTION_CANCEL: cancelVoice(); return true;
                default: return true;
            }
        });
        place(label("按住说话",20),160,409,160,35);
        ping.run();
    }
    private void refresh(int i) {
        captions[i].setText(actions[slots[i]]); buttons[i].setContentDescription(actions[slots[i]]); buttons[i].invalidate();
    }
    private class KeyButton extends View {
        final boolean mic; final int index; final Paint pen=new Paint(Paint.ANTI_ALIAS_FLAG);
        KeyButton(boolean mic,int index) { super(WatchActivity.this); this.mic=mic; this.index=index; setClickable(true); }
        protected void onDraw(Canvas canvas) {
            canvas.save(); canvas.scale(getWidth()/100f,getHeight()/100f);
            pen.setStyle(Paint.Style.FILL); pen.setColor(-1); canvas.drawCircle(50,50,49,pen);
            pen.setColor(0xff000000); pen.setStrokeWidth(5); pen.setStrokeCap(Paint.Cap.ROUND);
            if(mic) {
                canvas.drawRoundRect(41,23,59,59,9,9,pen);
                pen.setStyle(Paint.Style.STROKE); canvas.drawArc(34,34,66,68,0,180,false,pen);
                canvas.drawLine(50,68,50,76,pen); canvas.drawLine(43,76,57,76,pen);
            } else {
                pen.setTextAlign(Paint.Align.CENTER); pen.setTextSize(53);
                canvas.drawText(new String[]{"↵","↶","⎘"}[slots[index]],50,67,pen);
            }
            canvas.restore();
        }
    }
    private void settings() {
        if (busy) return;
        new AlertDialog.Builder(this).setItems(new String[]{"左键 · "+actions[slots[0]],"右键 · "+actions[slots[1]],"绑定实体键","取消实体键绑定"},(d,n)->{
            if(n<2) new AlertDialog.Builder(this).setItems(actions,(a,k)->{
                slots[n]=k; getPreferences(0).edit().putInt("slot"+n,k).apply(); refresh(n);
            }).show();
            else if(n==2) {
                learning=true; status.setText("请按要绑定的键");
                ui.removeCallbacks(learnTimeout);
                ui.postDelayed(learnTimeout,8000);
            }
            else { learning=false; boundKey=-1; getPreferences(0).edit().remove("key").apply(); }
        }).show();
    }
    private final Runnable learnTimeout=()->{
        if(learning) {
            learning=false;
            transcript.setText("未收到按键，可按住屏幕麦克风说话。");
        }
    };
    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        int key=e.getKeyCode();
        if(key==KeyEvent.KEYCODE_BACK && learning) { learning=false; status.setText("取消绑定"); return true; }
        if(key==KeyEvent.KEYCODE_BACK || key==KeyEvent.KEYCODE_HOME || key==KeyEvent.KEYCODE_POWER)
            return super.dispatchKeyEvent(e);
        if(learning && e.getAction()==KeyEvent.ACTION_UP) {
            learning=false; boundKey=key; getPreferences(0).edit().putInt("key",key).apply();
            status.setText("已绑定按键 "+key); return true;
        }
        if(learning) return true;
        if(key==boundKey) {
            if(e.getAction()==KeyEvent.ACTION_DOWN && e.getRepeatCount()==0) {
                downKey=key; ui.postDelayed(hardwareStart,ViewConfiguration.getLongPressTimeout());
            } else if(e.getAction()==KeyEvent.ACTION_UP) {
                ui.removeCallbacks(hardwareStart); downKey=-1;
                if(e.isCanceled()) cancelVoice(); else finishVoice();
            }
            return true;
        }
        return super.dispatchKeyEvent(e);
    }
    private final Runnable hardwareStart=()->{ if(downKey>=0 && visible) startVoice(); };
    private void startVoice() {
        if(!visible || busy) return;
        if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},1); return;
        }
        busy=true; WatchLink link=new WatchLink(); active=link;
        status.setText("正在聆听"); transcript.setText("请说话…");
        face.setKeepScreenOn(true);
        new Thread(()->{
            try {
                JSONObject result=link.record(token,value->ui.post(()->{
                    if(active==link) { transcript.setText(value); scroll.post(()->scroll.fullScroll(View.FOCUS_DOWN)); }
                }));
                ui.post(()->{
                    if(active!=link) return;
                    String text=result.optString("text");
                    String error=result.optString("lastError","");
                    boolean warning=!error.isEmpty() && !error.equals("null");
                    transcript.setText(text.isEmpty()?"未识别到文字":text);
                    complete(warning?("completed".equals(result.optString("phase"))?"已输入 · 复制失败":"请检查电脑输入"):text.isEmpty()?"已结束":"识别完成");
                });
            } catch(Exception e) { ui.post(()->{ if(active==link) { transcript.setText(message(e)); complete("录音未完成"); } }); }
            finally { link.close(); }
        },"watch-dictation").start();
    }
    private String message(Exception e) { return e instanceof java.net.SocketException ? "连接中断，请检查电脑接收器" : e.getMessage(); }
    private void finishVoice() { if(active!=null && active.held) { active.held=false; status.setText("正在识别"); } }
    private void complete(String note) { busy=false; active=null; status.setText(note); face.setKeepScreenOn(false); }
    private void cancelVoice() {
        if(active!=null) { active.close(); complete("已取消"); }
    }
    private void shortcut(int i) {
        if(busy || !visible) return;
        char cmd="RUV".charAt(slots[i]); String title=actions[slots[i]];
        busy=true;
        worker.execute(()->{
            String note;
            try(WatchLink l=new WatchLink()) { l.connect(token); l.send(cmd,null,0); note=title+"已发送"; }
            catch(Exception e) { note="快捷键发送失败"; }
            String result=note; ui.post(()->{ busy=false; if(visible) status.setText(result); });
        });
    }
    private final Runnable ping=new Runnable() {
        public void run() {
            ui.removeCallbacks(this);
            if(!visible || status==null) return;
            if(!busy && !pinging && !learning) {
                pinging=true;
                final byte[] current={(byte)slots[0],(byte)slots[1]};
                worker.execute(()->{
                    boolean ok;
                    try(WatchLink link=new WatchLink()) {
                        link.connect(token);
                        org.json.JSONArray keys=link.send('K',current,2).optJSONArray("keys");
                        if(keys!=null && keys.length()==2) {
                            int left=keys.getInt(0),right=keys.getInt(1);
                            if(left>=0 && left<3 && right>=0 && right<3) ui.post(()->{
                                slots[0]=left; slots[1]=right;
                                getPreferences(0).edit().putInt("slot0",left).putInt("slot1",right).apply();
                                refresh(0); refresh(1);
                            });
                        }
                        ok=true;
                    }
                    catch(Exception e) {
                        // The standalone 0.1.0 receiver understands the original heartbeat.
                        try(WatchLink old=new WatchLink()) { old.connect(token); old.send('P',null,0); ok=true; }
                        catch(Exception ignored) { ok=false; }
                    }
                    String note=ok?"已连接电脑":"未连接电脑";
                    ui.post(()->{ pinging=false; if(visible && !busy && !learning) status.setText(note); });
                });
            }
            ui.postDelayed(this,5000);
        }
    };
    @Override protected void onResume() { super.onResume(); visible=true; ping.run(); }
    @Override protected void onPause() {
        visible=false; learning=false; downKey=-1; ui.removeCallbacks(learnTimeout); ui.removeCallbacks(hardwareStart); ui.removeCallbacks(ping);
        cancelVoice(); super.onPause();
    }
    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }
}

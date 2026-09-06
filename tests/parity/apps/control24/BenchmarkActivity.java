package org.westlake.gapcontrol;
import android.app.Activity;
import android.os.*;
import android.view.*;
import android.webkit.*;
import android.widget.*;

/** Identical offline workload on both devices. Times are device-monotonic. */
public final class BenchmarkActivity extends Activity {
    final Handler handler=new Handler();WebView web;TextView status;
    int sample=0,clicks=0;long start,eventTime;boolean completed;
    final String page=page();
    static String page(){
        StringBuilder s=new StringBuilder("<!doctype html><meta name='viewport' content='width=400'><meta charset='utf-8'><link rel='icon' href='data:,'><title>Locked offline page</title><style>body{margin:0;width:400px;font:16px sans-serif}article{border-bottom:1px solid #ddd;padding:8px;background:#edf8ef}</style><h1>离线性能测试</h1>");
        for(int i=0;i<120;i++)s.append("<article><b>新闻页面 ").append(i).append("</b><p>Fixed text and layout for both devices. 中文输入响应测试。</p></article>");
        return s.toString();
    }
    void log(String s){System.err.println("[GAP_BENCH] "+s);status.setText(s);}
    void next(){
        if(sample==12){log("PAGE_COMPLETE samples=12 warmup=2");return;}
        completed=false;start=SystemClock.uptimeMillis();
        web.loadDataWithBaseURL("https://fixture.invalid/",page,"text/html","UTF-8",null);
        final int run=sample;
        handler.postDelayed(new Runnable(){public void run(){if(run==sample&&!completed){log("FAIL page_timeout sample="+run);completed=true;}}},15000);
    }
    @Override public boolean dispatchTouchEvent(MotionEvent event){
        if(event.getActionMasked()==MotionEvent.ACTION_UP)eventTime=event.getEventTime();
        return super.dispatchTouchEvent(event);
    }
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(0,100,0,0);
        Button button=new Button(this);button.setText("Tap response control");root.addView(button,new LinearLayout.LayoutParams(-1,120));
        status=new TextView(this);root.addView(status,new LinearLayout.LayoutParams(-1,100));
        WebView.setDataDirectorySuffix("bench-"+android.os.Process.myPid());web=new WebView(this);web.getSettings().setJavaScriptEnabled(true);
        root.addView(web,new LinearLayout.LayoutParams(-1,0,1));setContentView(root);
        button.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
            final long event=eventTime;final long click=SystemClock.uptimeMillis();final int id=++clicks;
            log("INPUT sample="+id+" event_to_click_ms="+(click-event));
            Choreographer.getInstance().postFrameCallback(new Choreographer.FrameCallback(){public void doFrame(long frame){
                log("INPUT_FRAME sample="+id+" event_to_frame_callback_ms="+(SystemClock.uptimeMillis()-event));
            }});
        }});
        web.setWebViewClient(new WebViewClient(){
            @Override public void onPageFinished(WebView v,String url){
                if(completed)return;final int id=sample;final long elapsed=SystemClock.uptimeMillis()-start;
                web.postVisualStateCallback(id,new WebView.VisualStateCallback(){public void onComplete(long request){
                    if(id!=sample||completed)return;completed=true;
                    log("PAGE sample="+id+" load_finished_ms="+elapsed+" visual_ready_ms="+(SystemClock.uptimeMillis()-start));
                    System.err.println("[BENCH_PHASE] sample="+id+" end_uptime="+SystemClock.uptimeMillis()+" cpu_ms="+android.os.Process.getElapsedCpuTime()+" focus="+web.hasWindowFocus()+" visibility="+web.getWindowVisibility());
                    sample++;handler.postDelayed(new Runnable(){public void run(){next();}},200);
                }});
            }
            @Override public void onReceivedError(WebView v,WebResourceRequest r,WebResourceError e){if(r.isForMainFrame())log("FAIL main_frame_error="+e.getErrorCode());}
        });
        log("CONFIG width="+getResources().getDisplayMetrics().widthPixels+" density="+getResources().getDisplayMetrics().densityDpi+" html_chars="+page.length());
        handler.postDelayed(new Runnable(){public void run(){next();}},700);
    }
}

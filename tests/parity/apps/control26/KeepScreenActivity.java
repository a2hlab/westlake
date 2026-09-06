package org.westlake.gapcontrol;
import android.app.Activity;
import android.app.Dialog;
import android.os.*;
import android.view.*;
import android.widget.TextView;

/** Public-API state transitions; external power dumps verify server effects. */
public final class KeepScreenActivity extends Activity {
    final Handler handler=new Handler(); TextView text; Dialog dialog;
    static volatile long result;
    void log(String phase) {
        text.setText(phase);
        try {java.lang.reflect.Field f=Activity.class.getDeclaredField("mDecor");f.setAccessible(true);
            System.err.println("[WINDOW_REF_DIAG] phase="+phase+" activityDecor="+f.get(this)+" windowDecor="+getWindow().peekDecorView()+" attached="+getWindow().getDecorView().isAttachedToWindow()+" callback="+getWindow().getCallback().getClass()+" flags="+getWindow().getAttributes().flags);
        }catch(Throwable t){System.err.println("[WINDOW_REF_DIAG] "+t);}

        System.err.println("[WINDOW_REF] phase="+phase+" uptime="+SystemClock.uptimeMillis());
    }
    void after(long ms,Runnable r){handler.postDelayed(r,ms);}
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);text=new TextView(this);text.setTextSize(28);setContentView(text);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);log("main_on");
        after(1000,()->new Thread(()->{
            try {
                long a=android.os.Process.getElapsedCpuTime(),start=SystemClock.elapsedRealtime();
                long x=1;while(SystemClock.elapsedRealtime()-start<300){for(int i=0;i<10000;i++)x=x*1664525+1013904223;result=x;}
                long b=android.os.Process.getElapsedCpuTime();Thread.sleep(300);
                long c=android.os.Process.getElapsedCpuTime();
                System.err.println("[CPU_REF] busy_ms="+(b-a)+" sleep_ms="+(c-b)+" monotonic="+(a<=b&&b<=c)+" pass="+(b-a>=70&&c-b<150));
            }catch(Throwable t){System.err.println("[CPU_REF] FAIL "+t);}
        },"cpu-reference").start());
        after(5000,()->{getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);log("main_off");});
        after(8000,()->{
            dialog=new Dialog(this);TextView d=new TextView(this);d.setText("Dialog holds the display");dialog.setContentView(d);
            dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);dialog.show();log("dialog_on");
        });
        after(13000,()->{dialog.dismiss();log("dialog_removed");});
        after(16000,()->{getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);log("idle_hold_start");});
        after(56000,()->{
            android.os.PowerManager power=(android.os.PowerManager)getSystemService(POWER_SERVICE);
            System.err.println("[WINDOW_REF] idle_hold_interactive="+(power==null?"unavailable":power.isInteractive()));
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);log("idle_release");
        });
        after(96000,()->log("complete"));
    }
}

package org.westlake.gapcontrol;
import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.SystemClock;
import android.net.http.SslError;
import android.webkit.*;
import android.widget.*;
import android.view.View;

/** Same diagnostic APK runs on OHOS and Android; no app or provider internals. */
public final class WebActivity extends Activity {
    WebView web; TextView status; long start; int generation;
    final Handler handler=new Handler();
    void report(String text) {
        String result="[GAP_WEB] elapsed="+(SystemClock.uptimeMillis()-start)+" "+text;
        System.err.println(result); status.setText(result);
    }
    void sample(String phase) {
        try {
            report(phase+" url="+web.getUrl()+" title="+web.getTitle()+" height="+web.getContentHeight()+" progress="+web.getProgress());
            web.evaluateJavascript("JSON.stringify({title:document.title,ready:document.readyState,text:document.body?document.body.innerText.slice(0,120):null,height:document.documentElement.scrollHeight})",
                new ValueCallback<String>() { public void onReceiveValue(String s) { report("JS="+s); }});
        } catch(Throwable t) { report("sample-error="+t); t.printStackTrace(); }
    }
    void load(boolean online) {
        generation++; start=SystemClock.uptimeMillis();
        try {
            report("start mode="+(online?"https":"local"));
            if(online) web.loadUrl("https://example.com/");
            else web.loadDataWithBaseURL("https://westlake.invalid/", "<!doctype html><meta charset='utf-8'><title>Westlake local</title><body style='background:#d7f7e8;font-size:32px'><h1>LOCAL WEBVIEW PASS 中文</h1><input id='editor' value='你好'><p id='js'></p><script>document.getElementById('js').textContent='JAVASCRIPT PASS';</script><div style='height:1800px'>Scroll control</div></body>","text/html","UTF-8",null);
            final int current=generation;
            handler.postDelayed(new Runnable(){public void run(){if(current==generation)sample("timer-3000");}},3000);
            handler.postDelayed(new Runnable(){public void run(){if(current==generation)sample("timer-10000");}},10000);
        } catch(Throwable t) { report("load-error="+t); t.printStackTrace(); }
    }
    @Override public void onCreate(Bundle b) {
        super.onCreate(b); start=SystemClock.uptimeMillis();
        PermissionProbe.run(this);
        new Thread(new TrustProbe(),"trust-probe").start();
        new Thread(new NetworkProbe(),"network-probe").start();
        new Thread(new NetworkStressProbe(),"network-stress").start();
        new Thread(new CertificateProbe(this),"certificate-probe").start();
        new Thread(new CryptoProbe(),"crypto-probe").start();
        LinearLayout root=new LinearLayout(this); root.setOrientation(1); root.setPadding(0,100,0,0);
        LinearLayout buttons=new LinearLayout(this);
        Button local=new Button(this);local.setText("Local page");buttons.addView(local,new LinearLayout.LayoutParams(0,100,1));
        Button online=new Button(this);online.setText("HTTPS page");buttons.addView(online,new LinearLayout.LayoutParams(0,100,1));
        Button software=new Button(this);software.setText("Software draw");buttons.addView(software,new LinearLayout.LayoutParams(0,100,1));
        root.addView(buttons);status=new TextView(this);status.setTextSize(12);root.addView(status);
        WebView.setDataDirectorySuffix("gap-"+android.os.Process.myPid());
        web=new WebView(this); root.addView(web,new LinearLayout.LayoutParams(-1,0,1));setContentView(root);
        try {
            WebView.setWebContentsDebuggingEnabled(true);
            web.getSettings().setJavaScriptEnabled(true);web.getSettings().setDomStorageEnabled(true);
            report("settings js="+web.getSettings().getJavaScriptEnabled()+" dom="+web.getSettings().getDomStorageEnabled());
        } catch(Throwable t) {report("settings-error="+t);t.printStackTrace();}
        web.setWebChromeClient(new WebChromeClient(){public boolean onConsoleMessage(ConsoleMessage m){report("console="+m.message());return false;}});
        web.setWebViewClient(new WebViewClient(){
            @Override public void onPageFinished(WebView v,String url){sample("finished");}
            @Override public void onReceivedError(WebView v,WebResourceRequest r,WebResourceError e){report("error="+e.getErrorCode()+" main="+r.isForMainFrame()+" description="+e.getDescription());}
            @Override public void onReceivedSslError(WebView v,SslErrorHandler h,SslError e){report("SSL_ERROR="+e.getPrimaryError());h.cancel();}
        });
        local.setOnClickListener(new View.OnClickListener(){public void onClick(View v){load(false);}});
        online.setOnClickListener(new View.OnClickListener(){public void onClick(View v){load(true);}});
        software.setOnClickListener(new View.OnClickListener(){public void onClick(View v){web.setLayerType(View.LAYER_TYPE_SOFTWARE,null);web.invalidate();report("software draw selected");}});
        handler.postDelayed(new Runnable(){public void run(){load(false);}},300);
    }
}

package org.westlake.gapcontrol;
import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;

public final class ImeActivity extends Activity {
    EditText first,second;TextView status;LinearLayout root;final Handler handler=new Handler();
    void log(String message){System.err.println("[GAP_IME] "+message);status.setText(message);}
    void invoke(String method,Class<?>[] types,Object... args)throws Exception{
        Class.forName("adapter.window.OhImeBridge").getMethod(method,types).invoke(null,args);
    }
    void check(boolean ok,String label){log((ok?"PASS ":"FAIL ")+label);}
    void test(){
        first.requestFocus();((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).showSoftInput(first,0);
        handler.postDelayed(new Runnable(){public void run(){try{
            first.setText("A😀Z");first.setSelection(3);
            invoke("nativeOnDeleteBefore",new Class<?>[]{int.class},1);
            handler.post(new Runnable(){public void run(){
                check(first.getText().toString().equals("AZ"),"emoji delete");
                try{
                    first.setText("ABC");first.setSelection(1,2);
                    invoke("nativeOnInsertText",new Class<?>[]{String.class},"中");
                    handler.post(new Runnable(){public void run(){
                        check(first.getText().toString().equals("A中C"),"selected replacement");
                        try{
                            invoke("nativeOnInsertText",new Class<?>[]{String.class},"STALE");
                            second.requestFocus();
                            handler.post(new Runnable(){public void run(){check(!first.getText().toString().contains("STALE")&&!second.getText().toString().contains("STALE"),"stale queued edit discarded");log("COMPLETE");}});
                        }catch(Exception e){log("FAIL "+e);}
                    }});
                }catch(Exception e){log("FAIL "+e);}
            }});
        }catch(Exception e){log("UNAVAILABLE bridge-only tests "+e);}}},500);
    }
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        root=new LinearLayout(this);root.setOrientation(1);root.setPadding(10,100,10,30);
        Button run=new Button(this);run.setText("Test bridge edits");root.addView(run,new LinearLayout.LayoutParams(-1,100));
        status=new TextView(this);status.setText("Tap bottom editor: it must stay above keyboard");root.addView(status);
        first=new EditText(this);first.setSingleLine(true);first.setHint("First editor");root.addView(first,new LinearLayout.LayoutParams(-1,140));
        View spacer=new View(this);root.addView(spacer,new LinearLayout.LayoutParams(-1,0,1));
        second=new EditText(this);second.setSingleLine(true);second.setHint("Bottom editor 中文 😀");root.addView(second,new LinearLayout.LayoutParams(-1,140));
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener(){public WindowInsets onApplyWindowInsets(View v,WindowInsets insets){
            System.err.println("[GAP_IME] insets bottom="+insets.getInsets(WindowInsets.Type.ime()).bottom+" visible="+insets.isVisible(WindowInsets.Type.ime()));
            return v.onApplyWindowInsets(insets);
        }});
        root.getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener(){public void onGlobalLayout(){int[] p=new int[2];second.getLocationOnScreen(p);System.err.println("[GAP_IME] layout height="+root.getHeight()+" editorBottom="+(p[1]+second.getHeight()));}});
        setContentView(root);run.setOnClickListener(new View.OnClickListener(){public void onClick(View v){test();}});
    }
}

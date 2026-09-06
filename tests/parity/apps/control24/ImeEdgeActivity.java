package org.westlake.gapcontrol;
import android.app.Activity;
import android.os.*;
import android.view.*;
import android.view.inputmethod.*;
import android.widget.*;
import java.lang.reflect.*;
import java.util.*;
import android.graphics.Rect;
import android.app.Dialog;

/** Boundary controls: no APK patch, independent assertions, no fabricated IME. */
public final class ImeEdgeActivity extends Activity {
    final Handler ui=new Handler(); EditText first,second; TextView status;
    InputMethodManager imm; boolean started; int failures;
    void log(String text){System.err.println("[GAP_IME_EDGE] "+text);status.setText(text);}
    void check(boolean ok,String name){if(!ok)failures++;log((ok?"PASS ":"FAIL ")+name);}
    Object call(String name,Class<?>[] types,Object... args)throws Exception {
        return Class.forName("adapter.window.OhImeBridge").getMethod(name,types).invoke(null,args);
    }
    long generation()throws Exception{return (Long)call("nativeGetGeneration",new Class<?>[]{});}
    void op(long generation,int op,int arg,String text,int end)throws Exception {
        call("nativeOnOperation",new Class<?>[]{long.class,int.class,int.class,String.class,int.class},generation,op,arg,text,end);
    }
    @Override public void onCreate(Bundle state){super.onCreate(state);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(15,100,15,20);
        status=new TextView(this);root.addView(status);first=new EditText(this);first.setHint("Complex Unicode and large document control");first.setMaxLines(3);root.addView(first,new LinearLayout.LayoutParams(-1,160));
        second=new EditText(this);second.setHint("New session destination");root.addView(second,new LinearLayout.LayoutParams(-1,160));
        setContentView(root);imm=(InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);
        if(focus&&!started){started=true;ui.postDelayed(()->start(),700);}
    }
    void start(){try{
        List<InputMethodInfo> installed=imm.getInputMethodList(),enabled=imm.getEnabledInputMethodList();
        InputMethodInfo current=imm.getCurrentInputMethodInfo();
        check(!installed.isEmpty()&&!enabled.isEmpty()&&current!=null,"enumeration installed="+installed.size()+" enabled="+enabled.size()+" current="+(current==null?null:current.getId()));
        if(current!=null)check(imm.getCurrentInputMethodSubtype()!=null&&!imm.getEnabledInputMethodSubtypeList(current,true).isEmpty(),"actual current subtype");
        first.requestFocus();imm.showSoftInput(first,0);ui.postDelayed(()->binding(),600);
    }catch(Throwable e){check(false,"start "+e);}}
    void binding(){try{
        Method available=InputMethodManager.class.getDeclaredMethod("isImeSessionAvailableLocked");available.setAccessible(true);
        Field id=InputMethodManager.class.getDeclaredField("mCurId");id.setAccessible(true);
        check((Boolean)available.invoke(imm)&&id.get(imm)!=null&&generation()>0,"Android session bound id="+id.get(imm));
        unicode(0);
    }catch(Throwable e){check(false,"binding "+e);}}
    final String[] clusters={"😀","e\u0301","👩‍👩‍👧‍👦","👍🏽","🇨🇳"};
    void unicode(int index){try{
        if(index==clusters.length){large();return;}
        String cluster=clusters[index];first.setText("A"+cluster+"Z");first.setSelection(1+cluster.length());
        op(generation(),1,1,null,-1);
        ui.post(()->{check(first.getText().toString().equals("AZ"),"grapheme-delete index="+index);unicode(index+1);});
    }catch(Throwable e){check(false,"unicode "+e);}}
    void large(){try{
        char[] chars=new char[40000];Arrays.fill(chars,'x');first.setText(new String(chars));first.setSelection(30000,30002);
        op(generation(),0,0,"中文",-1);
        ui.post(()->{try{
            check(first.getText().length()==40000&&first.getText().subSequence(30000,30002).toString().equals("中文")
                &&((Integer)call("nativeGetCursor",new Class<?>[]{}))==30002,"large-document absolute cursor and selected replacement");
            final long old=generation();op(old,0,0,"QUEUED_OLD",-1);second.requestFocus();imm.showSoftInput(second,0);
            ui.postDelayed(()->late(old),500);
        }catch(Throwable e){check(false,"large "+e);}});
    }catch(Throwable e){check(false,"large setup "+e);}}
    void late(long old){try{
        long next=generation();check(next>old,"new editor epoch");
        op(old,0,0,"LATE_OLD",-1);op(next,0,0,"新",-1);
        ui.post(()->{check(!first.getText().toString().contains("OLD")&&!second.getText().toString().contains("OLD")&&second.getText().toString().equals("新"),"late callback and queued old edit rejected");
            geometry();});
    }catch(Throwable e){check(false,"late "+e);}}
    void geometry(){try{
        Method frame=Class.forName("adapter.window.OhInsetsBridge").getMethod("insetFrame",Rect.class,Rect.class);
        Rect portrait=new Rect(0,0,1200,1920),dock=new Rect(0,1094,1200,1920);
        check(frame.invoke(null,portrait,dock).equals(dock),"portrait dock geometry");
        check(((Rect)frame.invoke(null,portrait,new Rect(200,700,800,1100))).isEmpty(),"floating keyboard does not resize whole window");
        Rect landscape=new Rect(0,0,1920,1200),horizontal=new Rect(0,800,1920,1200);
        check(frame.invoke(null,landscape,horizontal).equals(horizontal),"landscape dock geometry");
        Class<?> source=Class.forName("android.view.InsetsSource");
        Field id=source.getField("ID_IME");Object insets=source.getConstructor(int.class,int.class).newInstance(id.getInt(null),WindowInsets.Type.ime());
        source.getMethod("setFrame",Rect.class).invoke(insets,dock);source.getMethod("setVisible",boolean.class).invoke(insets,true);
        Method calculate=source.getMethod("calculateInsets",Rect.class,boolean.class);
        check(((android.graphics.Insets)calculate.invoke(insets,new Rect(0,0,1200,960),false)).bottom==0
                &&((android.graphics.Insets)calculate.invoke(insets,new Rect(0,960,1200,1920),false)).bottom==826,"split-window insets intersection");
        dialog();
    }catch(Throwable e){check(false,"geometry "+e);log("COMPLETE failures="+failures);}}
    void dialog(){try{
        final long old=generation();final Dialog dialog=new Dialog(this);final EditText edit=new EditText(this);edit.setHint("Second-window editor");dialog.setContentView(edit);
        dialog.show();dialog.getWindow().setLayout(800,500);
        ui.postDelayed(()->{try{
            edit.requestFocus();imm.showSoftInput(edit,0);
            ui.postDelayed(()->{try{
                long next=generation();check(next>old,"second window gets independent editor epoch");
                op(old,0,0,"WRONG_WINDOW",-1);op(next,0,0,"窗口",-1);
                ui.post(()->{check(edit.getText().toString().equals("窗口")&&!second.getText().toString().contains("WRONG_WINDOW"),"callback routed to focused dialog window");
                    dialog.dismiss();log("COMPLETE failures="+failures);});
            }catch(Throwable e){check(false,"dialog callback "+e);dialog.dismiss();}},500);
        }catch(Throwable e){check(false,"dialog bind "+e);dialog.dismiss();}},600);
    }catch(Throwable e){check(false,"dialog "+e);}}
}

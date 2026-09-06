package adapter.window;
import android.os.IBinder;
import android.view.IWindow;
import android.view.View;
import android.view.WindowManager;
import java.lang.reflect.Field;
import java.util.HashMap;
import java.util.Map;

/** Android window power requests mapped to the owning OH window. */
public final class OhWindowPolicy {
    private OhWindowPolicy() {}
    private static final Map<IBinder,State> states=new HashMap<>();
    private static Field sessions;
    private static final class State { int session; boolean requested, applied; }
    private static final class Native {
        static { System.load(System.getenv("WESTLAKE_WINDOW_POLICY_PATH")); }
        static void load() {}
    }
    private static native int nativeSetKeepScreenOn(int session, boolean value);
    @SuppressWarnings("unchecked")
    public static synchronized void update(Object adapter,IWindow window,WindowManager.LayoutParams attrs,
            int requestedWidth,int requestedHeight,int visibility) {
        try {
            State state=states.get(window.asBinder());
            boolean requested=attrs!=null ? (attrs.flags&WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)!=0
                    : state!=null&&state.requested;
            if (state==null&&!requested) return;
            if (sessions==null) { sessions=adapter.getClass().getDeclaredField("mSessionMap");sessions.setAccessible(true); }
            int[] info=((Map<IBinder,int[]>)sessions.get(adapter)).get(window.asBinder());
            if (info==null||info.length==0) return;
            if (state==null) { state=new State();state.session=info[0];states.put(window.asBinder(),state); }
            state.requested=requested;
            boolean effective=requested&&visibility==View.VISIBLE;
            if (effective==state.applied) return;
            Native.load();int rc=nativeSetKeepScreenOn(state.session,effective);
            System.err.println("[OH_WINDOW_POLICY] session="+state.session+" keepScreenOn="+effective+" rc="+rc);
            if (rc==0) state.applied=effective;
        } catch(Throwable failure) { System.err.println("[OH_WINDOW_POLICY] update failed "+failure); }
    }
    public static synchronized void remove(IWindow window) {
        State state=states.remove(window.asBinder());
        if (state!=null&&state.applied) {
            try { Native.load();int rc=nativeSetKeepScreenOn(state.session,false);
                System.err.println("[OH_WINDOW_POLICY] remove session="+state.session+" rc="+rc);
            }catch(Throwable failure){System.err.println("[OH_WINDOW_POLICY] remove failed "+failure);}
        }
    }
    /** Submitted state only. Verify server effects independently in power dumps. */
    public static synchronized int[] submittedStates() {
        int[] result=new int[states.size()*2];int i=0;
        for(State state:states.values()){result[i++]=state.session;result[i++]=state.applied?1:0;}
        return result;
    }
}

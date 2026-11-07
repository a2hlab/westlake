package adapter.activity;

import android.app.ActivityThread;
import android.app.servertransaction.ActivityLifecycleItem;
import android.app.servertransaction.ClientTransaction;
import android.app.servertransaction.DestroyActivityItem;
import android.os.Handler;
import android.os.Message;

import java.lang.reflect.Field;
import java.util.Map;

/**
 * Two things the app's process gets once it is bound (AppSchedulerBridge), before the app's code runs.
 *
 * A destroy for an activity that is already gone is dropped. Westlake destroys a finished activity
 * itself (ActivityClientControllerAdapter.scheduleLocalDestroy), and the native input bridge does the
 * same after a Back key (WESTLAKE-DIRECTFINISH-735, which cannot be rebuilt today), with an empty
 * ActivityResultItem as its callback. When both run, the second finds no activity record: Android
 * ignores a lifecycle request for a missing record, but its result item throws on the main thread,
 * and the app dies. TP-Link Tapo died that way on Back. The activity manager never sends such a
 * transaction, so dropping it changes nothing an app could see. Installed as ActivityThread.mH's
 * Handler.Callback, ahead of any the app adds (those keep this one and call it, as hooks do).
 *
 * A System.exit is logged, with the stack that called it. An app that ends itself so left no trace:
 * Westlake's native exit log is not in the runtime, and an exit is not a crash, so nothing is
 * dumped. A shutdown hook runs on System.exit (not on Process.killProcess, a SIGKILL).
 */
final class MainThreadGuards {
    private static boolean sInstalled;

    private MainThreadGuards() {}

    static synchronized void install(ActivityThread thread) {
        if (sInstalled || thread == null) return;
        sInstalled = true;
        try {
            Runtime.getRuntime().addShutdownHook(new Thread(MainThreadGuards::reportExit, "wl-exit-trace"));
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-EXIT] exit trace not installed: " + t);
        }
        try {
            Field hField = ActivityThread.class.getDeclaredField("mH");
            hField.setAccessible(true);
            Handler h = (Handler) hField.get(thread);
            Field callbackField = Handler.class.getDeclaredField("mCallback");
            callbackField.setAccessible(true);
            final Handler.Callback previous = (Handler.Callback) callbackField.get(h);
            callbackField.set(h, (Handler.Callback) msg -> {
                if (isStaleDestroy(thread, msg)) return true;
                return previous != null && previous.handleMessage(msg);
            });
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-STALE-DESTROY] guard not installed: " + t);
        }
    }

    /** A transaction whose lifecycle request destroys an activity that has no record any more. */
    private static boolean isStaleDestroy(ActivityThread thread, Message msg) {
        if (msg.what != 159 /* ActivityThread.H.EXECUTE_TRANSACTION */ || !(msg.obj instanceof ClientTransaction)) {
            return false;
        }
        try {
            ActivityLifecycleItem request = ((ClientTransaction) msg.obj).getLifecycleStateRequest();
            if (!(request instanceof DestroyActivityItem)) return false;
            if (thread.getActivityClient(request.getActivityToken()) != null) return false;
            System.err.println("[WESTLAKE-STALE-DESTROY] dropped a destroy for an activity already destroyed");
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    private static void reportExit() {
        StringBuilder out = new StringBuilder("[WESTLAKE-EXIT] the process is exiting (System.exit or the last thread)");
        try {
            for (Map.Entry<Thread, StackTraceElement[]> e : Thread.getAllStackTraces().entrySet()) {
                StackTraceElement[] stack = e.getValue();
                int at = -1;
                for (int i = 0; i < stack.length; i++) {
                    String frame = stack[i].getClassName() + "." + stack[i].getMethodName();
                    if (frame.equals("java.lang.Runtime.exit") || frame.equals("java.lang.System.exit")) at = i;
                }
                if (at < 0) continue;
                out.append("; called on thread \"").append(e.getKey().getName()).append('"');
                for (int i = at; i < stack.length && i < at + 16; i++) out.append("\n\tat ").append(stack[i]);
            }
        } catch (Throwable t) {
            out.append("; stacks unavailable: ").append(t);
        }
        System.err.println(out);
        System.err.flush();
    }
}

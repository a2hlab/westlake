package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.servertransaction.ClientTransaction;
import android.app.servertransaction.DestroyActivityItem;
import android.app.servertransaction.NewIntentItem;
import android.content.ComponentName;
import android.content.Intent;
import android.os.IBinder;
import android.os.Looper;
import com.android.internal.content.ReferrerIntent;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Intent reuse for the direct-launch task whose activities have no individual OH ability. */
final class LocalActivityLaunch {
    private LocalActivityLaunch() {}

    static boolean reuse(Intent intent, ActivityResults.Request request) {
        if (intent == null || Looper.myLooper() != Looper.getMainLooper()) return false;
        ComponentName component = intent.getComponent();
        String own = ActivityThread.currentPackageName();
        if (component == null || own == null || !own.equals(component.getPackageName())) return false;
        // Cross-task and manifest launch modes still belong to the ability/task launch path.
        int flags = intent.getFlags();
        if ((flags & Intent.FLAG_ACTIVITY_SINGLE_TOP) == 0
                || (flags & (Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                        | Intent.FLAG_ACTIVITY_NEW_DOCUMENT | Intent.FLAG_ACTIVITY_CLEAR_TASK)) != 0
                || (request != null && request.remote != null)) return false;
        ActivityThread thread = ActivityThread.currentActivityThread();
        if (thread == null) return false;
        List<IBinder> live = new ArrayList<>();
        for (IBinder token : ActivityClientControllerAdapter.taskTokens()) {
            Activity activity = thread.getActivity(token);
            if (activity != null && !activity.isFinishing() && !activity.isDestroyed()) live.add(token);
        }
        boolean clearTop = (flags & Intent.FLAG_ACTIVITY_CLEAR_TOP) != 0;
        int target = -1;
        for (int i = live.size() - 1; i >= 0; i--) {
            if (component.equals(thread.getActivity(live.get(i)).getComponentName())) {
                target = i;
                break;
            }
            if (!clearTop) break;
        }
        if (target < 0) return false;
        // Do not replace OH ability management when any affected activity has a real OH token.
        for (int i = target; i < live.size(); i++) {
            Long oh = adapter.core.OhTokenRegistry.findOhToken(live.get(i));
            if (oh != null && oh != 0L) return false;
        }
        IBinder targetToken = live.get(target);
        for (int i = live.size() - 1; i > target; i--) {
            IBinder token = live.get(i);
            ActivityClientControllerAdapter.getInstance().finishActivity(
                    token, Activity.RESULT_CANCELED, null, 0);
            // Finish the local transaction before delivering the new intent. The ordinary
            // queued destroy sees the removed record and does nothing.
            if (thread.getActivity(token) != null) {
                ClientTransaction destroy = ClientTransaction.obtain(thread.getApplicationThread());
                destroy.addTransactionItem(DestroyActivityItem.obtain(token, true));
                thread.executeTransaction(destroy);
            }
        }
        // Reusing an activity does not replace the result destination of its original launch.
        // A new start-for-result receives cancellation, as with Android's delivered-to-top path.
        if (request != null) {
            ActivityResults.sendNow(request.resultTo, request.resultWho, request.requestCode,
                    Activity.RESULT_CANCELED, null);
        }
        ClientTransaction transaction = ClientTransaction.obtain(thread.getApplicationThread());
        transaction.addTransactionItem(NewIntentItem.obtain(targetToken,
                Collections.singletonList(new ReferrerIntent(new Intent(intent), own)), true));
        thread.executeTransaction(transaction);
        // No URI, extras, account data or redirect token in the diagnostic.
        System.err.println("[WL-LOCAL-REUSE] class=" + component.getClassName()
                + " cleared=" + (live.size() - target - 1));
        return true;
    }
}

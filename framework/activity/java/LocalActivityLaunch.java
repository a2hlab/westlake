package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.servertransaction.ClientTransaction;
import android.app.servertransaction.DestroyActivityItem;
import android.app.servertransaction.NewIntentItem;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.IBinder;
import android.os.Looper;
import com.android.internal.content.ReferrerIntent;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Intent reuse for the direct-launch task whose activities have no individual OH ability. */
final class LocalActivityLaunch {
    private LocalActivityLaunch() {}

    /** NEW_TASK | CLEAR_TASK replaces the task selected for the new activity. The
     * direct source-APK host can identify its own same-affinity task on the main thread. */
    static void clearTaskIfSelected(ActivityThread thread, Intent intent) {
        int required = Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK;
        int separateTask = Intent.FLAG_ACTIVITY_MULTIPLE_TASK | Intent.FLAG_ACTIVITY_NEW_DOCUMENT;
        int flags = intent.getFlags();
        if ((flags & required) != required
                || (flags & separateTask) != 0
                || Looper.myLooper() != Looper.getMainLooper()) return;
        ComponentName target = intent.getComponent();
        String own = ActivityThread.currentPackageName();
        if (target == null || !target.getPackageName().equals(own)) return;
        List<IBinder> tokens = ActivityClientControllerAdapter.taskTokens();
        if (tokens.isEmpty()) return;
        PackageManager pm = ActivityThread.currentApplication().getPackageManager();
        try {
            ActivityInfo next = pm.getActivityInfo(target, 0);
            if (!next.enabled || !next.applicationInfo.enabled || next.taskAffinity == null
                    || next.documentLaunchMode != ActivityInfo.DOCUMENT_LAUNCH_NONE
                    || next.launchMode > ActivityInfo.LAUNCH_SINGLE_TOP) return;
            for (IBinder token : tokens) {
                Activity current = thread.getActivity(token);
                Long oh = adapter.core.OhTokenRegistry.findOhToken(token);
                ActivityResults.Request origin = ActivityResults.origin(token);
                if (current == null || !own.equals(current.getPackageName())
                        || (oh != null && oh != 0L) || (origin != null && origin.remote != null)) return;
                // The direct host has no separate task identifier for a document or an
                // explicitly separate task that an earlier launch added to this list.
                if (current.getIntent() == null || (current.getIntent().getFlags() & separateTask) != 0) return;
                ActivityInfo info = pm.getActivityInfo(current.getComponentName(), 0);
                if (!next.taskAffinity.equals(info.taskAffinity)
                        || info.documentLaunchMode != ActivityInfo.DOCUMENT_LAUNCH_NONE
                        || info.launchMode > ActivityInfo.LAUNCH_SINGLE_TOP) return;
            }
        } catch (PackageManager.NameNotFoundException unknown) {
            return;
        }
        finishFrom(thread, tokens, 0);
        System.err.println("[WL-LOCAL-TASK] cleared=" + tokens.size() + " for=" + target.getClassName());
    }

    static boolean reuse(Intent intent, ActivityResults.Request request) {
        if (intent == null || Looper.myLooper() != Looper.getMainLooper()) return false;
        ComponentName component = intent.getComponent();
        String own = ActivityThread.currentPackageName();
        if (component == null || own == null || !own.equals(component.getPackageName())) return false;
        int flags = intent.getFlags();
        // The manifest launch mode reuses as Android does. singleTop acts as SINGLE_TOP. singleTask and
        // singleInstance act as CLEAR_TOP | SINGLE_TOP on the activity's existing instance, which a
        // NEW_TASK start reaches too (Android adds NEW_TASK to every singleTask start). Uber's sign-in
        // depends on it: its link activity hands Google's redirect to the singleTask sign-in activity
        // already open, which holds the sign-in's state; a second instance starts the sign-in over.
        int mode = launchMode(component);
        boolean taskMode = mode == ActivityInfo.LAUNCH_SINGLE_TASK || mode == ActivityInfo.LAUNCH_SINGLE_INSTANCE;
        boolean singleTop = (flags & Intent.FLAG_ACTIVITY_SINGLE_TOP) != 0 || mode == ActivityInfo.LAUNCH_SINGLE_TOP;
        int crossTask = Intent.FLAG_ACTIVITY_MULTIPLE_TASK | Intent.FLAG_ACTIVITY_NEW_DOCUMENT
                | Intent.FLAG_ACTIVITY_CLEAR_TASK | (taskMode ? 0 : Intent.FLAG_ACTIVITY_NEW_TASK);
        // Other cross-task starts still belong to the ability/task launch path.
        if (!(singleTop || taskMode) || (flags & crossTask) != 0
                || (request != null && request.remote != null)) return false;
        ActivityThread thread = ActivityThread.currentActivityThread();
        if (thread == null) return false;
        List<IBinder> live = new ArrayList<>();
        for (IBinder token : ActivityClientControllerAdapter.taskTokens()) {
            Activity activity = thread.getActivity(token);
            if (activity != null && !activity.isFinishing() && !activity.isDestroyed()) live.add(token);
        }
        boolean clearTop = taskMode || (flags & Intent.FLAG_ACTIVITY_CLEAR_TOP) != 0;
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
        finishFrom(thread, live, target + 1);
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
                + " cleared=" + (live.size() - target - 1) + " launchMode=" + mode);
        return true;
    }

    /** The activity's manifest launch mode; LAUNCH_MULTIPLE (standard) when the package manager has none. */
    private static int launchMode(ComponentName component) {
        try {
            return ActivityThread.currentApplication().getPackageManager()
                    .getActivityInfo(component, 0).launchMode;
        } catch (Throwable unknown) {
            return ActivityInfo.LAUNCH_MULTIPLE;
        }
    }

    private static void finishFrom(ActivityThread thread, List<IBinder> tokens, int first) {
        for (int i = tokens.size() - 1; i >= first; i--) {
            IBinder token = tokens.get(i);
            ActivityClientControllerAdapter.getInstance().finishActivity(
                    token, Activity.RESULT_CANCELED, null, 0);
            // Complete the destruction before the next launch or onNewIntent.
            // The queued destroy sees the removed activity record.
            if (thread.getActivity(token) != null) {
                ClientTransaction destroy = ClientTransaction.obtain(thread.getApplicationThread());
                destroy.addTransactionItem(DestroyActivityItem.obtain(token, true));
                thread.executeTransaction(destroy);
            }
        }
    }
}

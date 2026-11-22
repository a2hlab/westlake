package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.IApplicationThread;
import android.app.servertransaction.ClientTransaction;
import android.app.servertransaction.NewIntentItem;
import android.app.servertransaction.ResumeActivityItem;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import com.android.internal.content.ReferrerIntent;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Launch modes and task flags for a start of one of the app's own activities in its process (an in-process start
 * of the direct launch, or of the managed launch's task). On Android the system server's ActivityStarter decides
 * whether a start makes a new instance; these launches have no system server, so every start made one.
 *
 * offerToday's MainActivity is singleTask and starts itself with an extra: on Android that start reaches the
 * running instance as onNewIntent, and the app moves on. Here each start created another MainActivity, whose
 * onCreate started it again: 7,041 instances in one D600 run, and no frame.
 *
 * The decision is ActivityStarter's, within the app's one task (ActivityClientControllerAdapter.taskTokens):
 *   - singleTask, singleInstance and singleInstancePerTask: an instance in the task is reused. The activities
 *     above it finish, and it gets the intent as onNewIntent.
 *   - singleTop, or FLAG_ACTIVITY_SINGLE_TOP: the task's top activity is reused only if it is the same activity.
 *   - FLAG_ACTIVITY_CLEAR_TOP: the activities above an instance finish. The instance gets onNewIntent if it is
 *     single-top by mode or flag; otherwise it finishes too, and the start makes a new one.
 *   - FLAG_ACTIVITY_NEW_TASK with FLAG_ACTIVITY_CLEAR_TASK: the task's activities finish, and the start makes a
 *     new one.
 * A start for a result is not reused: Android cancels such a result at once for a reused activity, and the
 * launch keeps its own result path.
 *
 * What a reused instance gets is posted to the main thread, so it runs after the callback that started it (an
 * in-process start runs inside startActivity, and offerToday's starts come from onCreate). Finishing goes through
 * Activity.finish, which destroys an in-process activity as ATMS would.
 */
final class LaunchModes {
    private LaunchModes() {}

    private static final String TAG = "[WL-LAUNCH-MODE] ";

    /**
     * Whether this start reuses an instance already in the task (the start then makes none: the instance gets the
     * intent). Finishes the activities the start's mode or flags clear either way.
     */
    static boolean reuse(IApplicationThread thread, String packageName, ActivityInfo info, Intent intent,
            String referrer) {
        try {
            int mode = declaredLaunchMode(packageName, info);
            int flags = intent.getFlags();
            String target = info.targetActivity != null ? info.targetActivity : info.name;
            List<IBinder> task = liveTask();
            if (task.isEmpty()) return false;

            if ((flags & Intent.FLAG_ACTIVITY_NEW_TASK) != 0 && (flags & Intent.FLAG_ACTIVITY_CLEAR_TASK) != 0) {
                System.err.println(TAG + target + ": NEW_TASK|CLEAR_TASK, the task's " + task.size()
                        + " activities finish");
                finishLater(task);
                return false;
            }

            boolean singleTask = mode == ActivityInfo.LAUNCH_SINGLE_TASK
                    || mode == ActivityInfo.LAUNCH_SINGLE_INSTANCE
                    || mode == ActivityInfo.LAUNCH_SINGLE_INSTANCE_PER_TASK;
            boolean singleTop = mode == ActivityInfo.LAUNCH_SINGLE_TOP
                    || (flags & Intent.FLAG_ACTIVITY_SINGLE_TOP) != 0;
            boolean clearTop = (flags & Intent.FLAG_ACTIVITY_CLEAR_TOP) != 0;
            if (!singleTask && !singleTop && !clearTop) return false;

            int index = -1;
            for (int i = task.size() - 1; i >= 0; i--) {
                Activity activity = activityOf(task.get(i));
                if (activity != null && activity.getClass().getName().equals(target)) {
                    index = i;
                    break;
                }
            }
            if (index < 0) return false;
            boolean top = index == task.size() - 1;
            List<IBinder> above = new ArrayList<>(task.subList(index + 1, task.size()));
            IBinder instance = task.get(index);

            if (singleTask || clearTop) {
                if (!singleTask && !singleTop) {
                    // CLEAR_TOP on a standard activity: the instance goes as well, and the start makes a new one.
                    above.add(instance);
                    System.err.println(TAG + target + ": CLEAR_TOP, " + above.size()
                            + " activities finish, a new instance starts");
                    finishLater(above);
                    return false;
                }
                System.err.println(TAG + target + " (" + modeName(mode) + ", flags 0x" + Integer.toHexString(flags)
                        + "): the instance in the task gets the intent; " + above.size() + " above it finish");
                finishLater(above);
                deliverLater(thread, instance, intent, referrer, !top);
                return true;
            }
            if (top) {
                System.err.println(TAG + target + " (" + modeName(mode) + ", flags 0x" + Integer.toHexString(flags)
                        + "): the task's top gets the intent");
                deliverLater(thread, instance, intent, referrer, false);
                return true;
            }
            return false;
        } catch (Throwable t) {
            System.err.println(TAG + "decision failed, the start makes a new instance: " + t);
            return false;
        }
    }

    /** The launch mode the manifest declares for the activity (an alias's own, as Android reads it). */
    private static int declaredLaunchMode(String packageName, ActivityInfo info) {
        try {
            if (packageName != null && adapter.packagemanager.SourcePackageRegistry.contains(packageName)) {
                PackageInfo pkg = adapter.packagemanager.SourcePackageRegistry.packageInfo(
                        packageName, PackageManager.GET_ACTIVITIES);
                if (pkg != null && pkg.activities != null) {
                    for (ActivityInfo declared : pkg.activities) {
                        if (declared.name.equals(info.name)) return declared.launchMode;
                    }
                }
            }
        } catch (Throwable ignored) {
            // The record's own mode then.
        }
        return info.launchMode;
    }

    /** The task's activities that are still alive and not finishing, root first. */
    private static List<IBinder> liveTask() {
        List<IBinder> live = new ArrayList<>();
        for (IBinder token : ActivityClientControllerAdapter.taskTokens()) {
            Activity activity = activityOf(token);
            if (activity != null && !activity.isFinishing() && !activity.isDestroyed()) live.add(token);
        }
        return live;
    }

    private static Activity activityOf(IBinder token) {
        ActivityThread thread = ActivityThread.currentActivityThread();
        return thread != null ? thread.getActivity(token) : null;
    }

    private static void finishLater(List<IBinder> tokens) {
        if (tokens.isEmpty()) return;
        final List<IBinder> finishing = new ArrayList<>(tokens);
        Collections.reverse(finishing);  // top first, as a cleared task unwinds
        new Handler(Looper.getMainLooper()).post(() -> {
            for (IBinder token : finishing) {
                Activity activity = activityOf(token);
                if (activity != null && !activity.isFinishing()) activity.finish();
            }
        });
    }

    /**
     * onNewIntent for the reused instance, from a transaction as ATMS sends it (NewIntentItem): ActivityThread
     * pauses a resumed activity around the delivery and resumes it after. One under activities that are now
     * finishing is resumed too, as the task's new top.
     */
    private static void deliverLater(IApplicationThread thread, IBinder token, Intent intent, String referrer,
            boolean resumeAfter) {
        final Intent delivered = new Intent(intent);
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                ActivityThread activityThread = ActivityThread.currentActivityThread();
                Activity activity = activityOf(token);
                if (activity == null || activity.isFinishing() || activity.isDestroyed()) {
                    System.err.println(TAG + "the instance went before the intent reached it");
                    return;
                }
                boolean resumed = activity.isResumed();
                ClientTransaction transaction = ClientTransaction.obtain(thread);
                List<ReferrerIntent> intents = new ArrayList<>(1);
                intents.add(new ReferrerIntent(delivered, referrer));
                transaction.addTransactionItem(NewIntentItem.obtain(token, intents, resumed));
                if (!resumed && resumeAfter) transaction.addTransactionItem(ResumeActivityItem.obtain(token, true, false));
                activityThread.executeTransaction(transaction);
                System.err.println(TAG + activity.getClass().getName() + " got onNewIntent");
            } catch (Throwable t) {
                System.err.println(TAG + "new intent not delivered: " + t);
            }
        });
    }

    private static String modeName(int mode) {
        switch (mode) {
            case ActivityInfo.LAUNCH_SINGLE_TOP: return "singleTop";
            case ActivityInfo.LAUNCH_SINGLE_TASK: return "singleTask";
            case ActivityInfo.LAUNCH_SINGLE_INSTANCE: return "singleInstance";
            case ActivityInfo.LAUNCH_SINGLE_INSTANCE_PER_TASK: return "singleInstancePerTask";
            default: return "standard";
        }
    }
}

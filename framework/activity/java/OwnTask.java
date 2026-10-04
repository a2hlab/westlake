package adapter.activity;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityThread;
import android.app.IAppTask;
import android.app.IApplicationThread;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.SystemClock;

import java.util.ArrayList;
import java.util.List;

/**
 * The app's own task, as the task queries report it to the app itself.
 *
 * Since Android 5 ActivityManager.getRunningTasks and getAppTasks show an app only its own tasks,
 * so the answer needs no OpenHarmony mission list: the one task the activity client controller
 * keeps (ActivityClientControllerAdapter). Both answered empty lists, and code that reads the first
 * entry (getRunningTasks(1).get(0).topActivity, the classic "is my app in front" check, or
 * getAppTasks().get(0)) threw IndexOutOfBoundsException.
 */
public final class OwnTask {
    private OwnTask() {}

    private static <T extends android.app.TaskInfo> T fill(T info) {
        List<IBinder> tokens = ActivityClientControllerAdapter.taskTokens();
        if (tokens.isEmpty()) return null;
        ActivityThread thread = ActivityThread.currentActivityThread();
        Activity base = thread != null ? thread.getActivity(tokens.get(0)) : null;
        Activity top = thread != null ? thread.getActivity(tokens.get(tokens.size() - 1)) : null;
        info.taskId = ActivityClientControllerAdapter.taskId();
        info.isRunning = true;
        info.numActivities = tokens.size();
        info.lastActiveTime = SystemClock.elapsedRealtime();
        if (base != null) {
            info.baseIntent = base.getIntent();
            info.baseActivity = base.getComponentName();
            info.realActivity = base.getComponentName();
        }
        if (top != null) info.topActivity = top.getComponentName();
        info.isFocused = true;
        info.isVisible = true;
        return info;
    }

    /** getTasks: the app's task, or none when it has no live activity. */
    @SuppressWarnings("deprecation")
    public static List<ActivityManager.RunningTaskInfo> runningTasks(int maxNum) {
        List<ActivityManager.RunningTaskInfo> out = new ArrayList<>(1);
        ActivityManager.RunningTaskInfo info = fill(new ActivityManager.RunningTaskInfo());
        if (info != null && maxNum > 0) {
            info.id = info.taskId;
            info.numRunning = info.numActivities;
            out.add(info);
        }
        return out;
    }

    /** RecentTaskInfo for the app's task, or null when it has no live activity. */
    @SuppressWarnings("deprecation")
    public static ActivityManager.RecentTaskInfo recentTaskInfo() {
        ActivityManager.RecentTaskInfo info = fill(new ActivityManager.RecentTaskInfo());
        if (info != null) {
            info.id = info.taskId;
            info.persistentId = info.taskId;
        }
        return info;
    }

    /** getAppTasks: the app's task as an AppTask, or none when it has no live activity. */
    public static List<IBinder> appTasks() {
        List<IBinder> out = new ArrayList<>(1);
        if (!ActivityClientControllerAdapter.taskTokens().isEmpty()) out.add(new AppTask().asBinder());
        return out;
    }

    /** The AppTask's binder, in process: what ActivityManager.AppTask calls. */
    private static final class AppTask extends IAppTask.Stub {
        @Override
        public void finishAndRemoveTask() {
            ActivityThread thread = ActivityThread.currentActivityThread();
            List<IBinder> tokens = ActivityClientControllerAdapter.taskTokens();
            for (int i = tokens.size() - 1; i >= 0 && thread != null; i--) {
                Activity activity = thread.getActivity(tokens.get(i));
                if (activity != null && !activity.isFinishing()) activity.finish();
            }
        }

        @Override
        public ActivityManager.RecentTaskInfo getTaskInfo() {
            ActivityManager.RecentTaskInfo info = recentTaskInfo();
            if (info == null) throw new IllegalArgumentException("Unable to find task ID " + ActivityClientControllerAdapter.taskId());
            return info;
        }

        @Override
        public void moveToFront(IApplicationThread appThread, String callingPackage) {
            // The app's task is the one on screen; there is nothing to bring forward.
        }

        @Override
        public int startActivity(IBinder whoThread, String callingPackage, String callingFeatureId,
                Intent intent, String resolvedType, Bundle options) {
            try {
                return ActivityTaskManagerAdapter.getInstance().startActivity(null, callingPackage,
                        callingFeatureId, intent, resolvedType, null, null, 0, 0, null, options);
            } catch (android.os.RemoteException e) {
                return -96;   // ActivityManager.START_CANCELED
            }
        }

        @Override
        public void setExcludeFromRecents(boolean exclude) {
            // No recents list is kept for the app here.
        }
    }
}

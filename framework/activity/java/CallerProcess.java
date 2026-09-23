package adapter.activity;

import android.app.ActivityManager;
import android.app.ActivityThread;
import android.os.Debug;
import android.os.Process;

import java.util.ArrayList;
import java.util.List;

/**
 * The process-table answers an ordinary application gets from ActivityManagerService.
 *
 * Since Android 5.1 an app sees only its own processes and services in these queries, so a
 * faithful answer needs no OpenHarmony process table: the caller's own process. Both the
 * direct-launch IActivityManager stub and ActivityManagerAdapter answer from here. null is not
 * one of Android's answers, and SDKs iterate the result unchecked (Kochava's AppUtil walks both
 * getRunningAppProcesses() and getRunningServices() without a null check).
 */
public final class CallerProcess {
    private CallerProcess() {}

    /** getRunningAppProcesses(): the caller, in the foreground, hosting its own package. */
    public static List<ActivityManager.RunningAppProcessInfo> runningAppProcesses() {
        String packageName = ActivityThread.currentPackageName();
        String processName = ActivityThread.currentProcessName();
        if (processName == null) processName = packageName != null ? packageName : "";
        ActivityManager.RunningAppProcessInfo info = new ActivityManager.RunningAppProcessInfo(
                processName, Process.myPid(),
                packageName != null ? new String[] { packageName } : new String[0]);
        info.uid = Process.myUid();
        // The process has just been started to show its launcher activity: AMS ranks it top.
        info.importance = ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
        List<ActivityManager.RunningAppProcessInfo> list = new ArrayList<>(1);
        list.add(info);
        return list;
    }

    /** getRunningServices(): the caller's own running services; none are tracked here. */
    public static List<ActivityManager.RunningServiceInfo> runningServices() {
        return new ArrayList<>(0);
    }

    /**
     * getProcessMemoryInfo(pids): one entry per requested pid, as AMS returns. The caller's own
     * entry is measured (Debug.getMemoryInfo is registered by class_library_services); an app
     * gets nothing useful about other processes on Android either, so theirs stay zero.
     */
    public static Debug.MemoryInfo[] processMemoryInfo(int[] pids) {
        int count = pids != null ? pids.length : 0;
        Debug.MemoryInfo[] out = new Debug.MemoryInfo[count];
        for (int i = 0; i < count; i++) {
            out[i] = new Debug.MemoryInfo();
            if (pids[i] == Process.myPid()) {
                try {
                    Debug.getMemoryInfo(out[i]);
                } catch (Throwable ignored) {
                    // an unregistered native leaves the entry zero, not the caller without one
                }
            }
        }
        return out;
    }
}

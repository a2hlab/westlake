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

    // ProcessList's OOM levels (KB) on a 64-bit device of 1 GB or more with a large screen, where
    // the scale is 1: mOomMinFreeHigh with the two cached slots raised by 3/2 and 7/4. Index by
    // the first adjustment at or above the one asked for: FOREGROUND 0, VISIBLE 100, PERCEPTIBLE
    // 200, PERCEPTIBLE_LOW 250, CACHED_APP_MIN 900, CACHED_APP_LMK_FIRST 950.
    private static final int[] OOM_ADJ = { 0, 100, 200, 250, 900, 950 };
    private static final long[] OOM_MIN_FREE_KB = { 73728, 92160, 110592, 129024, 221184, 322560 };

    private static long memLevel(int adjustment) {
        for (int i = 0; i < OOM_ADJ.length; i++) {
            if (adjustment <= OOM_ADJ[i]) return OOM_MIN_FREE_KB[i] * 1024;
        }
        return OOM_MIN_FREE_KB[OOM_MIN_FREE_KB.length - 1] * 1024;
    }

    /**
     * getMemoryInfo(outInfo): what ProcessList.getMemoryInfo reports, from /proc/meminfo. The
     * proxy's default left every field 0 -- totalMem and availMem included -- and 96 corpus apps
     * read it (cache sizing, low-memory checks that divide by totalMem).
     */
    public static void memoryInfo(ActivityManager.MemoryInfo out) {
        if (out == null) return;
        long total = 0, free = 0, cached = 0;
        try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader("/proc/meminfo"))) {
            for (String line; (line = reader.readLine()) != null; ) {
                String[] parts = line.trim().split("\\s+");
                if (parts.length < 2) continue;
                long kb = Long.parseLong(parts[1]);
                if ("MemTotal:".equals(parts[0])) total = kb;
                else if ("MemFree:".equals(parts[0])) free = kb;
                else if ("Cached:".equals(parts[0])) cached = kb;
            }
        } catch (Throwable ignored) {
            // unreadable: the fields stay as AMS would never leave them, but no worse than before
        }
        final long homeAppMem = memLevel(600);      // HOME_APP_ADJ
        final long cachedAppMem = memLevel(900);    // CACHED_APP_MIN_ADJ
        out.totalMem = total * 1024;
        out.advertisedMem = out.totalMem;
        out.availMem = (free + cached) * 1024;      // Process.getFreeMemory: MemFree + Cached
        out.threshold = homeAppMem;
        out.lowMemory = out.availMem < (homeAppMem + ((cachedAppMem - homeAppMem) / 2));
        out.hiddenAppThreshold = cachedAppMem;
        out.secondaryServerThreshold = memLevel(500);   // SERVICE_ADJ
        out.visibleAppThreshold = memLevel(100);        // VISIBLE_APP_ADJ
        out.foregroundAppThreshold = memLevel(0);       // FOREGROUND_APP_ADJ
    }
}

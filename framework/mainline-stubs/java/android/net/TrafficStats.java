// Mainline APEX stub.  android.net.TrafficStats (Connectivity APEX).
// Covers exactly the 5 method signatures referenced by framework.jar bytecode
// so ART pre-execution invoke check resolves without preallocated NCDFE.
// AOSP real source at packages/modules/Connectivity/framework-t/src/android/net/TrafficStats.java
// (1148 lines, transitive mainline deps; tech debt per memory:project_b19_tech_debt_postponed).

package android.net;

import android.content.Context;

import java.net.Socket;
import java.net.SocketException;

public class TrafficStats {
    /** Android's sentinel for counters unavailable on the current platform. */
    public static final long UNSUPPORTED = -1L;

    private static final ThreadLocal<Integer> THREAD_TAG =
            ThreadLocal.withInitial(() -> -1);
    private static final ThreadLocal<Integer> THREAD_UID =
            ThreadLocal.withInitial(() -> -1);

    public static void attachSocketTagger() { /* no-op */ }
    public static int getAndSetThreadStatsTag(int tag) {
        int oldTag = THREAD_TAG.get();
        THREAD_TAG.set(tag);
        return oldTag;
    }
    public static void init(Context context) { /* no-op */ }
    public static void setThreadStatsTag(int tag) { THREAD_TAG.set(tag); }
    public static int getThreadStatsTag() { return THREAD_TAG.get(); }
    public static void clearThreadStatsTag() { THREAD_TAG.set(-1); }
    public static void setThreadStatsUid(int uid) { THREAD_UID.set(uid); }
    public static int getThreadStatsUid() { return THREAD_UID.get(); }
    public static void clearThreadStatsUid() { THREAD_UID.set(-1); }
    public static void setThreadStatsTagApp() { setThreadStatsTag(0xFFFFFF05); }

    public static void tagSocket(Socket socket) throws SocketException { /* no-op */ }
    public static void untagSocket(Socket socket) throws SocketException { /* no-op */ }

    /**
     * Return total received bytes, or {@link #UNSUPPORTED} when the Android
     * network-stats service is not available at the OpenHarmony boundary.
     *
     * Returning Android's documented sentinel preserves the API contract and
     * lets callers select their normal fallback instead of losing the worker
     * thread to NoSuchMethodError.
     */
    public static long getTotalRxBytes() { return UNSUPPORTED; }
    public static long getTotalTxBytes() { return UNSUPPORTED; }
    public static long getMobileRxBytes() { return UNSUPPORTED; }
    public static long getMobileTxBytes() { return UNSUPPORTED; }
    public static long getUidRxBytes(int uid) { return UNSUPPORTED; }
    public static long getUidTxBytes(int uid) { return UNSUPPORTED; }
}

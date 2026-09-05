package android.app.usage;

import android.content.Context;

/**
 * Android API surface for network usage queries.
 *
 * OpenHarmony does not currently expose an Android INetworkStatsService.  An
 * empty result is therefore the faithful boundary result for query methods;
 * importantly, the platform type remains linkable for feature detection and
 * callers can continue on their normal no-data path.
 */
public class NetworkStatsManager {
    public NetworkStatsManager(Context context) {}

    public NetworkStats.Bucket querySummaryForDevice(int networkType, String subscriberId,
            long startTime, long endTime) {
        return new NetworkStats.Bucket();
    }

    public NetworkStats querySummary(int networkType, String subscriberId,
            long startTime, long endTime) {
        return new NetworkStats();
    }

    public NetworkStats queryDetailsForUid(int networkType, String subscriberId,
            long startTime, long endTime, int uid) {
        return new NetworkStats();
    }

    public NetworkStats queryDetails(int networkType, String subscriberId,
            long startTime, long endTime) {
        return new NetworkStats();
    }
}

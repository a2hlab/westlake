package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import java.util.concurrent.ConcurrentHashMap;

/**
 * Process-local cache for ApplicationInfo values returned by the OH package boundary.
 *
 * Android's PackageManagerService keeps parsed package state resident and sends callers
 * parcelled value objects.  Re-parsing the same BMS JSON for every IPackageManager call is both
 * unlike PMS semantics and particularly expensive for WebView Safe Browsing, which performs a
 * burst of identical lookups on the UI thread.  Store one private snapshot per query shape and
 * return a fresh copy so callers cannot mutate the cached package state.
 */
final class ApplicationInfoCache {
    private static final ConcurrentHashMap<String, ApplicationInfo> CACHE =
            new ConcurrentHashMap<>();

    private ApplicationInfoCache() {}

    private static String key(String packageName, long flags, int userId) {
        return packageName + '\u0000' + flags + '\u0000' + userId;
    }

    private static ApplicationInfo copy(ApplicationInfo source) {
        ApplicationInfo result = new ApplicationInfo(source);
        if (source.metaData != null) {
            result.metaData = new Bundle(source.metaData);
        }
        return result;
    }

    static ApplicationInfo get(String packageName, long flags, int userId) {
        ApplicationInfo cached = CACHE.get(key(packageName, flags, userId));
        return cached != null ? copy(cached) : null;
    }

    static ApplicationInfo putAndCopy(
            String packageName, long flags, int userId, ApplicationInfo value) {
        if (value == null) return null;
        String key = key(packageName, flags, userId);
        ApplicationInfo snapshot = copy(value);
        ApplicationInfo existing = CACHE.putIfAbsent(key, snapshot);
        return copy(existing != null ? existing : snapshot);
    }
}

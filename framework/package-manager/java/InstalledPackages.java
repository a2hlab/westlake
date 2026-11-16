package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageParser;
import android.content.pm.pkg.FrameworkPackageUserState;
import android.os.Parcel;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Packages a PackageInstaller session published (ADR-0006, second step).
 *
 * Each package has a directory under DIR laid out as Android lays out /data/app/<package>: base.apk, its
 * split_<name>.apk files, base.dm when the installer sent dex metadata, and record.properties, which commit writes
 * last (installer, version, signer, time). Every Westlake process reads the same directory, so what one installer
 * publishes, every app's PackageManager answers for, with the uid the app gets when it is launched. All Westlake
 * processes share one OS uid, so nothing yet keeps another app from changing this directory; ADR-0006 moves it
 * behind a trusted service.
 */
public final class InstalledPackages {
    /** In the spawner logs' directory, which every Westlake process can already reach and write. */
    static final File DIR = new File("/data/service/el1/public/appspawnx/installed");
    static final String RECORD = "record.properties";
    private static final Map<String, Entry> CACHE = new ConcurrentHashMap<>();

    private static final class Entry {
        final long stamp;
        final PackageParser.Package parsed;
        final Properties record;

        Entry(long stamp, PackageParser.Package parsed, Properties record) {
            this.stamp = stamp;
            this.parsed = parsed;
            this.record = record;
        }
    }

    private InstalledPackages() {}

    public static boolean contains(String packageName) {
        return entry(packageName) != null;
    }

    /** The installed packages' names. */
    public static List<String> names() {
        List<String> names = new ArrayList<>();
        String[] list = DIR.list();
        if (list == null) return names;
        for (String name : list) {
            if (!name.startsWith(".") && entry(name) != null) names.add(name);
        }
        return names;
    }

    /** The installed record's value, or null. */
    public static String recorded(String packageName, String key) {
        Entry entry = entry(packageName);
        return entry == null ? null : entry.record.getProperty(key);
    }

    private static Entry entry(String packageName) {
        if (packageName == null || packageName.isEmpty() || packageName.contains("/") || packageName.startsWith(".")) {
            return null;
        }
        File dir = new File(DIR, packageName);
        File record = new File(dir, RECORD);
        if (!record.isFile()) {
            CACHE.remove(packageName);
            return null;
        }
        long stamp = record.lastModified();
        Entry cached = CACHE.get(packageName);
        if (cached != null && cached.stamp == stamp) return cached;
        try {
            Properties values = new Properties();
            try (InputStream input = new FileInputStream(record)) {
                values.load(input);
            }
            PackageParser.Package parsed = new PackageParser().parsePackage(dir, 0);
            if (!packageName.equals(parsed.packageName)) return null;
            Entry entry = new Entry(stamp, parsed, values);
            CACHE.put(packageName, entry);
            return entry;
        } catch (IOException | PackageParser.PackageParserException | RuntimeException e) {
            System.err.println("[WL-INSTALL] installed " + packageName + " unreadable: " + e);
            return null;
        }
    }

    public static PackageInfo packageInfo(String packageName, long flags) {
        Entry entry = entry(packageName);
        if (entry == null) return null;
        if ((flags & (PackageManager.MATCH_DIRECT_BOOT_AWARE | PackageManager.MATCH_DIRECT_BOOT_UNAWARE)) == 0) {
            flags |= PackageManager.MATCH_DIRECT_BOOT_AWARE | PackageManager.MATCH_DIRECT_BOOT_UNAWARE;
        }
        synchronized (entry) {
            if ((flags & (PackageManager.GET_SIGNATURES | PackageManager.GET_SIGNING_CERTIFICATES)) != 0
                    && entry.parsed.mSigningDetails == PackageParser.SigningDetails.UNKNOWN) {
                try {
                    PackageParser.collectCertificates(entry.parsed, false);
                } catch (PackageParser.PackageParserException e) {
                    throw new SecurityException("Installed APK certificate verification failed", e);
                }
            }
        }
        long installed = parseLong(entry.record.getProperty("firstInstallTime"));
        long updated = parseLong(entry.record.getProperty("lastUpdateTime"));
        PackageInfo value = PackageParser.generatePackageInfo(entry.parsed, null, (int) flags, installed, updated,
                new java.util.HashSet<String>(entry.parsed.requestedPermissions), FrameworkPackageUserState.DEFAULT, 0);
        if (value == null) return null;
        Parcel parcel = Parcel.obtain();   // a copy of its own for the caller
        try {
            value.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            value = PackageInfo.CREATOR.createFromParcel(parcel);
        } finally {
            parcel.recycle();
        }
        if (value.applicationInfo != null) applyLocation(value.applicationInfo, entry);
        return value;
    }

    public static ApplicationInfo applicationInfo(String packageName, long flags) {
        PackageInfo info = packageInfo(packageName, flags);
        return info == null ? null : info.applicationInfo;
    }

    private static void applyLocation(ApplicationInfo info, Entry entry) {
        File dir = new File(DIR, entry.parsed.packageName);
        info.sourceDir = info.publicSourceDir = new File(dir, "base.apk").getPath();
        if (entry.parsed.splitNames != null) {
            String[] splits = new String[entry.parsed.splitNames.length];
            for (int i = 0; i < splits.length; i++) {
                splits[i] = new File(dir, "split_" + entry.parsed.splitNames[i] + ".apk").getPath();
            }
            info.splitSourceDirs = info.splitPublicSourceDirs = splits;
        }
        info.uid = RemotePackages.installedUid(entry.parsed.packageName);
        info.flags |= ApplicationInfo.FLAG_INSTALLED;
        info.flags &= ~ApplicationInfo.FLAG_SYSTEM;
    }

    private static long parseLong(String value) {
        try {
            return value == null ? 0 : Long.parseLong(value);
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}

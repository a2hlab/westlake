package adapter.packagemanager;

import android.content.pm.ApplicationInfo;
import android.content.pm.ComponentInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageParser;
import android.content.pm.pkg.FrameworkPackageUserState;
import android.os.Parcel;
import android.os.Process;
import java.io.File;
import java.util.HashSet;
import java.util.Set;

/** Parsed original APK state for an explicitly configured source-runtime process. */
public final class SourcePackageRegistry {
    private static PackageParser.Package parsed;
    private static ApplicationInfo location;
    private static boolean certificatesCollected;

    private SourcePackageRegistry() {}

    public static synchronized ApplicationInfo register(ApplicationInfo runtime)
            throws PackageParser.PackageParserException {
        if (parsed != null) throw new IllegalStateException("Source package already registered");
        if (runtime.uid != Process.myUid()) throw new SecurityException("Source package UID differs");
        PackageParser.Package candidate = new PackageParser().parsePackage(new File(runtime.sourceDir), 0);
        if (!runtime.packageName.equals(candidate.packageName)) {
            throw new SecurityException("Original APK package differs from launch request");
        }
        location = new ApplicationInfo(runtime);
        parsed = candidate;
        applyLocation(parsed.applicationInfo);
        ApplicationInfo result = packageInfo(runtime.packageName, PackageManager.GET_META_DATA).applicationInfo;
        System.err.println("[SOURCE-PACKAGE] parsed " + result.packageName + " uid=" + result.uid
                + " version=" + parsed.mVersionCode + " targetSdk=" + result.targetSdkVersion
                + " nativeLibraryDir=" + result.nativeLibraryDir);
        return result;
    }

    public static synchronized boolean contains(String packageName) {
        return parsed != null && parsed.packageName.equals(packageName);
    }

    public static synchronized String packageForUid(int uid) {
        return parsed != null && uid == location.uid ? parsed.packageName : null;
    }

    private static void applyLocation(ApplicationInfo info) {
        info.uid = location.uid;
        info.sourceDir = location.sourceDir;
        info.publicSourceDir = location.publicSourceDir;
        info.scanSourceDir = location.sourceDir;
        info.scanPublicSourceDir = location.publicSourceDir;
        info.dataDir = location.dataDir;
        info.deviceProtectedDataDir = location.deviceProtectedDataDir;
        info.credentialProtectedDataDir = location.credentialProtectedDataDir;
        info.nativeLibraryDir = location.nativeLibraryDir;
        info.nativeLibraryRootDir = location.nativeLibraryDir;
        info.nativeLibraryRootRequiresIsa = false;
        info.primaryCpuAbi = "arm64-v8a";
        info.seInfo = "default";
        info.seInfoUser = "";
    }

    /**
     * PackageManagerService applies Direct Boot match defaults server-side before matching
     * components. ApplicationPackageManager deliberately does not -- it only warns through
     * onImplicitDirectBoot -- so a caller's flags arrive here with neither bit set. This
     * adapter is the server side. Without the defaults every component fails
     * PackageParser.isMatch (which requires one of the direct-boot bits), num stays 0, and
     * ArrayUtils.trimToSize returns null for size 0 -- nulling out services, activities,
     * receivers and providers wholesale. The source app runs with its credential-encrypted
     * storage mounted, which is the unlocked-user case: match aware and unaware alike.
     */
    private static long withDirectBootMatchDefaults(long flags) {
        if ((flags & (PackageManager.MATCH_DIRECT_BOOT_AWARE
                | PackageManager.MATCH_DIRECT_BOOT_UNAWARE)) != 0) {
            return flags;
        }
        return flags | PackageManager.MATCH_DIRECT_BOOT_AWARE
                | PackageManager.MATCH_DIRECT_BOOT_UNAWARE;
    }

    /** Diagnostic: metadata size on Firebase's discovery service, or -1 / -2 when absent. */
    private static int probeDiscoveryMeta(PackageInfo info) {
        if (info == null || info.services == null) return -2;
        for (android.content.pm.ServiceInfo s : info.services) {
            if (s != null && s.name != null && s.name.endsWith("ComponentDiscoveryService")) {
                return s.metaData == null ? -1 : s.metaData.size();
            }
        }
        return -2;
    }

    private static void applyLocation(ComponentInfo[] components) {
        if (components != null) for (ComponentInfo component : components) {
            applyLocation(component.applicationInfo);
        }
    }

    public static synchronized PackageInfo packageInfo(String packageName, long flags) {
        if (!contains(packageName)) return null;
        flags = withDirectBootMatchDefaults(flags);
        if ((flags & (PackageManager.GET_SIGNATURES | PackageManager.GET_SIGNING_CERTIFICATES)) != 0
                && !certificatesCollected) {
            try {
                PackageParser.collectCertificates(parsed, false);
                certificatesCollected = true;
            } catch (PackageParser.PackageParserException failure) {
                throw new SecurityException("Original APK certificate verification failed", failure);
            }
        }
        Set<String> granted = new HashSet<>();
        if ((flags & PackageManager.GET_PERMISSIONS) != 0) {
            for (String permission : parsed.requestedPermissions) {
                if (PermissionManagerAdapter.checkProcessPermission(permission, -1, location.uid)
                        == PackageManager.PERMISSION_GRANTED) granted.add(permission);
            }
        }
        PackageInfo value = PackageParser.generatePackageInfo(parsed, null, (int) flags,
                0, 0, granted, FrameworkPackageUserState.DEFAULT, 0);
        if (value == null) return null;
        int preMeta = probeDiscoveryMeta(value);
        // PackageParser can share objects with its resident state. Match Binder
        // value semantics so an app cannot mutate future package-manager results.
        Parcel parcel = Parcel.obtain();
        try {
            value.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            value = PackageInfo.CREATOR.createFromParcel(parcel);
        } finally {
            parcel.recycle();
        }
        if ((flags & PackageManager.GET_SERVICES) != 0) {
            System.err.println("[SOURCE-PKG] packageInfo flags=0x" + Long.toHexString(flags)
                    + " parsedServices=" + (parsed.services == null ? -1 : parsed.services.size())
                    + " genServices=" + (value.services == null ? -1 : value.services.length)
                    + " discoveryMeta pre-parcel=" + preMeta
                    + " post-parcel=" + probeDiscoveryMeta(value));
        }
        applyLocation(value.applicationInfo);
        applyLocation(value.activities);
        applyLocation(value.receivers);
        applyLocation(value.services);
        applyLocation(value.providers);
        return value;
    }
}

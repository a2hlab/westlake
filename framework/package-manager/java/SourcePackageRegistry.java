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

    /** The registered package's <uses-library> names, required then optional. */
    public static synchronized java.util.List<String> usesLibraries() {
        java.util.ArrayList<String> names = new java.util.ArrayList<>();
        if (parsed != null) {
            if (parsed.usesLibraries != null) names.addAll(parsed.usesLibraries);
            if (parsed.usesOptionalLibraries != null) names.addAll(parsed.usesOptionalLibraries);
        }
        return names;
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

    /**
     * PackageParser leaves processName null on a component without android:process;
     * PackageManagerService fills in the application's process before anyone reads it.
     * This registry is that server side. Without the default, AppSchedulerBridge's
     * same-process filter drops every main-process provider at bind, so none is created
     * before Application.onCreate (Firebase, Forter and ML Kit all initialize that way).
     */
    private static void applyLocation(ComponentInfo[] components) {
        if (components != null) for (ComponentInfo component : components) {
            applyLocation(component.applicationInfo);
            if (component.processName == null && component.applicationInfo != null) {
                component.processName = component.applicationInfo.processName != null
                        ? component.applicationInfo.processName : component.packageName;
            }
        }
    }

    /**
     * Web hosts the app's activities declare in http/https intent filters: the domains Android
     * tracks for its link handling (DomainVerificationUserState's host map).
     */
    public static synchronized java.util.Set<String> webHosts() {
        java.util.Set<String> hosts = new java.util.TreeSet<>();
        if (parsed == null || parsed.activities == null) return hosts;
        for (PackageParser.Activity activity : parsed.activities) {
            for (PackageParser.ActivityIntentInfo filter : activity.intents) {
                if (!filter.hasDataScheme("http") && !filter.hasDataScheme("https")) continue;
                for (int i = 0; i < filter.countDataAuthorities(); i++) {
                    String host = filter.getDataAuthority(i).getHost();
                    if (host != null && !host.isEmpty()) hosts.add(host);
                }
            }
        }
        return hosts;
    }

    /**
     * The app's own services an intent resolves to, from their manifest intent filters.
     *
     * queryIntentServices answered null, so every library that looks up its own service by action
     * found nothing. Media3 resolves the app's MediaSessionService that way while the Application
     * is created, and Fossify Music Player died on "Failed to resolve SessionToken". Only this
     * package's services are known here; an intent for another package matches nothing.
     */
    public static synchronized java.util.List<android.content.pm.ResolveInfo> queryServices(
            android.content.Intent intent, String resolvedType, long flags) {
        java.util.List<android.content.pm.ResolveInfo> out = new java.util.ArrayList<>();
        if (parsed == null || intent == null || parsed.services == null) return out;
        android.content.ComponentName component = intent.getComponent();
        String target = component != null ? component.getPackageName() : intent.getPackage();
        if (target != null && !target.equals(parsed.packageName)) return out;
        PackageInfo info = packageInfo(parsed.packageName, flags | PackageManager.GET_SERVICES);
        if (info == null || info.services == null) return out;
        String type = resolvedType != null ? resolvedType : intent.getType();
        for (PackageParser.Service service : parsed.services) {
            android.content.IntentFilter matched = null;
            if (component != null) {
                if (!component.getClassName().equals(service.info.name)) continue;
            } else {
                for (PackageParser.ServiceIntentInfo filter : service.intents) {
                    if (filter.match(intent.getAction(), type, intent.getScheme(), intent.getData(),
                            intent.getCategories(), "WESTLAKE-PM") >= 0) {
                        matched = filter;
                        break;
                    }
                }
                if (matched == null) continue;
            }
            for (android.content.pm.ServiceInfo generated : info.services) {
                if (generated.name.equals(service.info.name)) {
                    android.content.pm.ResolveInfo resolved = new android.content.pm.ResolveInfo();
                    resolved.serviceInfo = generated;
                    if ((flags & PackageManager.GET_RESOLVED_FILTER) != 0) resolved.filter = matched;
                    if (matched != null) resolved.priority = matched.getPriority();
                    out.add(resolved);
                    break;
                }
            }
        }
        return out;
    }

    /**
     * The app's own activities an intent resolves to, from their manifest intent filters, as
     * PackageManagerService's activity resolver answers: with MATCH_DEFAULT_ONLY (what
     * Intent.resolveActivity and an implicit startActivity ask for) only through a filter in
     * CATEGORY_DEFAULT, and the highest priority first.
     *
     * Implicit queries went to OH's bundle manager alone, which knows nothing of the app's manifest.
     * Shazam's splash resolves VIEW shazam_activity://configuration in its own package before it
     * starts its first screen; nothing resolved, nothing started, and the splash finished.
     */
    public static synchronized java.util.List<android.content.pm.ResolveInfo> queryActivities(
            android.content.Intent intent, String resolvedType, long flags) {
        java.util.List<android.content.pm.ResolveInfo> out = new java.util.ArrayList<>();
        if (parsed == null || intent == null || parsed.activities == null) return out;
        android.content.ComponentName component = intent.getComponent();
        String target = component != null ? component.getPackageName() : intent.getPackage();
        if (target != null && !target.equals(parsed.packageName)) return out;
        PackageInfo info = packageInfo(parsed.packageName, flags | PackageManager.GET_ACTIVITIES);
        if (info == null || info.activities == null) return out;
        String type = resolvedType != null ? resolvedType : intent.getType();
        boolean defaultOnly = (flags & PackageManager.MATCH_DEFAULT_ONLY) != 0;
        for (PackageParser.Activity activity : parsed.activities) {
            android.content.IntentFilter matched = null;
            int match = 0;
            if (component != null) {
                if (!component.getClassName().equals(activity.info.name)) continue;
            } else {
                for (PackageParser.ActivityIntentInfo filter : activity.intents) {
                    int result = filter.match(intent.getAction(), type, intent.getScheme(), intent.getData(),
                            intent.getCategories(), "WESTLAKE-PM");
                    if (result >= 0 && (!defaultOnly || filter.hasCategory(android.content.Intent.CATEGORY_DEFAULT))) {
                        matched = filter;
                        match = result;
                        break;
                    }
                }
                if (matched == null) continue;
            }
            for (android.content.pm.ActivityInfo generated : info.activities) {
                if (generated.name.equals(activity.info.name)) {
                    android.content.pm.ResolveInfo resolved = new android.content.pm.ResolveInfo();
                    resolved.activityInfo = generated;
                    if ((flags & PackageManager.GET_RESOLVED_FILTER) != 0) resolved.filter = matched;
                    if (matched != null) {
                        resolved.priority = matched.getPriority();
                        resolved.match = match;
                        resolved.isDefault = matched.hasCategory(android.content.Intent.CATEGORY_DEFAULT);
                    }
                    out.add(resolved);
                    break;
                }
            }
        }
        out.sort((a, b) -> Integer.compare(b.priority, a.priority));   // stable: manifest order within one
        return out;
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

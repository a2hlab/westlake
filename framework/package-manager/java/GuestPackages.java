package adapter.packagemanager;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.ComponentInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageParser;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.pm.pkg.FrameworkPackageUserState;
import android.os.Parcel;
import android.os.Process;
import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Packages other than the app whose code runs in the app's process: microG's GmsCore
 * (com.google.android.gms) and Companion (com.android.vending), staged by the launcher and named
 * in ASX_GUEST_APKS (colon-separated). Each is parsed from its APK and described as an installed,
 * enabled, non-system package with the app's uid -- the condition under which ActivityThread loads
 * another package's code in process -- and a data directory of its own inside the app's.
 *
 * Certificates are the APK's own (microG's). Google's are not claimed until Play-services binds are
 * routed to the guests; until then a client library that checks GmsCore's signature finds it
 * invalid and stays away from it, as it does on a phone without Play services.
 *
 * PairIP's license check (224 of the HK top 1000) binds Companion's licensing service, and
 * Companion reads its switches through GmsCore's settings provider, hence the pair.
 */
public final class GuestPackages {
    public static final String GMS = "com.google.android.gms";
    public static final String VENDING = "com.android.vending";

    private static final class Guest {
        final PackageParser.Package parsed;
        final String apk;
        Guest(PackageParser.Package parsed, String apk) {
            this.parsed = parsed;
            this.apk = apk;
        }
    }

    private static Map<String, Guest> sGuests;

    private GuestPackages() {}

    private static synchronized Map<String, Guest> guests() {
        if (sGuests != null) return sGuests;
        sGuests = new LinkedHashMap<>();
        String paths = System.getenv("ASX_GUEST_APKS");
        if (paths == null || paths.isEmpty()) return sGuests;
        for (String path : paths.split(":")) {
            if (path.isEmpty() || !new File(path).isFile()) continue;
            try {
                PackageParser.Package parsed = new PackageParser().parsePackage(new File(path), 0);
                if (SourcePackageRegistry.contains(parsed.packageName)) continue;
                sGuests.put(parsed.packageName, new Guest(parsed, path));
                System.err.println("[WL-GUEST] " + parsed.packageName + " version=" + parsed.mVersionCode
                        + " from " + path);
            } catch (Throwable t) {
                System.err.println("[WL-GUEST] cannot parse " + path + ": " + t);
            }
        }
        return sGuests;
    }

    public static boolean contains(String packageName) {
        return packageName != null && guests().containsKey(packageName);
    }

    /**
     * Westlake's defaults for microG, as the meta-data microG's settings provider falls back to
     * (MetaDataPreferences, prefix org.microg.gms.settings.). Upstream ships licensing off; on, it
     * is the path PairIP's check needs. Acquiring free apps on the user's account stays off.
     */
    private static void applyDefaults(ApplicationInfo info) {
        if (!GMS.equals(info.packageName)) return;
        if (info.metaData == null) info.metaData = new android.os.Bundle();
        info.metaData.putBoolean("org.microg.gms.settings.vending_licensing", true);
    }

    private static void applyLocation(ApplicationInfo info, Guest guest) {
        if (info == null) return;
        String dataDir = SourcePackageRegistry.dataDir();
        dataDir = (dataDir != null ? dataDir : "/data/data/" + info.packageName) + "/westlake-guests/"
                + info.packageName;
        new File(dataDir).mkdirs();
        info.uid = Process.myUid();
        info.sourceDir = info.publicSourceDir = guest.apk;
        info.scanSourceDir = info.scanPublicSourceDir = guest.apk;
        info.dataDir = info.deviceProtectedDataDir = info.credentialProtectedDataDir = dataDir;
        info.nativeLibraryDir = info.nativeLibraryRootDir = dataDir + "/lib";
        info.nativeLibraryRootRequiresIsa = false;
        info.primaryCpuAbi = "arm64-v8a";
        info.seInfo = "default";
        info.seInfoUser = "";
        info.flags &= ~(ApplicationInfo.FLAG_SYSTEM | ApplicationInfo.FLAG_UPDATED_SYSTEM_APP
                | ApplicationInfo.FLAG_STOPPED);
        info.flags |= ApplicationInfo.FLAG_INSTALLED;
        info.enabled = true;
        if (info.processName == null) info.processName = info.packageName;
    }

    private static void applyLocation(ComponentInfo[] components, Guest guest) {
        if (components != null) for (ComponentInfo component : components) {
            applyLocation(component.applicationInfo, guest);
            if (component.processName == null) component.processName = component.packageName;
        }
    }

    public static synchronized PackageInfo packageInfo(String packageName, long flags) {
        Guest guest = guests().get(packageName);
        if (guest == null) return null;
        if ((flags & (PackageManager.MATCH_DIRECT_BOOT_AWARE | PackageManager.MATCH_DIRECT_BOOT_UNAWARE)) == 0) {
            flags |= PackageManager.MATCH_DIRECT_BOOT_AWARE | PackageManager.MATCH_DIRECT_BOOT_UNAWARE;
        }
        if ((flags & (PackageManager.GET_SIGNATURES | PackageManager.GET_SIGNING_CERTIFICATES)) != 0
                && guest.parsed.mSigningDetails == PackageParser.SigningDetails.UNKNOWN) {
            try {
                PackageParser.collectCertificates(guest.parsed, false);
            } catch (PackageParser.PackageParserException failure) {
                throw new SecurityException("Guest APK certificate verification failed", failure);
            }
        }
        PackageInfo value = PackageParser.generatePackageInfo(guest.parsed, null, (int) flags,
                0, 0, new java.util.HashSet<String>(guest.parsed.requestedPermissions),
                FrameworkPackageUserState.DEFAULT, 0);
        if (value == null) return null;
        Parcel parcel = Parcel.obtain();
        try {
            value.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            value = PackageInfo.CREATOR.createFromParcel(parcel);
        } finally {
            parcel.recycle();
        }
        applyLocation(value.applicationInfo, guest);
        if ((flags & PackageManager.GET_META_DATA) != 0) applyDefaults(value.applicationInfo);
        applyLocation(value.activities, guest);
        applyLocation(value.receivers, guest);
        applyLocation(value.services, guest);
        applyLocation(value.providers, guest);
        return value;
    }

    public static ApplicationInfo applicationInfo(String packageName, long flags) {
        PackageInfo info = packageInfo(packageName, flags);
        return info == null ? null : info.applicationInfo;
    }

    /** The guests' services an intent names, explicitly or by action within a guest package. */
    public static synchronized List<ResolveInfo> queryServices(Intent intent, String resolvedType, long flags) {
        List<ResolveInfo> out = new ArrayList<>();
        if (intent == null) return out;
        ComponentName component = intent.getComponent();
        String target = component != null ? component.getPackageName() : intent.getPackage();
        String type = resolvedType != null ? resolvedType : intent.getType();
        for (Guest guest : guests().values()) {
            if (target != null && !target.equals(guest.parsed.packageName)) continue;
            if (guest.parsed.services == null) continue;
            PackageInfo info = null;
            for (PackageParser.Service service : guest.parsed.services) {
                android.content.IntentFilter matched = null;
                if (component != null) {
                    if (!component.getClassName().equals(service.info.name)) continue;
                } else {
                    for (PackageParser.ServiceIntentInfo filter : service.intents) {
                        if (filter.match(intent.getAction(), type, intent.getScheme(), intent.getData(),
                                intent.getCategories(), "WESTLAKE-GUEST") >= 0) {
                            matched = filter;
                            break;
                        }
                    }
                    if (matched == null) continue;
                }
                if (info == null) info = packageInfo(guest.parsed.packageName, flags | PackageManager.GET_SERVICES);
                if (info == null || info.services == null) break;
                for (ServiceInfo generated : info.services) {
                    if (!generated.name.equals(service.info.name)) continue;
                    ResolveInfo resolved = new ResolveInfo();
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

    public static ServiceInfo serviceInfo(ComponentName component, long flags) {
        PackageInfo info = packageInfo(component.getPackageName(), flags | PackageManager.GET_SERVICES);
        if (info != null && info.services != null) for (ServiceInfo service : info.services) {
            if (component.getClassName().equals(service.name)) return service;
        }
        return null;
    }

    public static ProviderInfo providerInfo(ComponentName component, long flags) {
        PackageInfo info = packageInfo(component.getPackageName(), flags | PackageManager.GET_PROVIDERS);
        if (info != null && info.providers != null) for (ProviderInfo provider : info.providers) {
            if (component.getClassName().equals(provider.name)) return provider;
        }
        return null;
    }

    /** The guest provider that declares an authority, or null. */
    public static ProviderInfo resolveProvider(String authority, long flags) {
        if (authority == null) return null;
        for (String packageName : new ArrayList<>(guests().keySet())) {
            PackageInfo info = packageInfo(packageName, flags | PackageManager.GET_PROVIDERS);
            if (info == null || info.providers == null) continue;
            for (ProviderInfo provider : info.providers) {
                if (provider.authority == null) continue;
                for (String declared : provider.authority.split(";")) {
                    if (authority.equals(declared.trim())) return provider;
                }
            }
        }
        return null;
    }
}

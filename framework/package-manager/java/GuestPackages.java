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
import android.content.pm.Signature;
import android.content.pm.SigningDetails;
import android.content.pm.SigningInfo;
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

    /**
     * Beside a shared Play services process, a guest answers only for the Play Store's licensing
     * service, which that process doesn't host. Package details and every other service stay
     * Play services', whose Google certificates the availability check reads.
     */
    private static final boolean BESIDE_PLAY_SERVICES = System.getenv("WESTLAKE_PLAY_SERVICES_CONNECT") != null;
    private static final String LICENSING_ACTION = "com.android.vending.licensing.ILicensingService";

    /**
     * Beside shared Play services, the Play Store's details are Google's, without microG's
     * meta-data: Companion's settings then live in its own provider (its package's authority)
     * and read the licensing default from this copy.
     */
    public static ApplicationInfo besideDefaults(String packageName, ApplicationInfo info) {
        if (!BESIDE_PLAY_SERVICES || info == null || !VENDING.equals(packageName)
                || !guests().containsKey(VENDING)) {
            return info;
        }
        ApplicationInfo copy = new ApplicationInfo(info);
        copy.metaData = info.metaData == null ? new android.os.Bundle() : new android.os.Bundle(info.metaData);
        copy.metaData.putBoolean("org.microg.gms.settings.vending_licensing", true);
        return copy;
    }

    /** Whether a guest should answer this bind beside a shared Play services process. */
    public static boolean answersLicensing(Intent intent) {
        return BESIDE_PLAY_SERVICES && intent != null && LICENSING_ACTION.equals(intent.getAction())
                && VENDING.equals(intent.getPackage()) && guests().containsKey(VENDING);
    }

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
        return !BESIDE_PLAY_SERVICES && packageName != null && guests().containsKey(packageName);
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
        SourcePackageRegistry.bootClassSigningKeys(value);
        Parcel parcel = Parcel.obtain();
        try {
            value.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            value = PackageInfo.CREATOR.createFromParcel(parcel);
        } finally {
            parcel.recycle();
        }
        if ((flags & (PackageManager.GET_SIGNATURES | PackageManager.GET_SIGNING_CERTIFICATES)) != 0) {
            fakeSignature(guest.parsed, value);
        }
        applyLocation(value.applicationInfo, guest);
        if ((flags & PackageManager.GET_META_DATA) != 0) applyDefaults(value.applicationInfo);
        applyLocation(value.activities, guest);
        applyLocation(value.receivers, guest);
        applyLocation(value.services, guest);
        applyLocation(value.providers, guest);
        return value;
    }

    /**
     * microG's signature spoofing, as the ROMs that host microG implement it: a package that
     * requests FAKE_PACKAGE_SIGNATURE and names a certificate in its "fake-signature" meta-data is
     * reported with that certificate. GmsCore and Companion name Google's, which the Play services
     * client libraries in apps check before they use either package; with microG's own
     * certificate they answered SERVICE_INVALID and "Google Play services not available"
     * (AdvertisingIdClient, Firebase, sign-in, maps).
     */
    private static void fakeSignature(PackageParser.Package parsed, PackageInfo value) {
        if (parsed.mAppMetaData == null || parsed.requestedPermissions == null
                || !parsed.requestedPermissions.contains("android.permission.FAKE_PACKAGE_SIGNATURE")) {
            return;
        }
        String certificate = parsed.mAppMetaData.getString("fake-signature");
        if (certificate == null || certificate.isEmpty()) return;
        Signature[] fake = { new Signature(certificate) };
        if (value.signatures != null) value.signatures = fake;
        if (value.signingInfo != null) {
            try {
                value.signingInfo = new SigningInfo(
                        new SigningDetails(fake, SigningDetails.SignatureSchemeVersion.SIGNING_BLOCK_V3));
            } catch (java.security.cert.CertificateException e) {
                value.signingInfo = null;
            }
        }
    }

    public static ApplicationInfo applicationInfo(String packageName, long flags) {
        PackageInfo info = packageInfo(packageName, flags);
        return info == null ? null : info.applicationInfo;
    }

    /** The guests' services an intent names, explicitly or by action within a guest package. */
    public static synchronized List<ResolveInfo> queryServices(Intent intent, String resolvedType, long flags) {
        List<ResolveInfo> out = new ArrayList<>();
        if (intent == null) return out;
        if (BESIDE_PLAY_SERVICES && !answersLicensing(intent)) return out;
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
        // Companion reads its licensing switch from microG's settings provider.
        if (BESIDE_PLAY_SERVICES && !authority.contains(".microg.")) return null;
        // microG's own settings provider belongs to GmsCore, which isn't staged beside shared
        // Play services: Companion's copy of the same provider answers in its place.
        if (BESIDE_PLAY_SERVICES && authority.startsWith(GMS + ".microg.")) {
            authority = VENDING + authority.substring(GMS.length());
        }
        for (String packageName : new ArrayList<>(guests().keySet())) {
            if (BESIDE_PLAY_SERVICES && !VENDING.equals(packageName)) continue;
            PackageInfo info = packageInfo(packageName, flags | PackageManager.GET_PROVIDERS);
            if (info == null || info.providers == null) continue;
            for (ProviderInfo provider : info.providers) {
                if (provider.authority == null) continue;
                for (String declared : provider.authority.split(";")) {
                    if (!authority.equals(declared.trim())) continue;
                    if (!BESIDE_PLAY_SERVICES) return provider;
                    // One instance serves both names, whichever is asked first: the provider
                    // checks each call's authority against the ones it was made with.
                    ProviderInfo both = new ProviderInfo(provider);
                    both.authority = provider.authority + ";" + GMS + authority.substring(VENDING.length());
                    return both;
                }
            }
        }
        return null;
    }
}

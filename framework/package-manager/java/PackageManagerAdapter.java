/*
 * PackageManagerAdapter.java
 *
 * IPackageManager adapter that routes Android PackageManager queries to OH BMS.
 *
 * Installation path: AOSP PackageManager singleton replaced with this adapter
 * (similar to ActivityManagerAdapter pattern).
 *
 * Query flow:
 *   App -> PackageManager -> IPackageManager.Stub (this class) -> JNI -> OH BMS
 *        -> BundleInfo JSON -> PackageInfoBuilder -> Android PackageInfo -> App
 *
 * Tags:
 *   [BRIDGED] - Fully implemented, routes to BMS
 *   [STUB]    - Returns default/empty value (Phase 1)
 */
package adapter.packagemanager;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.IPackageManager;
import android.content.pm.PackageInfo;
import android.content.pm.ParceledListSlice;
import android.content.pm.dex.IArtManager;
import android.content.pm.ChangedPackages;
import android.content.pm.ModuleInfo;
import android.content.pm.PermissionInfo;
import android.content.pm.PermissionGroupInfo;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.os.RemoteException;
import android.util.Log;
import android.graphics.Bitmap;
import android.content.pm.KeySet;
import android.content.pm.IPackageInstaller;
import android.content.pm.VerifierDeviceIdentity;

import adapter.activity.IntentWantConverter;
import adapter.packagemanager.PackageInfoBuilder;
import adapter.packagemanager.PermissionMapper;

import java.util.ArrayList;
import java.util.List;

public class PackageManagerAdapter extends IPackageManager.Stub {

    /**
     * Optional AOSP WebView package exposed by the OH package boundary.
     *
     * Android normally gets this PackageInfo from PackageManagerService after
     * WebViewUpdateService has selected an installed provider.  appspawn-x has
     * neither Android PMS nor SystemServer, so a board-matched provider can be
     * supplied explicitly with ASX_WEBVIEW_APK.  Keeping this representation in
     * the package-manager adapter lets the unmodified framework continue through
     * WebViewFactory's normal verification, context creation, class loading, and
     * provider factory paths.
     */
    private static final String SIDELOADED_WEBVIEW_PACKAGE = "com.android.webview";
    private static final int SIDELOADED_WEBVIEW_VERSION_CODE = 541412303;
    private static final String SIDELOADED_WEBVIEW_VERSION_NAME = "109.0.5414.123";

    private static String getenv(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isEmpty() ? fallback : value;
    }

    /** Returns null unless an explicitly configured provider APK exists. */
    public static PackageInfo getSideloadedWebViewPackageInfo() {
        String apkPath = System.getenv("ASX_WEBVIEW_APK");
        if (apkPath == null || apkPath.isEmpty()
                || !new java.io.File(apkPath).isFile()) {
            return null;
        }

        String libDir = getenv("ASX_WEBVIEW_LIB_DIR",
                "/data/local/tmp/asx/webview-t-lib");
        String dataDir = getenv("ASX_WEBVIEW_DATA_DIR",
                "/data/local/tmp/asx/webview-t-data");

        ApplicationInfo ai = new ApplicationInfo();
        ai.packageName = SIDELOADED_WEBVIEW_PACKAGE;
        ai.processName = SIDELOADED_WEBVIEW_PACKAGE;
        ai.className = "org.chromium.android_webview.nonembedded.WebViewApkApplication";
        ai.sourceDir = apkPath;
        ai.publicSourceDir = apkPath;
        ai.scanSourceDir = apkPath;
        ai.scanPublicSourceDir = apkPath;
        ai.dataDir = dataDir;
        ai.deviceProtectedDataDir = dataDir;
        ai.credentialProtectedDataDir = dataDir;
        ai.nativeLibraryDir = libDir;
        ai.nativeLibraryRootDir = libDir;
        ai.nativeLibraryRootRequiresIsa = false;
        ai.primaryCpuAbi = "arm64-v8a";
        ai.uid = android.os.Process.myUid();
        ai.enabled = true;
        // This is deliberately represented as an installed data package, not
        // FLAG_SYSTEM. LoadedApk's bundled-system-app branch derives a native
        // search path from the platform install root; a provider staged at the
        // OH boundary has an explicit nativeLibraryDir instead.
        ai.flags = ApplicationInfo.FLAG_HAS_CODE
                | ApplicationInfo.FLAG_INSTALLED
                | ApplicationInfo.FLAG_EXTRACT_NATIVE_LIBS
                | ApplicationInfo.FLAG_MULTIARCH;
        ai.minSdkVersion = 24;
        ai.targetSdkVersion = 33;
        ai.compileSdkVersion = 33;
        ai.versionCode = SIDELOADED_WEBVIEW_VERSION_CODE;
        ai.longVersionCode = SIDELOADED_WEBVIEW_VERSION_CODE;
        ai.splitSourceDirs = new String[0];
        ai.splitPublicSourceDirs = new String[0];
        ai.splitNames = new String[0];
        ai.splitDependencies = new android.util.SparseArray<int[]>(0);
        ai.sharedLibraryFiles = new String[0];
        ai.resourceDirs = new String[0];
        ai.seInfo = "default";
        ai.seInfoUser = "";
        ai.metaData = new android.os.Bundle();
        ai.metaData.putString("com.android.webview.WebViewLibrary",
                "libwebviewchromium.so");

        PackageInfo pi = new PackageInfo();
        pi.packageName = SIDELOADED_WEBVIEW_PACKAGE;
        pi.versionCode = SIDELOADED_WEBVIEW_VERSION_CODE;
        pi.versionName = SIDELOADED_WEBVIEW_VERSION_NAME;
        pi.compileSdkVersion = 33;
        pi.applicationInfo = ai;
        // Both WebViewUpdateServiceAdapter and IPackageManager return an
        // equivalent unsigned package. WebViewFactory treats two null signature
        // arrays as equal, while still checking package name, version, and
        // WebViewLibrary metadata exactly as upstream does.
        pi.signatures = null;
        return pi;
    }

    private static boolean isSideloadedWebViewPackage(String packageName) {
        return SIDELOADED_WEBVIEW_PACKAGE.equals(packageName)
                && getSideloadedWebViewPackageInfo() != null;
    }

    private static volatile PackageManagerAdapter sInstance;

    public static PackageManagerAdapter getInstance() {
        if (sInstance == null) {
            synchronized (PackageManagerAdapter.class) {
                if (sInstance == null) {
                    sInstance = new PackageManagerAdapter();
                }
            }
        }
        return sInstance;
    }

    public PackageManagerAdapter() { super(OHPermissionEnforcer.getInstance()); }

    @Override
    public boolean[] canPackageQuery(String callerPackage, String[] targetPackages, int userId) {
        boolean[] r = new boolean[targetPackages != null ? targetPackages.length : 0];
        java.util.Arrays.fill(r, true);
        return r;
    }

    @Override
    public boolean waitForHandler(long timeoutMillis, boolean forBackgroundHandler) {
        return true;
    }

    @Override
    public void setKeepUninstalledPackages(java.util.List<String> packageNames) {
        // stub
    }

    @Override
    public void holdLock(android.os.IBinder token, int duration) {
        // stub
    }


    @Override
    public void makeUidVisible(int recipientAppId, int visibleUid) {
        
    }

    @Override
    public void makeProviderVisible(int recipientAppId, String visibleAuthority) {
        
    }

    @Override
    public boolean isAutoRevokeWhitelisted(String packageName) {
        return false;
    }

    @Override
    public List<String> getMimeGroup(String packageName, String group) {
        return null;
    }

    @Override
    public void setSplashScreenTheme(String packageName, String themeName, int userId) {
        
    }

    @Override
    public String getSplashScreenTheme(String packageName, int userId) {
        return null;
    }

    @Override
    public void setMimeGroup(String packageName, String group, List<String> mimeTypes) {
        
    }

    @Override
    public int checkUidPermission(String permName, int uid) {
        return 0;
    }

    @Override
    public void grantRuntimePermission(String packageName, String permissionName, int userId) {
        
    }

    @Override
    public void removePermission(String name) {
        
    }

    @Override
    public boolean addPermissionAsync(PermissionInfo info) {
        return false;
    }

    @Override
    public boolean addPermission(PermissionInfo info) {
        return false;
    }

    @Override
    public PermissionGroupInfo getPermissionGroupInfo(String name, int flags) {
        return null;
    }

    @Override
    public String[] getAppOpPermissionPackages(String permissionName, int userId) {
        return null;
    }

    @Override
    public android.content.IntentSender getLaunchIntentSenderForPackage(String packageName, String callingPackage, String featureId, int userId) {
        return null;
    }
    @Override
    public void requestPackageChecksums(String packageName, boolean includeSplits,
            int optional, int required, java.util.List trustedInstallers,
            android.content.pm.IOnChecksumsReadyListener onChecksumsReadyListener, int userId) {
        // stub
    }


    @Override
    public void notifyPackagesReplacedReceived(String[] packages) {
        
    }

    @Override
    public void setRuntimePermissionsVersion(int version, int userId) {
        
    }

    @Override
    public int getRuntimePermissionsVersion(int userId) {
        return 0;
    }

    @Override
    public ModuleInfo getModuleInfo(String packageName, int flags) {
        return null;
    }

    @Override
    public List<ModuleInfo> getInstalledModules(int flags) {
        return null;
    }

    @Override
    public void sendDeviceCustomizationReadyBroadcast() {
        
    }

    @Override
    public boolean isPackageStateProtected(String packageName, int userId) {
        return false;
    }

    @Override
    public String getIncidentReportApproverPackageName() {
        return null;
    }

    @Override
    public String getSetupWizardPackageName() {
        return null;
    }

    @Override
    public String getSystemCaptionsServicePackageName() {
        return null;
    }

    @Override
    public String getAppPredictionServicePackageName() {
        return null;
    }

    @Override
    public String getWellbeingPackageName() {
        return null;
    }

    @Override
    public String getRotationResolverPackageName() {
        return null;
    }

    @Override
    public String getAttentionServicePackageName() {
        return null;
    }

    @Override
    public String getSystemTextClassifierPackageName() {
        return null;
    }

    @Override
    public String getDefaultTextClassifierPackageName() {
        return null;
    }

    @Override
    public boolean hasUidSigningCertificate(int uid, byte[] signingCertificate, int flags) {
        return false;
    }

    @Override
    public boolean hasSigningCertificate(String packageName, byte[] signingCertificate, int flags) {
        return false;
    }

    @Override
    public CharSequence getHarmfulAppWarning(String packageName, int userId) {
        return null;
    }

    @Override
    public void setHarmfulAppWarning(String packageName, CharSequence warning, int userId) {
        
    }

    @Override
    public IArtManager getArtManager() {
        return null;
    }

    @Override
    public String getInstantAppAndroidId(String packageName, int userId) {
        return null;
    }

    @Override
    public ComponentName getInstantAppInstallerComponent() {
        return null;
    }

    @Override
    public ComponentName getInstantAppResolverSettingsComponent() {
        return null;
    }

    @Override
    public ComponentName getInstantAppResolverComponent() {
        return null;
    }

    @Override
    public void deletePreloadsFileCache() {
        
    }

    @Override
    public boolean canRequestPackageInstalls(String packageName, int userId) {
        return false;
    }

    @Override
    public ParceledListSlice getDeclaredSharedLibraries(String packageName, long flags, int userId) {
        return null;
    }

    @Override
    public ParceledListSlice getSharedLibraries(String packageName, long flags, int userId) {
        return null;
    }

    @Override
    public int getInstallReason(String packageName, int userId) {
        return 0;
    }

    @Override
    public String getSharedSystemSharedLibraryPackageName() {
        return null;
    }

    @Override
    public ChangedPackages getChangedPackages(int sequenceNumber, int userId) {
        return null;
    }

    @Override
    public boolean isPackageDeviceAdminOnAnyUser(String packageName) {
        return false;
    }


    @Override
    public String getServicesSystemSharedLibraryPackageName() {
        return null;
    }

    @Override
    public void setUpdateAvailable(String packageName, boolean updateAvaialble) {
        
    }

    @Override
    public boolean setRequiredForSystemUser(String packageName, boolean systemUserApp) {
        return false;
    }

    @Override
    public boolean isInstantApp(String packageName, int userId) {
        return false;
    }

    @Override
    public Bitmap getInstantAppIcon(String packageName, int userId) {
        return null;
    }

    @Override
    public boolean setInstantAppCookie(String packageName, byte[] cookie, int userId) {
        return false;
    }

    @Override
    public byte[] getInstantAppCookie(String packageName, int userId) {
        return null;
    }

    @Override
    public ParceledListSlice getInstantApps(int userId) {
        return null;
    }

    @Override
    public String getSdkSandboxPackageName() {
        return null;
    }

    @Override
    public String getPermissionControllerPackageName() {
        return null;
    }

    @Override
    public boolean isPackageSignedByKeySetExactly(String packageName, KeySet ks) {
        return false;
    }

    @Override
    public boolean isPackageSignedByKeySet(String packageName, KeySet ks) {
        return false;
    }

    @Override
    public KeySet getSigningKeySet(String packageName) {
        return null;
    }

    @Override
    public KeySet getKeySetByAlias(String packageName, String alias) {
        return null;
    }

    @Override
    public boolean getBlockUninstallForUser(String packageName, int userId) {
        return false;
    }

    @Override
    public boolean setBlockUninstallForUser(String packageName, boolean blockUninstall, int userId) {
        return false;
    }

    @Override
    public IPackageInstaller getPackageInstaller() {
        return null;
    }

    @Override
    public boolean setSystemAppInstallState(String packageName, boolean installed, int userId) {
        return false;
    }

    @Override
    public void setSystemAppHiddenUntilInstalled(String packageName, boolean hidden) {
        
    }

    @Override
    public boolean getApplicationHiddenSettingAsUser(String packageName, int userId) {
        return false;
    }

    @Override
    public boolean setApplicationHiddenSettingAsUser(String packageName, boolean hidden, int userId) {
        return false;
    }

    @Override
    public boolean isStorageLow() {
        return false;
    }

    @Override
    public boolean isDeviceUpgrading() {
        return false;
    }

    @Override
    public boolean isFirstBoot() {
        return false;
    }

    @Override
    public VerifierDeviceIdentity getVerifierDeviceIdentity() {
        return null;
    }

    @Override
    public ParceledListSlice getAllIntentFilters(String packageName) {
        return null;
    }

    @Override
    public ParceledListSlice getIntentFilterVerifications(String packageName) {
        return null;
    }

    @Override
    public boolean updateIntentVerificationStatus(String packageName, int status, int userId) {
        return false;
    }

    @Override
    public int getIntentVerificationStatus(String packageName, int userId) {
        return 0;
    }

    @Override
    public void verifyIntentFilter(int id, int verificationCode, List<String> failedDomains) {
        
    }

    @Override
    public android.os.IBinder getHoldLockToken() {
        return null;
    }

    @Override
    public android.content.pm.PackageManager.Property getPropertyAsUser(String propertyName, String packageName, String className, int userId) {
        return null;
    }

    @Override
    public android.content.pm.ParceledListSlice queryProperty(String propertyName, int componentType) {
        return new android.content.pm.ParceledListSlice(java.util.Collections.emptyList());
    }

    private static final String TAG = "OH_PMAdapter";

    // ========================================================================
    // JNI native methods — each calls OH BMS via JNI
    // ========================================================================

    /**
     * Query BMS.GetBundleInfo(bundleName, flags).
     * Returns BundleInfo as JSON string.
     */
    private static native String nativeGetBundleInfo(String bundleName, int flags);

    /**
     * Query BMS.GetApplicationInfo(bundleName, flags).
     * Returns ApplicationInfo as JSON string.
     */
    private static native String nativeGetApplicationInfo(String bundleName, int flags);

    /**
     * Query BMS.GetBundleInfos(flags).
     * Returns JSON array of BundleInfo strings.
     */
    private static native String nativeGetAllBundleInfos(int flags);

    /**
     * Query BMS.QueryAbilityInfos(want, flags).
     * Returns JSON array of AbilityInfo.
     */
    private static native String nativeQueryAbilityInfos(String wantJson, int flags);

    /**
     * Query BMS.GetUidByBundleName(bundleName).
     * Returns UID, or -1 if not found.
     */
    private static native int nativeGetUidByBundleName(String bundleName);

    /**
     * Query BMS.CheckPermission(bundleName, permission).
     * Returns 0 for granted, -1 for denied.
     */
    private static native int nativeCheckPermission(String bundleName, String permission);

    /**
     * 2026-04-30 P2-B — On-demand parse of installed APK's AndroidManifest.xml.
     * Returns JSON with Android-specific fields not stored in OH BMS (className,
     * theme, providers, largeHeap, appComponentFactory, classLoaderName,
     * networkSecurityConfigRes, processName). Empty string on failure.
     * Spec: doc/ability_manager_ipc_adapter_design.html §1.1.4
     */
    private static native String nativeParseApkManifestJson(String packageName);

    /**
     * 2026-04-30 P2-B — Read OH SystemProperties (OHOS::system::GetParameter).
     * Used for properties not in AOSP's property store (e.g. ro.serialno).
     */
    private static native String nativeGetOhSystemProperty(String key, String defValue);

    /**
     * Public accessor: parse the installed APK manifest for Android-specific fields.
     * Result cached per packageName.
     */
    private static final java.util.concurrent.ConcurrentHashMap<String, String> sManifestCache =
            new java.util.concurrent.ConcurrentHashMap<>();

    /**
     * Packages BMS has already said it does not have. Nothing installs packages into a running
     * child, and an app that polls for an absent one (a Play-services check, typically) otherwise
     * pays a full APK path resolution and manifest query on every call.
     */
    private static final java.util.Set<String> sAbsentPackages =
            java.util.concurrent.ConcurrentHashMap.newKeySet();
    public static String getApkManifestJson(String packageName) {
        if (packageName == null || packageName.isEmpty()) return "";
        String cached = sManifestCache.get(packageName);
        if (cached != null) return cached;
        String result = nativeParseApkManifestJson(packageName);
        if (result == null) result = "";
        sManifestCache.put(packageName, result);
        return result;
    }

    /** Public accessor for the OH system properties bridge. */
    public static String getOhSystemProperty(String key, String defValue) {
        try {
            String v = nativeGetOhSystemProperty(key, defValue);
            return v != null ? v : defValue;
        } catch (Throwable t) {
            return defValue;
        }
    }

    // ========================================================================
    // Category 1: Package Info Queries
    // ========================================================================

    /**
     * [BRIDGED] getPackageInfo -> BMS.GetBundleInfo
     */
    @Override
    public PackageInfo getPackageInfo(String packageName, long flags, int userId) {
        logBridged("getPackageInfo", packageName);
        if (SourcePackageRegistry.contains(packageName)) {
            return SourcePackageRegistry.packageInfo(packageName, flags);
        }
        if (isSideloadedWebViewPackage(packageName)) {
            System.err.println("[WESTLAKE-WEBVIEW] IPackageManager.getPackageInfo -> "
                    + System.getenv("ASX_WEBVIEW_APK"));
            return getSideloadedWebViewPackageInfo();
        }
        if (sAbsentPackages.contains(packageName)) {
            return null;   // asked and answered; see sAbsentPackages
        }
        String json = nativeGetBundleInfo(packageName, (int) flags);
        if (json == null || json.isEmpty()) {
            if (sAbsentPackages.add(packageName)) {
                Log.w(TAG, "getPackageInfo: not found: " + packageName
                        + " (remembered; later lookups answer null without re-resolving the APK)");
            }
            return null;
        }
        PackageInfo result = PackageInfoBuilder.fromBundleInfo(json);
        // OH BMS describes OH ExtensionAbilities, not the Android providers
        // declared in the unchanged APK manifest. PackageManager.GET_PROVIDERS
        // is nevertheless required to return those ProviderInfo entries. The
        // adapter already parses the APK manifest for AppBindData; reuse that
        // same boundary parser here so package queries and process binding see
        // one authoritative component set.
        if (result != null
                && (flags & android.content.pm.PackageManager.GET_PROVIDERS) != 0) {
            java.util.List<ProviderInfo> providers =
                    getProvidersForPackage(packageName);
            if (result.applicationInfo != null) {
                for (ProviderInfo provider : providers) {
                    provider.applicationInfo = result.applicationInfo;
                }
            }
            result.providers = providers.toArray(new ProviderInfo[providers.size()]);
            System.err.println("[WESTLAKE-PM] getPackageInfo providers "
                    + packageName + " count=" + result.providers.length);
        }
        return result;
    }

    /**
     * [BRIDGED] getApplicationInfo -> BMS.GetApplicationInfo
     */
    @Override
    public ApplicationInfo getApplicationInfo(String packageName, long flags, int userId) {
        logBridged("getApplicationInfo", packageName);
        if (SourcePackageRegistry.contains(packageName)) {
            PackageInfo info = SourcePackageRegistry.packageInfo(packageName, flags);
            return info == null ? null : info.applicationInfo;
        }
        if (isSideloadedWebViewPackage(packageName)) {
            return getSideloadedWebViewPackageInfo().applicationInfo;
        }
        ApplicationInfo cached = ApplicationInfoCache.get(packageName, flags, userId);
        if (cached != null) {
            return cached;
        }
        if (sAbsentPackages.contains(packageName)) {
            return null;   // asked and answered; see sAbsentPackages
        }
        String json = nativeGetApplicationInfo(packageName, (int) flags);
        if (json == null || json.isEmpty()) {
            sAbsentPackages.add(packageName);
            return null;
        }
        PackageInfo pi = PackageInfoBuilder.fromBundleInfo(json);
        ApplicationInfo result = pi != null ? pi.applicationInfo : null;
        return ApplicationInfoCache.putAndCopy(packageName, flags, userId, result);
    }

    /**
     * [BRIDGED] getInstalledPackages -> BMS.GetBundleInfos
     */
    @Override
    public ParceledListSlice<PackageInfo> getInstalledPackages(long flags, int userId) {
        logBridged("getInstalledPackages", "flags=" + flags);
        String json = nativeGetAllBundleInfos((int) flags);
        List<PackageInfo> result = new ArrayList<>();
        if (json == null || json.isEmpty()) {
            return new ParceledListSlice<>(result);
        }

        try {
            org.json.JSONArray array = new org.json.JSONArray(json);
            for (int i = 0; i < array.length(); i++) {
                String bundleJson = array.getString(i);
                PackageInfo pi = PackageInfoBuilder.fromBundleInfo(bundleJson);
                if (pi != null) {
                    result.add(pi);
                }
            }
        } catch (org.json.JSONException e) {
            Log.e(TAG, "getInstalledPackages: JSON parse error", e);
        }

        return new ParceledListSlice<>(result);
    }

    // ========================================================================
    // Category 2: Component Queries
    // ========================================================================

    /**
     * [BRIDGED] getActivityInfo -> BMS.QueryAbilityInfo
     */
    @Override
    public ActivityInfo getActivityInfo(ComponentName component, long flags, int userId) {
        logBridged("getActivityInfo", component.flattenToShortString());
        if (SourcePackageRegistry.contains(component.getPackageName())) {
            PackageInfo info = SourcePackageRegistry.packageInfo(component.getPackageName(),
                    flags | android.content.pm.PackageManager.GET_ACTIVITIES);
            if (info != null && info.activities != null) for (ActivityInfo activity : info.activities) {
                if (component.getClassName().equals(activity.name)) return activity;
            }
            return null;
        }
        // Convert ComponentName to Want-style query
        IntentWantConverter.WantParams want = new IntentWantConverter.WantParams();
        want.bundleName = component.getPackageName();
        want.abilityName = IntentWantConverter.abilityNameToClassName(component.getClassName());
        // Actually we need class->ability name conversion
        String abilityName = component.getClassName();
        int lastDot = abilityName.lastIndexOf('.');
        if (lastDot >= 0) {
            abilityName = abilityName.substring(lastDot + 1);
        }
        abilityName += "Ability";

        String wantJson = "{\"bundleName\":\"" + component.getPackageName()
                + "\",\"abilityName\":\"" + abilityName + "\"}";
        String json = nativeQueryAbilityInfos(wantJson, (int) flags);
        if (json == null || json.isEmpty()) {
            return null;
        }

        try {
            org.json.JSONArray array = new org.json.JSONArray(json);
            if (array.length() > 0) {
                org.json.JSONObject abilityJson = array.getJSONObject(0);
                ActivityInfo ai = new ActivityInfo();
                ai.name = IntentWantConverter.abilityNameToClassName(
                    component.getPackageName(), abilityJson.getString("name"));
                ai.packageName = component.getPackageName();
                ai.exported = abilityJson.optBoolean("visible", false);
                return ai;
            }
        } catch (org.json.JSONException e) {
            Log.e(TAG, "getActivityInfo: parse error", e);
        }
        return null;
    }

    /**
     * [BRIDGED] resolveIntent -> BMS.QueryAbilityInfos
     */
    @Override
    public ResolveInfo resolveIntent(Intent intent, String resolvedType, long flags, int userId) {
        logBridged("resolveIntent", intent.toString());
        List<ResolveInfo> results = queryIntentActivities(intent, resolvedType, flags, userId).getList();
        if (results != null && !results.isEmpty()) {
            return results.get(0);
        }
        return null;
    }

    /**
     * [BRIDGED] queryIntentActivities -> BMS.QueryAbilityInfos
     */
    @Override
    public ParceledListSlice<ResolveInfo> queryIntentActivities(Intent intent, String resolvedType,
                                                     long flags, int userId) {
        logBridged("queryIntentActivities", intent.toString());
        IntentWantConverter.WantParams want = IntentWantConverter.intentToWant(intent);
        String wantJson = buildWantJson(want);
        String json = nativeQueryAbilityInfos(wantJson, (int) flags);

        List<ResolveInfo> result = new ArrayList<>();
        if (json == null || json.isEmpty()) {
            return new ParceledListSlice<>(result);
        }

        try {
            org.json.JSONArray array = new org.json.JSONArray(json);
            for (int i = 0; i < array.length(); i++) {
                org.json.JSONObject abilityJson = array.getJSONObject(i);
                ResolveInfo ri = new ResolveInfo();
                ri.activityInfo = new ActivityInfo();
                String bundleName = abilityJson.optString("bundleName", "");
                ri.activityInfo.name = IntentWantConverter.abilityNameToClassName(
                    bundleName, abilityJson.getString("name"));
                ri.activityInfo.packageName = bundleName;
                ri.activityInfo.exported = abilityJson.optBoolean("visible", false);
                result.add(ri);
            }
        } catch (org.json.JSONException e) {
            Log.e(TAG, "queryIntentActivities: parse error", e);
        }

        return new ParceledListSlice<>(result);
    }

    // ========================================================================
    // Category 3: Permission Queries
    // ========================================================================

    /**
     * [BRIDGED] checkPermission -> AccessTokenKit.VerifyPermission
     */
    @Override
    public int checkPermission(String permName, String pkgName, int userId) {
        logBridged("checkPermission", permName + " for " + pkgName);
        String ohPermission = PermissionMapper.mapToOH(permName);
        return nativeCheckPermission(pkgName, ohPermission);
    }

    /**
     * [BRIDGED] getPackageUid -> BMS.GetUidByBundleName
     */
    @Override
    public int getPackageUid(String packageName, long flags, int userId) {
        logBridged("getPackageUid", packageName);
        if (SourcePackageRegistry.contains(packageName)) return android.os.Process.myUid();
        return nativeGetUidByBundleName(packageName);
    }

    /**
     * [STUB] checkSignatures
     */
    @Override
    public int checkSignatures(String pkg1, String pkg2, int userId) {
        logStub("checkSignatures", pkg1 + " vs " + pkg2);
        return 0;  // SIGNATURE_MATCH
    }

    // ========================================================================
    // Category 4: Feature Queries
    // ========================================================================

    /**
     * [STUB] hasSystemFeature
     */
    @Override
    public boolean hasSystemFeature(String name, int version) {
        logStub("hasSystemFeature", name);
        // Phase 1: report common features as available
        switch (name) {
            case "android.software.webview":
                return getSideloadedWebViewPackageInfo() != null;
            case "android.hardware.touchscreen":
            case "android.hardware.wifi":
            case "android.hardware.bluetooth":
            case "android.hardware.camera":
            case "android.hardware.screen.portrait":
            case "android.hardware.screen.landscape":
                return true;
            default:
                return false;
        }
    }

    // ========================================================================
    // Utility Methods
    // ========================================================================

    private String buildWantJson(IntentWantConverter.WantParams want) {
        StringBuilder sb = new StringBuilder("{");
        if (want.bundleName != null) {
            sb.append("\"bundleName\":\"").append(want.bundleName).append("\",");
        }
        if (want.abilityName != null) {
            sb.append("\"abilityName\":\"").append(want.abilityName).append("\",");
        }
        if (want.action != null) {
            sb.append("\"action\":\"").append(want.action).append("\",");
        }
        if (want.uri != null) {
            sb.append("\"uri\":\"").append(want.uri).append("\",");
        }
        // Remove trailing comma
        if (sb.charAt(sb.length() - 1) == ',') {
            sb.setLength(sb.length() - 1);
        }
        sb.append("}");
        return sb.toString();
    }

    // An app that asks the same question in a loop must not fill the log with the answer.
    // McDonald's polls getPackageInfo("com.android.vending") ~300 times a second while Play
    // services are absent: one run wrote 59k copies of each trace line and pushed every other
    // process's logs out of the hilog ring, which is where a diagnosis has to come from.
    private static final int TRACE_REPEAT_LIMIT = 3;
    private static final java.util.concurrent.ConcurrentHashMap<String, java.util.concurrent.atomic.AtomicInteger>
            sTraceCounts = new java.util.concurrent.ConcurrentHashMap<>();

    private static boolean traceAllowed(String key) {
        int seen = sTraceCounts.computeIfAbsent(key, unused -> new java.util.concurrent.atomic.AtomicInteger())
                .incrementAndGet();
        if (seen <= TRACE_REPEAT_LIMIT) {
            return true;
        }
        // One note per key that the rest are suppressed, then silence.
        return false;
    }

    private void logBridged(String method, String detail) {
        String key = "B:" + method + ":" + detail;
        if (traceAllowed(key)) {
            Log.d(TAG, "[BRIDGED] " + method + ": " + detail);
        } else if (sTraceCounts.get(key).get() == TRACE_REPEAT_LIMIT + 1) {
            Log.d(TAG, "[BRIDGED] " + method + ": " + detail + " (repeats suppressed)");
        }
    }

    private void logStub(String method, String detail) {
        String key = "S:" + method + ":" + detail;
        if (traceAllowed(key)) {
            Log.d(TAG, "[STUB] " + method + ": " + detail);
        } else if (sTraceCounts.get(key).get() == TRACE_REPEAT_LIMIT + 1) {
            Log.d(TAG, "[STUB] " + method + ": " + detail + " (repeats suppressed)");
        }
    }


    // === AUTO-GENERATED STUBS (generate_pma_stubs.py, 2026-04-11) ===
    // These satisfy IPackageManager.Stub's abstract contract. They log
    // and return safe defaults. Replace with [BRIDGED] OH BMS routing when
    // needed per use-case. Not runtime stubs in the 禁止用 stub 回避问题 sense —
    // these are Java compile-satisfaction stubs; the real work happens
    // at the adapter JNI layer in oh_bundle_mgr_client.cpp.

    @Override
    public void checkPackageStartable(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("checkPackageStartable", "");
    }


    @Override
    public boolean isPackageAvailable(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("isPackageAvailable", "");
        return false;
    }


    @Override
    public android.content.pm.PackageInfo getPackageInfoVersioned(android.content.pm.VersionedPackage arg0, long arg1, int arg2) throws android.os.RemoteException {
        logStub("getPackageInfoVersioned", "");
        return null;
    }


    @Override
    public int[] getPackageGids(java.lang.String arg0, long arg1, int arg2) throws android.os.RemoteException {
        logStub("getPackageGids", "");
        return null;
    }


    @Override
    public java.lang.String[] currentToCanonicalPackageNames(java.lang.String[] arg0) throws android.os.RemoteException {
        logStub("currentToCanonicalPackageNames", "");
        return null;
    }


    @Override
    public java.lang.String[] canonicalToCurrentPackageNames(java.lang.String[] arg0) throws android.os.RemoteException {
        logStub("canonicalToCurrentPackageNames", "");
        return null;
    }


    @Override
    public int getTargetSdkVersion(java.lang.String arg0) throws android.os.RemoteException {
        logStub("getTargetSdkVersion", "");
        return 0;
    }


    @Override
    public boolean activitySupportsIntentAsUser(android.content.ComponentName arg0, android.content.Intent arg1, java.lang.String arg2, int arg3) throws android.os.RemoteException {
        logStub("activitySupportsIntentAsUser", "");
        return false;
    }


    @Override
    public android.content.pm.ActivityInfo getReceiverInfo(android.content.ComponentName arg0, long arg1, int arg2) throws android.os.RemoteException {
        if (SourcePackageRegistry.contains(arg0.getPackageName())) {
            PackageInfo info = SourcePackageRegistry.packageInfo(arg0.getPackageName(),
                    arg1 | android.content.pm.PackageManager.GET_RECEIVERS);
            if (info != null && info.receivers != null) for (ActivityInfo receiver : info.receivers) {
                if (arg0.getClassName().equals(receiver.name)) return receiver;
            }
            return null;
        }
        logStub("getReceiverInfo", "");
        return null;
    }


    @Override
    public android.content.pm.ServiceInfo getServiceInfo(android.content.ComponentName arg0, long arg1, int arg2) throws android.os.RemoteException {
        logBridged("getServiceInfo", String.valueOf(arg0));
        if (SourcePackageRegistry.contains(arg0.getPackageName())) {
            PackageInfo info = SourcePackageRegistry.packageInfo(arg0.getPackageName(),
                    arg1 | android.content.pm.PackageManager.GET_SERVICES);
            if (info != null && info.services != null) for (ServiceInfo service : info.services) {
                if (arg0.getClassName().equals(service.name)) {
                    System.err.println("[SOURCE-PM] getServiceInfo HIT " + arg0.getClassName()
                            + " flags=0x" + Long.toHexString(arg1)
                            + " meta=" + (service.metaData == null ? -1 : service.metaData.size()));
                    return service;
                }
            }
            System.err.println("[SOURCE-PM] getServiceInfo MISS " + arg0
                    + " flags=0x" + Long.toHexString(arg1)
                    + " services=" + (info == null ? "null-info"
                            : (info.services == null ? "null" : String.valueOf(info.services.length))));
            return null;
        }
        return ServiceInfoResolver.resolve(arg0, arg1);
    }


    @Override
    public android.content.pm.ProviderInfo getProviderInfo(android.content.ComponentName arg0, long arg1, int arg2) throws android.os.RemoteException {
        logBridged("getProviderInfo", String.valueOf(arg0));
        if (SourcePackageRegistry.contains(arg0.getPackageName())) {
            PackageInfo info = SourcePackageRegistry.packageInfo(arg0.getPackageName(),
                    arg1 | android.content.pm.PackageManager.GET_PROVIDERS);
            if (info != null && info.providers != null) for (ProviderInfo provider : info.providers) {
                if (arg0.getClassName().equals(provider.name)) return provider;
            }
            return null;
        }
        return ProviderInfoResolver.resolve(arg0, arg1);
    }


    @Override
    public boolean isProtectedBroadcast(java.lang.String arg0) throws android.os.RemoteException {
        logStub("isProtectedBroadcast", "");
        return false;
    }


    @Override
    public int checkUidSignatures(int arg0, int arg1) throws android.os.RemoteException {
        logStub("checkUidSignatures", "");
        return 0;
    }


    @Override
    public java.util.List<java.lang.String> getAllPackages() throws android.os.RemoteException {
        logStub("getAllPackages", "");
        return null;
    }


    @Override
    public java.lang.String[] getPackagesForUid(int arg0) throws android.os.RemoteException {
        String sourcePackage = SourcePackageRegistry.packageForUid(arg0);
        if (sourcePackage != null) return new String[] {sourcePackage};
        logStub("getPackagesForUid", "");
        return null;
    }


    @Override
    public java.lang.String getNameForUid(int arg0) throws android.os.RemoteException {
        String sourcePackage = SourcePackageRegistry.packageForUid(arg0);
        if (sourcePackage != null) return sourcePackage;
        logStub("getNameForUid", "");
        return null;
    }


    @Override
    public java.lang.String[] getNamesForUids(int[] arg0) throws android.os.RemoteException {
        logStub("getNamesForUids", "");
        return null;
    }


    @Override
    public int getUidForSharedUser(java.lang.String arg0) throws android.os.RemoteException {
        logStub("getUidForSharedUser", "");
        return 0;
    }


    @Override
    public int getFlagsForUid(int arg0) throws android.os.RemoteException {
        logStub("getFlagsForUid", "");
        return 0;
    }


    @Override
    public int getPrivateFlagsForUid(int arg0) throws android.os.RemoteException {
        logStub("getPrivateFlagsForUid", "");
        return 0;
    }


    @Override
    public boolean isUidPrivileged(int arg0) throws android.os.RemoteException {
        logStub("isUidPrivileged", "");
        return false;
    }


    @Override
    public android.content.pm.ResolveInfo findPersistentPreferredActivity(android.content.Intent arg0, int arg1) throws android.os.RemoteException {
        logStub("findPersistentPreferredActivity", "");
        return null;
    }


    @Override
    public boolean canForwardTo(android.content.Intent arg0, java.lang.String arg1, int arg2, int arg3) throws android.os.RemoteException {
        logStub("canForwardTo", "");
        return false;
    }


    @Override
    public android.content.pm.ParceledListSlice queryIntentActivityOptions(android.content.ComponentName arg0, android.content.Intent[] arg1, java.lang.String[] arg2, android.content.Intent arg3, java.lang.String arg4, long arg5, int arg6) throws android.os.RemoteException {
        logStub("queryIntentActivityOptions", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice queryIntentReceivers(android.content.Intent arg0, java.lang.String arg1, long arg2, int arg3) throws android.os.RemoteException {
        logStub("queryIntentReceivers", "");
        return null;
    }


    @Override
    public android.content.pm.ResolveInfo resolveService(android.content.Intent arg0, java.lang.String arg1, long arg2, int arg3) throws android.os.RemoteException {
        logStub("resolveService", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice queryIntentServices(android.content.Intent arg0, java.lang.String arg1, long arg2, int arg3) throws android.os.RemoteException {
        logStub("queryIntentServices", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice queryIntentContentProviders(android.content.Intent arg0, java.lang.String arg1, long arg2, int arg3) throws android.os.RemoteException {
        logStub("queryIntentContentProviders", "");
        return null;
    }


    @Override
    public android.os.ParcelFileDescriptor getAppMetadataFd(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("getAppMetadataFd", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice getPackagesHoldingPermissions(java.lang.String[] arg0, long arg1, int arg2) throws android.os.RemoteException {
        logStub("getPackagesHoldingPermissions", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice getInstalledApplications(long arg0, int arg1) throws android.os.RemoteException {
        logStub("getInstalledApplications", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice getPersistentApplications(int arg0) throws android.os.RemoteException {
        logStub("getPersistentApplications", "");
        return null;
    }


    @Override
    public android.content.pm.ProviderInfo resolveContentProvider(java.lang.String arg0, long arg1, int arg2) throws android.os.RemoteException {
        logStub("resolveContentProvider", "");
        return null;
    }


    @Override
    public void querySyncProviders(java.util.List<java.lang.String> arg0, java.util.List<android.content.pm.ProviderInfo> arg1) throws android.os.RemoteException {
        logStub("querySyncProviders", "");
    }


    /**
     * [BRIDGED] queryContentProviders — 2026-04-30 P2-B real implementation.
     * Reads provider list from on-demand parsed APK manifest (via
     * nativeParseApkManifestJson). Result returned as ParceledListSlice<ProviderInfo>.
     *
     * Spec: doc/ability_manager_ipc_adapter_design.html §1.1.4.6
     *
     * @param processName  filter by ApplicationInfo.processName, or null = all
     * @param uid          UID filter (ignored — all providers belong to one UID)
     * @param flags        PackageManager.* flags (currently ignored)
     * @param metaDataKey  metadata filter (ignored)
     */
    @Override
    public android.content.pm.ParceledListSlice queryContentProviders(
            java.lang.String processName, int uid, long flags, java.lang.String metaDataKey)
            throws android.os.RemoteException {
        logBridged("queryContentProviders",
                "process=" + processName + " uid=" + uid + " flags=" + flags);
        java.util.List<android.content.pm.ProviderInfo> result = new java.util.ArrayList<>();
        try {
            // Resolve packageName from UID. The caller may pass uid or processName;
            // we walk all installed packages and pick by uid match.
            String packageName = null;
            String allBundlesJson = nativeGetAllBundleInfos(0);
            if (allBundlesJson != null && !allBundlesJson.isEmpty()) {
                org.json.JSONArray arr = new org.json.JSONArray(allBundlesJson);
                for (int i = 0; i < arr.length(); i++) {
                    String bundleJson = arr.getString(i);
                    org.json.JSONObject obj = new org.json.JSONObject(bundleJson);
                    int bundleUid = obj.optInt("uid", -1);
                    String name = obj.optString("name", "");
                    if (bundleUid == uid && !name.isEmpty()) {
                        packageName = name;
                        break;
                    }
                }
            }
            if (packageName == null) {
                Log.d(TAG, "queryContentProviders: no package found for uid=" + uid);
                return new android.content.pm.ParceledListSlice<>(result);
            }
            collectProvidersForPackage(packageName, processName, result);
        } catch (Throwable t) {
            Log.e(TAG, "queryContentProviders failed", t);
        }
        return new android.content.pm.ParceledListSlice<>(result);
    }

    /**
     * Public helper — collect ContentProvider list for a package by name.
     * Used by AppSchedulerBridge to populate AppBindData.providers at bindApplication.
     */
    public static java.util.List<android.content.pm.ProviderInfo>
            getProvidersForPackage(String packageName) {
        java.util.List<android.content.pm.ProviderInfo> out = new java.util.ArrayList<>();
        collectProvidersForPackage(packageName, null, out);
        return out;
    }

    private static void collectProvidersForPackage(String packageName,
            String processFilter,
            java.util.List<android.content.pm.ProviderInfo> out) {
        if (SourcePackageRegistry.contains(packageName)) {
            PackageInfo info = SourcePackageRegistry.packageInfo(packageName,
                    android.content.pm.PackageManager.GET_PROVIDERS
                    | android.content.pm.PackageManager.GET_META_DATA);
            if (info != null && info.providers != null) for (ProviderInfo provider : info.providers) {
                if (processFilter == null || processFilter.equals(provider.processName)) out.add(provider);
            }
            return;
        }
        String json = getApkManifestJson(packageName);
        if (json == null || json.isEmpty()) return;
        try {
            org.json.JSONObject manifest = new org.json.JSONObject(json);
            org.json.JSONArray provs = manifest.optJSONArray("providers");
            if (provs == null) return;

            // Build a single ApplicationInfo for all providers — they share
            // packageName/uid/dataDir from the host package.
            android.content.pm.ApplicationInfo appInfoTmpl =
                    new android.content.pm.ApplicationInfo();
            appInfoTmpl.packageName = packageName;
            appInfoTmpl.processName = packageName;
            appInfoTmpl.dataDir = "/data/data/" + packageName;
            appInfoTmpl.sourceDir =
                    "/system/app/" + packageName + "/" + packageName + ".apk";
            appInfoTmpl.publicSourceDir = appInfoTmpl.sourceDir;
            appInfoTmpl.enabled = true;

            for (int i = 0; i < provs.length(); i++) {
                org.json.JSONObject p = provs.getJSONObject(i);
                String procName = p.optString("processName", "");
                if (procName.isEmpty()) procName = packageName;
                if (processFilter != null && !processFilter.isEmpty()
                        && !processFilter.equals(procName)) {
                    continue;
                }
                android.content.pm.ProviderInfo pi = new android.content.pm.ProviderInfo();
                pi.name = p.optString("name", "");
                pi.packageName = packageName;
                pi.processName = procName;
                pi.authority = p.optString("authorities", "");
                pi.exported = p.optBoolean("exported", false);
                pi.readPermission = nullIfEmpty(p.optString("readPermission", ""));
                pi.writePermission = nullIfEmpty(p.optString("writePermission", ""));
                pi.grantUriPermissions = p.optBoolean("grantUriPermissions", false);
                pi.multiprocess = p.optBoolean("multiprocess", false);
                pi.initOrder = p.optInt("initOrder", 0);
                pi.applicationInfo = appInfoTmpl;
                pi.flags = 0;
                pi.enabled = true;
                org.json.JSONObject meta = p.optJSONObject("metaData");
                if (meta != null) {
                    android.os.Bundle bundle = new android.os.Bundle();
                    java.util.Iterator<String> keys = meta.keys();
                    while (keys.hasNext()) {
                        String key = keys.next();
                        bundle.putString(key, meta.optString(key, ""));
                    }
                    pi.metaData = bundle;
                }
                out.add(pi);
            }
            Log.i(TAG, "collectProvidersForPackage: " + packageName
                    + " -> " + out.size() + " providers");
        } catch (Throwable t) {
            Log.e(TAG, "collectProvidersForPackage failed for " + packageName, t);
        }
    }

    private static String nullIfEmpty(String s) {
        return (s == null || s.isEmpty()) ? null : s;
    }


    @Override
    public android.content.pm.InstrumentationInfo getInstrumentationInfoAsUser(android.content.ComponentName arg0, int arg1, int arg2) throws android.os.RemoteException {
        logStub("getInstrumentationInfoAsUser", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice queryInstrumentationAsUser(java.lang.String arg0, int arg1, int arg2) throws android.os.RemoteException {
        logStub("queryInstrumentationAsUser", "");
        return null;
    }


    @Override
    public void finishPackageInstall(int arg0, boolean arg1) throws android.os.RemoteException {
        logStub("finishPackageInstall", "");
    }


    @Override
    public void setInstallerPackageName(java.lang.String arg0, java.lang.String arg1) throws android.os.RemoteException {
        logStub("setInstallerPackageName", "");
    }


    @Override
    public void relinquishUpdateOwnership(java.lang.String arg0) throws android.os.RemoteException {
        logStub("relinquishUpdateOwnership", "");
    }


    @Override
    public void setApplicationCategoryHint(java.lang.String arg0, int arg1, java.lang.String arg2) throws android.os.RemoteException {
        logStub("setApplicationCategoryHint", "");
    }


    @Override
    public void deletePackageAsUser(java.lang.String arg0, int arg1, android.content.pm.IPackageDeleteObserver arg2, int arg3, int arg4) throws android.os.RemoteException {
        logStub("deletePackageAsUser", "");
    }


    @Override
    public void deletePackageVersioned(android.content.pm.VersionedPackage arg0, android.content.pm.IPackageDeleteObserver2 arg1, int arg2, int arg3) throws android.os.RemoteException {
        logStub("deletePackageVersioned", "");
    }


    @Override
    public void deleteExistingPackageAsUser(android.content.pm.VersionedPackage arg0, android.content.pm.IPackageDeleteObserver2 arg1, int arg2) throws android.os.RemoteException {
        logStub("deleteExistingPackageAsUser", "");
    }


    @Override
    public java.lang.String getInstallerPackageName(java.lang.String arg0) throws android.os.RemoteException {
        logStub("getInstallerPackageName", "");
        return null;
    }


    @Override
    public android.content.pm.InstallSourceInfo getInstallSourceInfo(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("getInstallSourceInfo", "");
        return null;
    }


    @Override
    public void resetApplicationPreferences(int arg0) throws android.os.RemoteException {
        logStub("resetApplicationPreferences", "");
    }


    @Override
    public android.content.pm.ResolveInfo getLastChosenActivity(android.content.Intent arg0, java.lang.String arg1, int arg2) throws android.os.RemoteException {
        logStub("getLastChosenActivity", "");
        return null;
    }


    @Override
    public void setLastChosenActivity(android.content.Intent arg0, java.lang.String arg1, int arg2, android.content.IntentFilter arg3, int arg4, android.content.ComponentName arg5) throws android.os.RemoteException {
        logStub("setLastChosenActivity", "");
    }


    @Override
    public void addPreferredActivity(android.content.IntentFilter arg0, int arg1, android.content.ComponentName[] arg2, android.content.ComponentName arg3, int arg4, boolean arg5) throws android.os.RemoteException {
        logStub("addPreferredActivity", "");
    }


    @Override
    public void replacePreferredActivity(android.content.IntentFilter arg0, int arg1, android.content.ComponentName[] arg2, android.content.ComponentName arg3, int arg4) throws android.os.RemoteException {
        logStub("replacePreferredActivity", "");
    }


    @Override
    public void clearPackagePreferredActivities(java.lang.String arg0) throws android.os.RemoteException {
        logStub("clearPackagePreferredActivities", "");
    }


    @Override
    public int getPreferredActivities(java.util.List<android.content.IntentFilter> arg0, java.util.List<android.content.ComponentName> arg1, java.lang.String arg2) throws android.os.RemoteException {
        logStub("getPreferredActivities", "");
        return 0;
    }


    @Override
    public void addPersistentPreferredActivity(android.content.IntentFilter arg0, android.content.ComponentName arg1, int arg2) throws android.os.RemoteException {
        logStub("addPersistentPreferredActivity", "");
    }


    @Override
    public void clearPackagePersistentPreferredActivities(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("clearPackagePersistentPreferredActivities", "");
    }


    @Override
    public void clearPersistentPreferredActivity(android.content.IntentFilter arg0, int arg1) throws android.os.RemoteException {
        logStub("clearPersistentPreferredActivity", "");
    }


    @Override
    public void addCrossProfileIntentFilter(android.content.IntentFilter arg0, java.lang.String arg1, int arg2, int arg3, int arg4) throws android.os.RemoteException {
        logStub("addCrossProfileIntentFilter", "");
    }


    @Override
    public boolean removeCrossProfileIntentFilter(android.content.IntentFilter arg0, java.lang.String arg1, int arg2, int arg3, int arg4) throws android.os.RemoteException {
        logStub("removeCrossProfileIntentFilter", "");
        return false;
    }


    @Override
    public void clearCrossProfileIntentFilters(int arg0, java.lang.String arg1) throws android.os.RemoteException {
        logStub("clearCrossProfileIntentFilters", "");
    }


    @Override
    public java.lang.String[] setDistractingPackageRestrictionsAsUser(java.lang.String[] arg0, int arg1, int arg2) throws android.os.RemoteException {
        logStub("setDistractingPackageRestrictionsAsUser", "");
        return null;
    }



    public java.lang.String[] setPackagesSuspendedAsUser(java.lang.String[] arg0, boolean arg1, android.os.PersistableBundle arg2, android.os.PersistableBundle arg3, android.content.pm.SuspendDialogInfo arg4, java.lang.String arg5, int arg6) throws android.os.RemoteException {
        logStub("setPackagesSuspendedAsUser", "");
        return null;
    }


    @Override
    public java.lang.String[] getUnsuspendablePackagesForUser(java.lang.String[] arg0, int arg1) throws android.os.RemoteException {
        logStub("getUnsuspendablePackagesForUser", "");
        return null;
    }


    @Override
    public boolean isPackageSuspendedForUser(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("isPackageSuspendedForUser", "");
        return false;
    }


    @Override
    public android.os.Bundle getSuspendedPackageAppExtras(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("getSuspendedPackageAppExtras", "");
        return null;
    }


    @Override
    public byte[] getPreferredActivityBackup(int arg0) throws android.os.RemoteException {
        logStub("getPreferredActivityBackup", "");
        return null;
    }


    @Override
    public void restorePreferredActivities(byte[] arg0, int arg1) throws android.os.RemoteException {
        logStub("restorePreferredActivities", "");
    }


    @Override
    public byte[] getDefaultAppsBackup(int arg0) throws android.os.RemoteException {
        logStub("getDefaultAppsBackup", "");
        return null;
    }


    @Override
    public void restoreDefaultApps(byte[] arg0, int arg1) throws android.os.RemoteException {
        logStub("restoreDefaultApps", "");
    }


    @Override
    public byte[] getDomainVerificationBackup(int arg0) throws android.os.RemoteException {
        logStub("getDomainVerificationBackup", "");
        return null;
    }


    @Override
    public void restoreDomainVerification(byte[] arg0, int arg1) throws android.os.RemoteException {
        logStub("restoreDomainVerification", "");
    }


    @Override
    public android.content.ComponentName getHomeActivities(java.util.List<android.content.pm.ResolveInfo> arg0) throws android.os.RemoteException {
        logStub("getHomeActivities", "");
        return null;
    }


    @Override
    public void setHomeActivity(android.content.ComponentName arg0, int arg1) throws android.os.RemoteException {
        logStub("setHomeActivity", "");
    }


    @Override
    public void overrideLabelAndIcon(android.content.ComponentName arg0, java.lang.String arg1, int arg2, int arg3) throws android.os.RemoteException {
        logStub("overrideLabelAndIcon", "");
    }


    @Override
    public void restoreLabelAndIcon(android.content.ComponentName arg0, int arg1) throws android.os.RemoteException {
        logStub("restoreLabelAndIcon", "");
    }


    @Override
    public void setComponentEnabledSetting(android.content.ComponentName arg0, int arg1, int arg2, int arg3, java.lang.String arg4) throws android.os.RemoteException {
        logStub("setComponentEnabledSetting", "");
    }


    @Override
    public void setComponentEnabledSettings(java.util.List<android.content.pm.PackageManager.ComponentEnabledSetting> arg0, int arg1, java.lang.String arg2) throws android.os.RemoteException {
        logStub("setComponentEnabledSettings", "");
    }


    @Override
    public int getComponentEnabledSetting(android.content.ComponentName arg0, int arg1) throws android.os.RemoteException {
        logStub("getComponentEnabledSetting", "");
        return 0;
    }


    @Override
    public void setApplicationEnabledSetting(java.lang.String arg0, int arg1, int arg2, int arg3, java.lang.String arg4) throws android.os.RemoteException {
        logStub("setApplicationEnabledSetting", "");
    }


    @Override
    public int getApplicationEnabledSetting(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("getApplicationEnabledSetting", "");
        return 0;
    }


    @Override
    public void logAppProcessStartIfNeeded(java.lang.String arg0, java.lang.String arg1, int arg2, java.lang.String arg3, java.lang.String arg4, int arg5) throws android.os.RemoteException {
        logStub("logAppProcessStartIfNeeded", "");
    }


    @Override
    public void flushPackageRestrictionsAsUser(int arg0) throws android.os.RemoteException {
        logStub("flushPackageRestrictionsAsUser", "");
    }


    @Override
    public void setPackageStoppedState(java.lang.String arg0, boolean arg1, int arg2) throws android.os.RemoteException {
        logStub("setPackageStoppedState", "");
    }


    @Override
    public void freeStorageAndNotify(java.lang.String arg0, long arg1, int arg2, android.content.pm.IPackageDataObserver arg3) throws android.os.RemoteException {
        logStub("freeStorageAndNotify", "");
    }


    @Override
    public void freeStorage(java.lang.String arg0, long arg1, int arg2, android.content.IntentSender arg3) throws android.os.RemoteException {
        logStub("freeStorage", "");
    }


    @Override
    public void deleteApplicationCacheFiles(java.lang.String arg0, android.content.pm.IPackageDataObserver arg1) throws android.os.RemoteException {
        logStub("deleteApplicationCacheFiles", "");
    }


    @Override
    public void deleteApplicationCacheFilesAsUser(java.lang.String arg0, int arg1, android.content.pm.IPackageDataObserver arg2) throws android.os.RemoteException {
        logStub("deleteApplicationCacheFilesAsUser", "");
    }


    @Override
    public void clearApplicationUserData(java.lang.String arg0, android.content.pm.IPackageDataObserver arg1, int arg2) throws android.os.RemoteException {
        logStub("clearApplicationUserData", "");
    }


    @Override
    public void clearApplicationProfileData(java.lang.String arg0) throws android.os.RemoteException {
        logStub("clearApplicationProfileData", "");
    }


    @Override
    public void getPackageSizeInfo(java.lang.String arg0, int arg1, android.content.pm.IPackageStatsObserver arg2) throws android.os.RemoteException {
        logStub("getPackageSizeInfo", "");
    }


    @Override
    public java.lang.String[] getSystemSharedLibraryNames() throws android.os.RemoteException {
        logStub("getSystemSharedLibraryNames", "");
        return null;
    }


    @Override
    public android.content.pm.ParceledListSlice getSystemAvailableFeatures() throws android.os.RemoteException {
        logStub("getSystemAvailableFeatures", "");
        return null;
    }


    @Override
    public java.util.List<java.lang.String> getInitialNonStoppedSystemPackages() throws android.os.RemoteException {
        logStub("getInitialNonStoppedSystemPackages", "");
        return null;
    }


    @Override
    public void enterSafeMode() throws android.os.RemoteException {
        logStub("enterSafeMode", "");
    }


    @Override
    public boolean isSafeMode() throws android.os.RemoteException {
        logStub("isSafeMode", "");
        return false;
    }


    @Override
    public boolean hasSystemUidErrors() throws android.os.RemoteException {
        logStub("hasSystemUidErrors", "");
        return false;
    }


    @Override
    public void notifyPackageUse(java.lang.String arg0, int arg1) throws android.os.RemoteException {
        logStub("notifyPackageUse", "");
    }


    @Override
    public void notifyDexLoad(java.lang.String arg0, java.util.Map<java.lang.String, java.lang.String> arg1, java.lang.String arg2) throws android.os.RemoteException {
        logStub("notifyDexLoad", "");
    }


    @Override
    public void registerDexModule(java.lang.String arg0, java.lang.String arg1, boolean arg2, android.content.pm.IDexModuleRegisterCallback arg3) throws android.os.RemoteException {
        logStub("registerDexModule", "");
    }


    @Override
    public boolean performDexOptMode(java.lang.String arg0, boolean arg1, java.lang.String arg2, boolean arg3, boolean arg4, java.lang.String arg5) throws android.os.RemoteException {
        logStub("performDexOptMode", "");
        return false;
    }


    @Override
    public boolean performDexOptSecondary(java.lang.String arg0, java.lang.String arg1, boolean arg2) throws android.os.RemoteException {
        logStub("performDexOptSecondary", "");
        return false;
    }


    @Override
    public int getMoveStatus(int arg0) throws android.os.RemoteException {
        logStub("getMoveStatus", "");
        return 0;
    }


    @Override
    public void registerMoveCallback(android.content.pm.IPackageMoveObserver arg0) throws android.os.RemoteException {
        logStub("registerMoveCallback", "");
    }


    @Override
    public void unregisterMoveCallback(android.content.pm.IPackageMoveObserver arg0) throws android.os.RemoteException {
        logStub("unregisterMoveCallback", "");
    }


    @Override
    public int movePackage(java.lang.String arg0, java.lang.String arg1) throws android.os.RemoteException {
        logStub("movePackage", "");
        return 0;
    }


    @Override
    public int movePrimaryStorage(java.lang.String arg0) throws android.os.RemoteException {
        logStub("movePrimaryStorage", "");
        return 0;
    }


    @Override
    public boolean setInstallLocation(int arg0) throws android.os.RemoteException {
        logStub("setInstallLocation", "");
        return false;
    }


    @Override
    public int getInstallLocation() throws android.os.RemoteException {
        logStub("getInstallLocation", "");
        return 0;
    }


    @Override
    public int installExistingPackageAsUser(java.lang.String arg0, int arg1, int arg2, int arg3, java.util.List<java.lang.String> arg4) throws android.os.RemoteException {
        logStub("installExistingPackageAsUser", "");
        return 0;
    }


    @Override
    public void verifyPendingInstall(int arg0, int arg1) throws android.os.RemoteException {
        logStub("verifyPendingInstall", "");
    }


    @Override
    public void extendVerificationTimeout(int arg0, int arg1, long arg2) throws android.os.RemoteException {
        logStub("extendVerificationTimeout", "");
    }


    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public java.lang.String[] setPackagesSuspendedAsUser(java.lang.String[] packageNames, boolean suspended, android.os.PersistableBundle appExtras, android.os.PersistableBundle launcherExtras, android.content.pm.SuspendDialogInfo dialogInfo, int flags, java.lang.String suspendingPackage, int suspendingUserId, int targetUserId) throws android.os.RemoteException {
        throw new SecurityException("Package suspension requires the OH bundle manager");
    }

    @Override
    public boolean isPackageQuarantinedForUser(java.lang.String packageName, int userId) throws android.os.RemoteException {
        return false;
    }

    @Override
    public boolean isPackageStoppedForUser(java.lang.String packageName, int userId) throws android.os.RemoteException {
        ApplicationInfo info = getApplicationInfo(packageName, 0, userId);
        return info != null && (info.flags & ApplicationInfo.FLAG_STOPPED) != 0;
    }

    @Override
    public java.lang.String getSuspendingPackage(java.lang.String packageName, int userId) throws android.os.RemoteException {
        return null;
    }

    @Override
    public java.util.Map<java.lang.String,java.lang.String> getSystemSharedLibraryNamesAndPaths() throws android.os.RemoteException {
        return java.util.Collections.emptyMap();
    }

    @Override
    public int getUserMinAspectRatio(java.lang.String packageName, int userId) throws android.os.RemoteException {
        return 0;
    }

    @Override
    public void setUserMinAspectRatio(java.lang.String packageName, int userId, int aspectRatio) throws android.os.RemoteException {
        throw new UnsupportedOperationException("OpenHarmony user aspect-ratio policy is not connected");
    }

    @Override
    public void registerPackageMonitorCallback(android.os.IRemoteCallback callback, int userId) throws android.os.RemoteException {
        throw new UnsupportedOperationException("OpenHarmony package change subscription is not connected");
    }

    @Override
    public void unregisterPackageMonitorCallback(android.os.IRemoteCallback callback) throws android.os.RemoteException {
        logStub("unregisterPackageMonitorCallback", "no OH subscription");
    }

    @Override
    public android.content.pm.ArchivedPackageParcel getArchivedPackage(java.lang.String packageName, int userId) throws android.os.RemoteException {
        return null;
    }

    @Override
    public android.graphics.Bitmap getArchivedAppIcon(java.lang.String packageName, android.os.UserHandle user, java.lang.String callingPackageName) throws android.os.RemoteException {
        return null;
    }

    @Override
    public boolean isAppArchivable(java.lang.String packageName, android.os.UserHandle user) throws android.os.RemoteException {
        return false;
    }

    @Override
    public int getAppMetadataSource(java.lang.String packageName, int userId) throws android.os.RemoteException {
        return 0;
    }

    @Override
    public android.content.ComponentName getDomainVerificationAgent(int userId) throws android.os.RemoteException {
        return null;
    }
}

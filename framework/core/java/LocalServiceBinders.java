package adapter.core;

import android.content.ClipData;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * System services with no OH service to route to, answered in process with what an idle,
 * ordinary phone reports.
 *
 * Without a binder, SystemServiceRegistry's fetcher throws ServiceNotFoundException and
 * getSystemService returns null. Java callers often check; Kotlin's {@code as UiModeManager} does
 * not, and inside a React Native host function the throw becomes a JS exception: Burger King's
 * expo-device reads the UI mode type while its JS bundle loads, and the null took down the app's
 * root component. The same app casts PowerManager, AlarmManager and ClipboardManager the same way.
 *
 * Same shape as LocationManagerAdapter: a Proxy over the AIDL interface attached to a local
 * Binder, so Stub.asInterface resolves it without a transaction; methods not listed answer the
 * type default. Nothing here reaches OH: alarms are accepted and never delivered, the clipboard is
 * local to the process, and power reports an interactive device without power saving.
 */
public final class LocalServiceBinders {
    private static final String TAG = "WESTLAKE-LOCAL-SERVICE";
    private static final Object DEFAULT = new Object();
    private static final Map<String, IBinder> sBinders = new HashMap<>();
    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());
    private static volatile ClipData sClip;

    private interface Answers {
        Object answer(String method, Object[] args);
    }

    private LocalServiceBinders() {}

    /** The binder for {@code name}, or null when this class does not answer that service. */
    public static synchronized IBinder get(String name) {
        IBinder binder = sBinders.get(name);
        if (binder != null) return binder;
        try {
            switch (name) {
                case "uimode":
                    binder = proxy(name, "android.app.IUiModeManager", LocalServiceBinders::uiMode);
                    break;
                case "power":
                    binder = proxy(name, "android.os.IPowerManager", LocalServiceBinders::power);
                    break;
                case "thermalservice": {
                    // PowerManager's fetcher also requires the thermal service.
                    westlake.impl.IThermalServiceImpl thermal = new westlake.impl.IThermalServiceImpl();
                    thermal.attachInterface(thermal, "android.os.IThermalService");
                    binder = thermal;
                    break;
                }
                case "alarm":
                    binder = proxy(name, "android.app.IAlarmManager", LocalServiceBinders::alarm);
                    break;
                case "clipboard":
                    binder = proxy(name, "android.content.IClipboard", LocalServiceBinders::clipboard);
                    break;
                case "account":
                    binder = proxy(name, "android.accounts.IAccountManager", LocalServiceBinders::account);
                    break;
                case "audio":
                    binder = proxy(name, "android.media.IAudioService", LocalServiceBinders::audio);
                    break;
                case "appops":
                    binder = proxy(name, "com.android.internal.app.IAppOpsService",
                            LocalServiceBinders::appOps);
                    break;
                case "locale":
                    binder = proxy(name, "android.app.ILocaleManager", LocalServiceBinders::locale);
                    break;
                case "appwidget":
                    binder = proxy(name, "com.android.internal.appwidget.IAppWidgetService",
                            LocalServiceBinders::appWidget);
                    break;
                case "deviceidle":
                    binder = proxy(name, "android.os.IDeviceIdleController", LocalServiceBinders::deviceIdle);
                    break;
                case "search":
                    binder = proxy(name, "android.app.ISearchManager", LocalServiceBinders::search);
                    break;
                case "domain_verification":
                    binder = proxy(name, "android.content.pm.verify.domain.IDomainVerificationManager",
                            LocalServiceBinders::domainVerification);
                    break;
                case "role":
                    binder = proxy(name, "android.app.role.IRoleManager", LocalServiceBinders::role);
                    break;
                case "uri_grants":
                    binder = proxy(name, "android.app.IUriGrantsManager", LocalServiceBinders::uriGrants);
                    break;
                case "trust":
                    binder = proxy(name, "android.app.trust.ITrustManager", LocalServiceBinders::trust);
                    break;
                case "media_router":
                    binder = proxy(name, "android.media.IMediaRouterService", LocalServiceBinders::mediaRouter);
                    break;
                case "wallpaper":
                    binder = proxy(name, "android.app.IWallpaperManager", LocalServiceBinders::wallpaper);
                    break;
                case "wifi":
                    binder = proxy(name, "android.net.wifi.IWifiManager", LocalServiceBinders::wifi);
                    break;
                // TrafficStats.init (run at bind) takes its binder from NetworkStatsManager, which
                // needs this service; without it every TrafficStats call threw "TrafficStats not
                // initialized" (Waze, Shazam). Stats are not counted here, so every query answers
                // UNSUPPORTED, as on a device that does not count traffic.
                case "netstats":
                    binder = proxy(name, "android.net.INetworkStatsService", LocalServiceBinders::netStats);
                    break;
                // TelephonyManager.listen registers through the telephony registry; a null registry
                // threw a NullPointerException (Waze). Registration succeeds and no event arrives,
                // which is what a device without telephony changes delivers.
                case "telephony.registry":
                    binder = proxy(name, "com.android.internal.telephony.ITelephonyRegistry",
                            LocalServiceBinders::telephonyRegistry);
                    break;
                case "notification":
                    binder = proxy(name, "android.app.INotificationManager",
                            LocalServiceBinders::notification);
                    break;
                // BatteryManager's fetcher requires two services and throws if either is missing,
                // so registering one of them changes nothing.
                case "batterystats":
                    binder = proxy(name, "com.android.internal.app.IBatteryStats",
                            LocalServiceBinders::batteryStats);
                    break;
                case "batteryproperties":
                    binder = proxy(name, "android.os.IBatteryPropertiesRegistrar",
                            LocalServiceBinders::batteryProperties);
                    break;
                default:
                    return null;
            }
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] " + name + " unavailable: " + t);
            return null;
        }
        sBinders.put(name, binder);
        System.err.println("[" + TAG + "] " + name + " bound in process");
        return binder;
    }

    private static IBinder proxy(String name, String descriptor, Answers answers) throws ClassNotFoundException {
        Class<?> iface = Class.forName(descriptor);
        Binder binder = new Binder();
        Object local = Proxy.newProxyInstance(LocalServiceBinders.class.getClassLoader(), new Class<?>[] {iface},
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object self, Method method, Object[] args) {
                        String call = method.getName();
                        if ("asBinder".equals(call)) return binder;
                        if (sSeen.add(name + "." + call)) {
                            System.err.println("[" + TAG + "] " + name + "." + call);
                        }
                        Object value = answers.answer(call, args);
                        return value == DEFAULT ? defaultValue(method.getReturnType()) : value;
                    }
                });
        binder.attachInterface((IInterface) local, descriptor);
        return binder;
    }

    private static Object uiMode(String method, Object[] args) {
        switch (method) {
            case "getCurrentModeType":
                return Configuration.UI_MODE_TYPE_NORMAL;
            case "getNightMode": {
                int night = Resources.getSystem().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
                return night == Configuration.UI_MODE_NIGHT_YES ? 2 /* MODE_NIGHT_YES */ : 1 /* MODE_NIGHT_NO */;
            }
            case "getNightModeCustomType":
                return -1;     // MODE_NIGHT_CUSTOM_TYPE_UNKNOWN
            case "getAttentionModeThemeOverlay":
                return 1000;   // MODE_ATTENTION_THEME_OVERLAY_OFF
            case "getProjectingPackages":
                return new ArrayList<String>();
            default:
                return DEFAULT;   // car mode off, nothing locked, standard contrast (0f)
        }
    }

    private static Object power(String method, Object[] args) {
        switch (method) {
            case "isInteractive":
            case "isDisplayInteractive":
            case "isWakeLockLevelSupported":
                return Boolean.TRUE;
            default:
                return DEFAULT;   // no power save, not idle; wake locks are accepted
        }
    }

    private static Object alarm(String method, Object[] args) {
        return DEFAULT;   // set() is accepted, nothing is ever delivered; no next alarm clock
    }

    /**
     * OONI Probe calls setOverrideLocaleConfig while its Application is constructed and does not
     * null-check the manager, so the missing binder took the whole binding down before any
     * activity started. AndroidX does the same through AppLocalesMetadataHolderService.
     *
     * Per-app locale overrides are stored by the real service and read back later; nothing here
     * stores anything, so a set is accepted and a get reports no override. That is a truthful
     * answer for a board with no per-app locale database, not a placeholder -- an app that sets
     * an override and reads it back will see it absent, which is the same as a user never having
     * chosen one.
     */
    private static Object locale(String method, Object[] args) {
        switch (method) {
            case "getSystemLocales":
                return Resources.getSystem().getConfiguration().getLocales();
            case "getApplicationLocales":
                return android.os.LocaleList.getEmptyLocaleList();
            default:
                // setApplicationLocales / setOverrideLocaleConfig accepted and dropped;
                // getOverrideLocaleConfig answers null -- no override was ever recorded.
                return DEFAULT;
        }
    }

    /**
     * A launcher with no widgets placed: no widget ids, no providers bound, updates accepted and
     * dropped. Catima refreshes its home-screen widget from MainActivity.onResume.
     */
    private static Object appWidget(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * Roles on a board with no telephony: no role is available (so none is offered to the user)
     * and none is held. An SMS or dialer app sees that it is not the default and cannot become it.
     */
    private static Object role(String method, Object[] args) {
        return DEFAULT;   // isRoleAvailable/isRoleHeld false, getRoleHolders empty
    }

    /**
     * No persisted URI permissions: an app that never took one sees an empty list, and a take or
     * release is accepted. Nothing crosses to OH's own URI permission service.
     */
    private static Object uriGrants(String method, Object[] args) {
        return DEFAULT;
    }

    /** No trust agents and no secure lock: listeners register and nothing is ever trusted. */
    private static Object trust(String method, Object[] args) {
        return DEFAULT;
    }

    /** No remote routes: clients register and discover nothing; playback stays on the device. */
    private static Object mediaRouter(String method, Object[] args) {
        return DEFAULT;
    }

    /** No wallpaper and no wallpaper colours: the framework's nullable answers. */
    private static Object wallpaper(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * Wi-Fi as this process can see it through Android's interface: nothing, because OH's
     * wifi manager is not bridged. isWifiEnabled answers false and scans are empty; an app that
     * needs Wi-Fi state learns it is unavailable rather than crashing on a null manager.
     */
    private static Object wifi(String method, Object[] args) {
        // A null connection info NPE'd NYTimes' startup initializer. AOSP's default WifiInfo is
        // "not connected": unknown SSID, network id -1, invalid RSSI.
        if ("getConnectionInfo".equals(method)) {
            try {
                java.lang.reflect.Constructor<?> c = Class.forName("android.net.wifi.WifiInfo").getDeclaredConstructor();
                c.setAccessible(true);
                return c.newInstance();
            } catch (Throwable t) {
                return DEFAULT;
            }
        }
        return DEFAULT;
    }

    private static Object netStats(String method, Object[] args) {
        // getUidStats / getIfaceStats / getTotalStats return long: TrafficStats.UNSUPPORTED.
        if (method.endsWith("Stats")) return Long.valueOf(-1L);
        return DEFAULT;
    }

    private static Object telephonyRegistry(String method, Object[] args) {
        return DEFAULT;
    }

    /** No searchable activities registered system-wide; an app's own search UI is unaffected. */
    private static Object search(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * An app's link handling as a fresh install sees it: allowed, every declared web host
     * listed, none verified.
     * getDomainVerificationUserState is nullable in the API, but Aurora Store's settings read it
     * without checking; Android always answers for the calling package itself.
     */
    private static Object domainVerification(String method, Object[] args) {
        if ("getDomainVerificationUserState".equals(method) && args != null && args.length > 0
                && args[0] instanceof String) {
            String pkg = (String) args[0];
            // Every web host the manifest declares, none verified or selected (STATE_NONE, 0).
            Map<String, Integer> hosts = new HashMap<>();
            for (String host : adapter.packagemanager.SourcePackageRegistry.webHosts()) hosts.put(host, 0);
            return new android.content.pm.verify.domain.DomainVerificationUserState(
                    java.util.UUID.nameUUIDFromBytes(pkg.getBytes()), pkg,
                    android.os.Process.myUserHandle(), true, hosts);
        }
        return DEFAULT;
    }

    /**
     * Doze with no exemptions granted: no app is on the power-save allowlist, which is what a newly
     * installed app sees on Android. DAVx5 asks whether it is exempt before its intro screen.
     */
    private static Object deviceIdle(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * PackageManager.getPackageInstaller(): PackageInstaller's constructor rejects a null
     * IPackageInstaller, so the null the adapter used to return threw inside the framework (Aurora
     * Store's Application.onCreate). This installer has no sessions, and installing is refused
     * with a reason: this platform installs nothing on an app's behalf.
     */
    public static synchronized IInterface packageInstaller() {
        IBinder binder = sBinders.get("package-installer");
        if (binder == null) {
            try {
                binder = proxy("package-installer", "android.content.pm.IPackageInstaller", (method, args) -> {
                    if ("createSession".equals(method)) {
                        throw new IllegalStateException("package installation is not available on this platform");
                    }
                    return DEFAULT;
                });
            } catch (ClassNotFoundException e) {
                return null;
            }
            sBinders.put("package-installer", binder);
        }
        return binder.queryLocalInterface("android.content.pm.IPackageInstaller");
    }

    /**
     * Tusky's Application.onCreate reads its notification channels; NotificationManager unwraps the
     * ParceledListSlice the service returns, and the plain Binder that used to stand in for the
     * service decoded every reply to null, so the NPE landed inside the framework, during bind.
     *
     * No notification reaches OH through this interface. Posts and channel changes are accepted
     * and dropped; queries answer what a service with nothing stored reports: no channels, no
     * active notifications, notifications not enabled (targetSdk 33+ apps start without
     * POST_NOTIFICATIONS), zen mode off.
     */
    private static Object notification(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * Every SoundPool, MediaPlayer and AudioTrack is a PlayerBase, and PlayerBase registers itself
     * with IAudioService.trackPlayer through its OWN cached copy of the service, fetched from
     * ServiceManager. AppSpawnXInit stamps a stub into AudioManager's cache only, so that path
     * still got null: Termux's TermuxActivity.onResume builds a SoundPool for its bell and died on
     * the NPE. Answering the name here gives every cache the same binder.
     *
     * Nothing reaches OH audio through this interface; actual playback is the SoundPool/AVPlayer
     * backend's business. So it answers as the stub it replaces did, with defaults: no players
     * tracked, no sound effects, nothing muted. trackPlayer's 0 is a player id the caller only
     * hands back later.
     */
    private static Object audio(String method, Object[] args) {
        return DEFAULT;
    }

    /**
     * The app-ops service, answered as a phone with no user overrides would answer it.
     *
     * Asked by every app in the corpus and survived by most -- which is why it looked harmless --
     * but AnkiDroid's DeckPicker.onCreate reaches Environment.isExternalStorageLegacy, which calls
     * AppOpsManager unconditionally, and the null manager killed the activity.
     *
     * Each op answers its platform default (AppOpsManager.opToDefaultMode). Where that default is
     * MODE_DEFAULT, callers fall back to the permission check, which PermissionManagerAdapter
     * answers. OP_LEGACY_STORAGE is the exception: on a real device StorageManagerService sets it
     * per app, allowed only below target SDK 29, or at 29 with requestLegacyExternalStorage. An
     * app told it has legacy storage when it does not goes looking for shared storage it cannot
     * use, so that one is decided here the same way.
     *
     * note/start return a SyncNotedAppOp rather than a mode; null there NPEs inside AppOpsManager.
     */
    private static Object appOps(String method, Object[] args) {
        Integer code = firstInt(args);
        if (code == null) return DEFAULT;
        int mode = appOpMode(code);
        switch (method) {
            case "checkOperation":
            case "checkOperationRaw":
            case "checkAudioOperation":
                return mode;
            case "noteOperation":
            case "startOperation":
            case "noteProxyOperation":
            case "startProxyOperation":
            case "startProxyOperationWithState":
                return new android.app.SyncNotedAppOp(mode, code, null,
                        android.app.ActivityThread.currentPackageName());
            default:
                return DEFAULT;   // watchers accepted, no history, checkPackage allowed
        }
    }

    private static final int OP_LEGACY_STORAGE = 87;

    private static int appOpMode(int code) {
        if (code == OP_LEGACY_STORAGE) {
            android.app.Application app = android.app.ActivityThread.currentApplication();
            android.content.pm.ApplicationInfo info = app != null ? app.getApplicationInfo() : null;
            if (info == null) return android.app.AppOpsManager.MODE_IGNORED;
            boolean legacy = info.targetSdkVersion < android.os.Build.VERSION_CODES.Q
                    || (info.targetSdkVersion == android.os.Build.VERSION_CODES.Q
                        && info.hasRequestedLegacyExternalStorage());
            return legacy ? android.app.AppOpsManager.MODE_ALLOWED
                    : android.app.AppOpsManager.MODE_IGNORED;
        }
        try {
            return android.app.AppOpsManager.opToDefaultMode(code);
        } catch (RuntimeException unknownOp) {
            return android.app.AppOpsManager.MODE_DEFAULT;
        }
    }

    private static Integer firstInt(Object[] args) {
        if (args == null) return null;
        for (Object arg : args) if (arg instanceof Integer) return (Integer) arg;
        return null;
    }

    private static Object batteryStats(String method, Object[] args) {
        // Only needed so BatteryManager's fetcher can be satisfied; nothing reads it here.
        return DEFAULT;
    }

    /**
     * NewPipe reads BatteryManager.getIntProperty while starting its main activity and does not
     * null-check the manager either.
     *
     * The values are those of an idle, fully charged phone, matching what the rest of this class
     * reports. They are invented rather than read from OH: the board does expose battery state,
     * and wiring that through is worth doing, but a plausible constant is enough to stop an app
     * that only wants to know whether it should throttle background work.
     */
    private static Object batteryProperties(String method, Object[] args) {
        if (!"getProperty".equals(method) || args == null || args.length < 2) {
            return DEFAULT;   // scheduleUpdate is oneway and has nothing to schedule
        }
        final long value;
        switch ((Integer) args[0]) {
            case 1:  value = 3_000_000L; break;   // CHARGE_COUNTER, uAh
            case 2:                               // CURRENT_NOW, uA -- idle, neither in nor out
            case 3:  value = 0L; break;           // CURRENT_AVERAGE
            case 4:  value = 100L; break;         // CAPACITY, percent
            case 6:  value = 5L; break;           // STATUS = BATTERY_STATUS_FULL
            default:
                // Unsupported ids are a real answer on real hardware: a non-zero status makes
                // getIntProperty return Integer.MIN_VALUE, which callers already handle.
                return 1;
        }
        ((android.os.BatteryProperty) args[1]).setLong(value);
        return 0;
    }

    private static Object account(String method, Object[] args) {
        // There is no account database on the board and no authenticator to add one to, so the
        // truthful answer is "signed in nowhere": an empty account list, no features, no token.
        // Wikipedia asks through AccountManager.get(context) in BaseActivity.onCreate and never
        // null-checks the manager, so the binder has to exist even though it owns nothing.
        switch (method) {
            case "addAccountExplicitly":
            case "removeAccountExplicitly":
            case "hasFeatures":
            case "someUserHasAccount":
            case "accountAuthenticated":
                return Boolean.FALSE;
            default:
                return DEFAULT;   // arrays come back empty, everything else null or zero
        }
    }

    private static Object clipboard(String method, Object[] args) {
        ClipData clip = sClip;
        switch (method) {
            case "setPrimaryClip":
            case "setPrimaryClipAsPackage":
                sClip = args != null && args.length > 0 && args[0] instanceof ClipData ? (ClipData) args[0] : null;
                return DEFAULT;
            case "clearPrimaryClip":
                sClip = null;
                return DEFAULT;
            case "getPrimaryClip":
                return clip;
            case "getPrimaryClipDescription":
                return clip != null ? clip.getDescription() : null;
            case "hasPrimaryClip":
                return clip != null;
            case "hasClipboardText":
                return clip != null && clip.getItemCount() > 0 && clip.getItemAt(0).getText() != null;
            default:
                return DEFAULT;
        }
    }

    private static Object defaultValue(Class<?> type) {
        // An empty array, never null: callers iterate what a service hands back without checking,
        // and a null Account[] from getAccounts() throws inside the caller rather than here.
        if (type != null && type.isArray()) {
            return java.lang.reflect.Array.newInstance(type.getComponentType(), 0);
        }
        // Collections likewise: an empty one is what a service with nothing to report returns, and
        // a null List of audio devices or playback configurations throws in the caller's loop.
        if (type == java.util.List.class) return new ArrayList<>();
        if (type == java.util.Map.class) return new HashMap<>();
        if (type == java.util.Set.class) return new HashSet<>();
        // AIDL list results travel as a ParceledListSlice, which the manager unwraps with getList()
        // before the app sees anything: a null throws inside the framework.
        if (type == android.content.pm.ParceledListSlice.class) {
            return android.content.pm.ParceledListSlice.emptyList();
        }
        if (type == null || !type.isPrimitive()) return null;
        if (type == boolean.class) return Boolean.FALSE;
        if (type == byte.class) return (byte) 0;
        if (type == short.class) return (short) 0;
        if (type == char.class) return (char) 0;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0f;
        if (type == double.class) return 0d;
        return null;   // void
    }
}

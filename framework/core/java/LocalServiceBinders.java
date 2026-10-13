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
    static final Object DEFAULT = new Object();
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
                case "device_identifiers":
                    binder = proxy(name, "android.os.IDeviceIdentifiersPolicyService",
                            LocalServiceBinders::deviceIdentifiers);
                    break;
                case "launcherapps":
                    binder = proxy(name, "android.content.pm.ILauncherApps", LocalServiceBinders::launcherApps);
                    break;
                case "vpn_management":
                    binder = proxy(name, "android.net.IVpnManager", LocalServiceBinders::vpnManager);
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
                case "device_policy":
                    binder = proxy(name, "android.app.admin.IDevicePolicyManager",
                            LocalServiceBinders::devicePolicy);
                    break;
                case "servicediscovery":
                    binder = proxy(name, "android.net.nsd.INsdManager", LocalServiceBinders::nsd);
                    break;
                case "usagestats":
                    binder = proxy(name, "android.app.usage.IUsageStatsManager", LocalServiceBinders::usageStats);
                    break;
                // No USB device or accessory attached: the device list is empty, nothing to open
                // or ask permission for. OH's USB service is not bridged.
                case "usb":
                    binder = proxy(name, "android.hardware.usb.IUsbManager", (method, args) -> DEFAULT);
                    break;
                case "storagestats":
                    binder = proxy(name, "android.app.usage.IStorageStatsManager", LocalServiceBinders::storageStats);
                    break;
                // Bluetooth present and switched off. With no manager service BluetoothAdapter is
                // null, which Android gives only on hardware without Bluetooth, and apps use it
                // unchecked: aat's BLE sensors threw "getAdapter(...) must not be null" while its
                // tracker service started. registerAdapter answers no IBluetooth, so the adapter
                // reports STATE_OFF, isEnabled() false and no LE scanner, and enable() is refused --
                // a phone whose user has Bluetooth off. OH's Bluetooth service is not bridged.
                case "bluetooth_manager":
                    binder = proxy(name, "android.bluetooth.IBluetoothManager", (method, args) -> DEFAULT);
                    break;
                // TileService.requestListeningState, which an app with a Quick Settings tile calls
                // when its state changes, reaches StatusBarManager with no service behind it: a
                // NullPointerException on the main thread (Chrono, starting a timer). OH's status
                // bar is not bridged, so requests are accepted and nothing changes on screen.
                case "statusbar":
                    binder = proxy(name, "com.android.internal.statusbar.IStatusBarService",
                            LocalServiceBinders::statusBar);
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

    private static Object statusBar(String method, Object[] args) {
        if ("getDisableFlags".equals(method)) return new int[2];   // nothing disabled
        return DEFAULT;
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

    /**
     * Device identifiers, refused as Android refuses an app without READ_PRIVILEGED_PHONE_STATE:
     * Build.getSerial throws SecurityException, which callers catch (a device-info plugin answers
     * "unknown"). With no service it threw NullPointerException instead, and wormhole2's plugin
     * call failed before its first screen.
     */
    private static Object deviceIdentifiers(String method, Object[] args) {
        if (method.startsWith("getSerial")) {
            throw new SecurityException(method + ": The uid " + android.os.Process.myUid()
                    + " does not meet the requirements to access device identifiers.");
        }
        return DEFAULT;
    }

    /**
     * VPN management, as on a device where the user has not yet consented to this app's VPN:
     * prepareVpn answers false, so VpnService.prepare hands the app the consent Intent (which no
     * dialog answers here), and every other call answers its type's default (no VPN established,
     * not always-on). VpnService fetches this binder itself, and with no service its prepare
     * dereferenced null: socks5 died in its first activity's onCreate, blockads's VPN thread at start.
     */
    private static Object vpnManager(String method, Object[] args) {
        return "prepareVpn".equals(method) ? Boolean.FALSE : DEFAULT;
    }

    /**
     * The launcher's view of this device: one user, whose own profile group it is, and the
     * activities the package manager resolves for MAIN/LAUNCHER with their application (this
     * process knows its own package; OH abilities come without one, and a launcher loads each
     * entry's label and icon from it). No shortcut host permission is held, as for any launcher
     * that is not the default; nothing is installing; starting another application's activity is
     * accepted and has no effect.
     */
    private static Object launcherApps(String method, Object[] args) {
        switch (method) {
            case "getUserProfiles": {
                ArrayList<android.os.UserHandle> users = new ArrayList<>();
                users.add(android.os.Process.myUserHandle());
                return users;
            }
            case "getLauncherActivities":
                return new android.content.pm.ParceledListSlice<>(
                        launcherActivities((String) args[1], (android.os.UserHandle) args[2]));
            case "resolveLauncherActivityInternal": {
                android.content.pm.ActivityInfo info = activityInfo((android.content.ComponentName) args[1]);
                return info == null ? null : new android.content.pm.LauncherActivityInfoInternal(info,
                        new android.content.pm.IncrementalStatesInfo(false, 1f, 0L), (android.os.UserHandle) args[2]);
            }
            case "isPackageEnabled":
                return applicationInfo((String) args[1]) != null;
            case "isActivityEnabled":
                return activityInfo((android.content.ComponentName) args[1]) != null;
            case "getApplicationInfo":
                return applicationInfo((String) args[1]);
            case "getLauncherUserInfo":
                return new android.content.pm.LauncherUserInfo.Builder(
                        android.os.UserManager.USER_TYPE_FULL_SYSTEM, 0).build();
            default:
                return DEFAULT;
        }
    }

    private static android.content.pm.PackageManager packageManager() {
        android.app.Application application = android.app.ActivityThread.currentApplication();
        return application != null ? application.getPackageManager() : null;
    }

    private static java.util.List<android.content.pm.LauncherActivityInfoInternal> launcherActivities(
            String packageName, android.os.UserHandle user) {
        ArrayList<android.content.pm.LauncherActivityInfoInternal> found = new ArrayList<>();
        android.content.pm.PackageManager pm = packageManager();
        if (pm == null) return found;
        android.content.Intent main = new android.content.Intent(android.content.Intent.ACTION_MAIN)
                .addCategory(android.content.Intent.CATEGORY_LAUNCHER);
        if (packageName != null) main.setPackage(packageName);
        for (android.content.pm.ResolveInfo info : pm.queryIntentActivities(main, 0)) {
            if (info.activityInfo == null || info.activityInfo.applicationInfo == null) continue;
            found.add(new android.content.pm.LauncherActivityInfoInternal(info.activityInfo,
                    new android.content.pm.IncrementalStatesInfo(false, 1f, 0L), user));
        }
        return found;
    }

    private static android.content.pm.ActivityInfo activityInfo(android.content.ComponentName component) {
        android.content.pm.PackageManager pm = packageManager();
        if (pm == null || component == null) return null;
        try {
            return pm.getActivityInfo(component, 0);
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return null;
        }
    }

    private static android.content.pm.ApplicationInfo applicationInfo(String packageName) {
        android.content.pm.PackageManager pm = packageManager();
        if (pm == null || packageName == null) return null;
        try {
            return pm.getApplicationInfo(packageName, 0);
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return null;
        }
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
     * LocalNotifications keeps the app's channels and posted notifications and plays a post's
     * sound, as NotificationManagerService does; notifications are enabled when the user allows
     * them for the host application on OH. Nothing reaches OH's notification centre. Zen mode off.
     * Before, posts and channels were dropped: a timer's notification ended it in silence.
     */
    private static Object notification(String method, Object[] args) {
        return LocalNotifications.answer(method, args);
    }

    /**
     * Every SoundPool, MediaPlayer and AudioTrack is a PlayerBase, and PlayerBase registers itself
     * with IAudioService.trackPlayer through its OWN cached copy of the service, fetched from
     * ServiceManager. AppSpawnXInit stamps a stub into AudioManager's cache only, so that path
     * still got null: Termux's TermuxActivity.onResume builds a SoundPool for its bell and died on
     * the NPE. Answering the name here gives every cache the same binder.
     *
     * Nothing reaches OH audio through this interface; actual playback is the SoundPool/AVPlayer
     * backend's business. It answers as a phone whose speaker is its only output: every stream
     * at AudioSystem's default volume out of AudioService's maximum (music 5 of 15), the ringer
     * normal, the mode normal, and audio focus granted, since one app has nobody to take it from.
     * A volume set or adjusted reads back, per stream; OH's mixer keeps its own. Players are not
     * tracked, and there are no sound effects. trackPlayer's 0 is a player id the caller only hands
     * back later.
     *
     * Before, every method answered its type default: a maximum volume of 0, a ringer mode of 0
     * (silent) and focus refused (AUDIOFOCUS_REQUEST_FAILED), so a player that asks for focus
     * before it plays never played, and volume controls had nothing to set. The harness's
     * framework-contracts probe measured it (svc:audio).
     */
    private static Object audio(String method, Object[] args) {
        switch (method) {
            case "getStreamVolume":
            case "getLastAudibleStreamVolume":
                return streamVolume(firstInt(args));
            case "getStreamMaxVolume":
                return limit(MAX_STREAM_VOLUME, firstInt(args), 15);
            case "getStreamMinVolume":
                return limit(MIN_STREAM_VOLUME, firstInt(args), 0);
            case "setStreamVolume":
            case "setStreamVolumeWithAttribution":
                setStreamVolume((Integer) args[0], (Integer) args[1]);
                return DEFAULT;
            case "adjustStreamVolume":
            case "adjustStreamVolumeWithAttribution":
                adjustStreamVolume((Integer) args[0], (Integer) args[1]);
                return DEFAULT;
            case "adjustSuggestedStreamVolume": {
                int stream = (Integer) args[1];
                adjustStreamVolume(stream == Integer.MIN_VALUE ? 3 /* STREAM_MUSIC */ : stream, (Integer) args[0]);
                return DEFAULT;
            }
            case "isStreamMute": {
                Integer stream = firstInt(args);
                synchronized (sAudio) {
                    return stream != null && stream >= 0 && stream < sMuted.length && sMuted[stream];
                }
            }
            case "getRingerModeExternal":
            case "getRingerModeInternal":
                return sRingerMode;
            case "setRingerModeExternal":
            case "setRingerModeInternal":
                sRingerMode = (Integer) args[0];
                return DEFAULT;
            case "getMode":
                return sMode;
            case "setMode":
                sMode = (Integer) args[0];
                return DEFAULT;
            case "requestAudioFocus":
            case "requestAudioFocusForTest":
            case "abandonAudioFocus":
            case "abandonAudioFocusForTest":
                return 1;   // AUDIOFOCUS_REQUEST_GRANTED
            case "getUiSoundsStreamType":
                return 2;   // STREAM_RING: STREAM_SYSTEM's alias on phones
            case "isStreamAffectedByRingerMode": {
                Integer stream = firstInt(args);
                return stream != null && (stream == 1 || stream == 2 || stream == 5 || stream == 7);
            }
            case "isStreamAffectedByMute": {
                Integer stream = firstInt(args);   // AudioSystem.DEFAULT_MUTE_STREAMS_AFFECTED
                return stream != null && (stream == 0 || stream == 1 || stream == 2 || stream == 3
                        || stream == 5 || stream == 6);
            }
            default:
                return DEFAULT;
        }
    }

    // AudioService's MAX_STREAM_VOLUME and MIN_STREAM_VOLUME, and AudioSystem.DEFAULT_STREAM_VOLUME,
    // by stream type (voice call, system, ring, music, alarm, notification, bluetooth SCO, system
    // enforced, DTMF, TTS, accessibility, assistant).
    private static final int[] MAX_STREAM_VOLUME = {5, 7, 7, 15, 7, 7, 15, 7, 15, 15, 15, 15};
    private static final int[] MIN_STREAM_VOLUME = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    private static final Object sAudio = new Object();
    private static final int[] sVolume = {4, 7, 5, 5, 6, 5, 7, 7, 5, 5, 5, 5};
    private static final boolean[] sMuted = new boolean[12];
    private static volatile int sRingerMode = 2;   // RINGER_MODE_NORMAL
    private static volatile int sMode = 0;         // MODE_NORMAL

    private static int limit(int[] table, Integer stream, int fallback) {
        return stream != null && stream >= 0 && stream < table.length ? table[stream] : fallback;
    }

    private static int streamVolume(Integer stream) {
        if (stream == null || stream < 0 || stream >= sVolume.length) return 0;
        synchronized (sAudio) {
            return sMuted[stream] ? 0 : sVolume[stream];
        }
    }

    private static void setStreamVolume(int stream, int index) {
        if (stream < 0 || stream >= sVolume.length) return;
        synchronized (sAudio) {
            sVolume[stream] = Math.max(MIN_STREAM_VOLUME[stream], Math.min(MAX_STREAM_VOLUME[stream], index));
            sMuted[stream] = false;
        }
    }

    private static void adjustStreamVolume(int stream, int direction) {
        if (stream < 0 || stream >= sVolume.length) return;
        synchronized (sAudio) {
            switch (direction) {
                case -100: sMuted[stream] = true; break;              // ADJUST_MUTE
                case 100: sMuted[stream] = false; break;              // ADJUST_UNMUTE
                case 101: sMuted[stream] = !sMuted[stream]; break;    // ADJUST_TOGGLE_MUTE
                case 1:
                case -1:
                    sVolume[stream] = Math.max(MIN_STREAM_VOLUME[stream],
                            Math.min(MAX_STREAM_VOLUME[stream], sVolume[stream] + direction));
                    sMuted[stream] = false;
                    break;
                default:
                    break;   // ADJUST_SAME shows UI only
            }
        }
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

    /**
     * A personal phone nobody manages: no device or profile owner, no active admin, no policy set.
     * Every query then answers the type default (camera and screen capture allowed, no password
     * requirement, no lock-task packages), and admin calls are accepted and change nothing.
     *
     * CameraX checks DevicePolicyManager.getCameraDisabled before it lists cameras, and casts the
     * manager non-null: the null manager failed every init attempt (FairScan, six seconds of
     * retries before its camera screen).
     *
     * Two answers differ from the default: a provisioned device says so, and USB data signalling
     * is on unless a policy turned it off. Storage encryption answers the default, unsupported,
     * which is what Android reports without file encryption; the board sets no fscrypt policy.
     */
    private static Object devicePolicy(String method, Object[] args) {
        switch (method) {
            case "isDeviceProvisioned":
            case "isUsbDataSignalingEnabled":
                return Boolean.TRUE;
            default:
                return DEFAULT;
        }
    }

    /**
     * Network service discovery with nothing behind it: OH's mDNS is not bridged, so every request
     * fails as NsdService reports an internal error, through the callback the app connected with.
     * NsdManager's constructor connects first and throws on a null connector; that killed Internet
     * Radio's FCast device discoverer while its process started.
     */
    private static Object nsd(String method, Object[] args) {
        if (!"connect".equals(method) || args == null || args.length < 1) return DEFAULT;
        Object callback = args[0];
        try {
            IBinder connector = proxy("servicediscovery-connector", "android.net.nsd.INsdServiceConnector",
                    (call, callArgs) -> nsdRequest(callback, call, callArgs));
            return connector.queryLocalInterface("android.net.nsd.INsdServiceConnector");
        } catch (ClassNotFoundException e) {
            return DEFAULT;
        }
    }

    private static final int NSD_FAILURE_INTERNAL_ERROR = 0;   // NsdManager.FAILURE_INTERNAL_ERROR

    private static Object nsdRequest(Object callback, String method, Object[] args) {
        String failure;
        switch (method) {
            case "discoverServices": failure = "onDiscoverServicesFailed"; break;
            case "stopDiscovery": failure = "onStopDiscoveryFailed"; break;
            case "registerService": failure = "onRegisterServiceFailed"; break;
            case "unregisterService": failure = "onUnregisterServiceFailed"; break;
            case "resolveService": failure = "onResolveServiceFailed"; break;
            case "stopResolution": failure = "onStopResolutionFailed"; break;
            case "registerServiceInfoCallback": failure = "onServiceInfoCallbackRegistrationFailed"; break;
            default: return DEFAULT;   // startDaemon, unregisterServiceInfoCallback, offload engines
        }
        Integer key = firstInt(args);
        if (key == null || callback == null) return DEFAULT;
        try {
            // NsdManager queued the listener under this key before calling; the callback posts the
            // failure to its handler, which reports it to the listener and drops it.
            Class.forName("android.net.nsd.INsdManagerCallback").getMethod(failure, int.class, int.class)
                    .invoke(callback, key, NSD_FAILURE_INTERNAL_ERROR);
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] servicediscovery " + failure + " not delivered: " + t);
        }
        return DEFAULT;
    }

    /**
     * Usage stats as an app without usage access sees them on Android: every query answers empty
     * (PACKAGE_USAGE_STATS is not granted), and the caller, running in the foreground, is in the
     * ACTIVE standby bucket. linphone asks for its bucket while it starts and threw on the null
     * service.
     */
    private static Object usageStats(String method, Object[] args) {
        switch (method) {
            case "getAppStandbyBucket":
                return 10;   // UsageStatsManager.STANDBY_BUCKET_ACTIVE
            default:
                return DEFAULT;   // not inactive; event and stats queries empty (null events, empty slices)
        }
    }

    /**
     * Storage stats from the board's own data partition. Totals and free space are what statfs
     * reports for the app's data directory; the app's own package is measured on disk (its APKs,
     * data and cache directories). Other users' and other packages' stats need permissions an
     * app does not hold, so those answer empty.
     */
    private static Object storageStats(String method, Object[] args) {
        android.app.Application app = android.app.ActivityThread.currentApplication();
        String dataDir = app != null ? app.getDataDir().getPath() : android.os.Environment.getDataDirectory().getPath();
        try {
            switch (method) {
                case "getTotalBytes":
                    return new android.os.StatFs(dataDir).getTotalBytes();
                case "getFreeBytes":
                    return new android.os.StatFs(dataDir).getAvailableBytes();
                case "getCacheQuotaBytes":
                    return CACHE_QUOTA_BYTES;
                case "queryStatsForPackage":
                case "queryStatsForUid":
                    return ownStorageStats(app);
                case "queryStatsForUser":
                    return new android.app.usage.StorageStats();
                default:
                    return DEFAULT;   // quotas and reserved space unsupported, cache total unknown (0)
            }
        } catch (RuntimeException e) {
            System.err.println("[" + TAG + "] storagestats." + method + ": " + e);
            return DEFAULT;
        }
    }

    /** StorageStatsService's quota for an app it has no usage history for (DEFAULT_QUOTA). */
    public static final long CACHE_QUOTA_BYTES = 64L * 1024 * 1024;

    private static android.app.usage.StorageStats ownStorageStats(android.app.Application app) {
        android.app.usage.StorageStats stats = new android.app.usage.StorageStats();
        if (app == null) return stats;
        android.content.pm.ApplicationInfo info = app.getApplicationInfo();
        long code = sizeOf(new java.io.File(info.sourceDir));
        if (info.splitSourceDirs != null) {
            for (String split : info.splitSourceDirs) code += sizeOf(new java.io.File(split));
        }
        stats.codeBytes = code;
        stats.apkBytes = code;
        stats.cacheBytes = sizeOf(app.getCacheDir()) + sizeOf(app.getCodeCacheDir());
        stats.dataBytes = sizeOf(app.getDataDir());   // includes the caches, as Android counts it
        return stats;
    }

    /** Bytes under a file or directory, stopping after 100000 entries so a huge tree cannot stall the caller. */
    private static long sizeOf(java.io.File root) {
        long total = 0;
        int seen = 0;
        java.util.ArrayDeque<java.io.File> pending = new java.util.ArrayDeque<>();
        if (root != null) pending.add(root);
        while (!pending.isEmpty() && seen++ < 100000) {
            java.io.File file = pending.poll();
            if (java.nio.file.Files.isSymbolicLink(file.toPath())) continue;
            java.io.File[] children = file.isDirectory() ? file.listFiles() : null;
            if (children != null) {
                java.util.Collections.addAll(pending, children);
            } else {
                total += file.length();
            }
        }
        return total;
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

package adapter.activity;

import adapter.core.SocketBinder;
import adapter.packagemanager.RemotePackages;
import android.app.ActivityThread;
import android.app.Application;
import android.app.ContentProviderHolder;
import android.app.IServiceConnection;
import android.content.ComponentName;
import android.content.ContentProviderNative;
import android.content.Context;
import android.content.IContentProvider;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.ApplicationInfo;
import android.content.pm.ActivityInfo;
import android.content.pm.IPackageManager;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.Process;
import android.os.RemoteException;
import android.os.UserHandle;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Connects apps to the shared Play services process, and to the Play Store's, over SocketBinder
 * (ADR-0001).
 *
 * The Play services process serves when WESTLAKE_PLAY_SERVICES_SERVE names a socket; an app connects
 * when WESTLAKE_PLAY_SERVICES_CONNECT does. An app then asks the other process about Play services'
 * packages, binds and starts its services and acquires its content providers. What comes back are
 * real binders, so client libraries talk to Play services directly from there on.
 *
 * The Play Store runs as a second host, as GrapheneOS runs it beside Play services: it serves when
 * WESTLAKE_PLAY_STORE_SERVE names a socket, and is itself a client of the Play services process (its
 * account manager, Gservices and check-in are there). An app with WESTLAKE_PLAY_STORE_CONNECT reaches
 * the Play Store's package there and everything else in the Play services process.
 *
 * Each app says which package it is, and nothing checks that. All Westlake processes share one uid,
 * the kernel reports only a pid, and OH's hidepid hides one app's /proc entries from another, so the
 * host can't look. An app can claim to be another until appspawn-x creates the channels, so this
 * must not hold real data before then.
 */
public final class PlayServicesChannel {
    private static final String SERVE = "WESTLAKE_PLAY_SERVICES_SERVE";
    private static final String CONNECT = "WESTLAKE_PLAY_SERVICES_CONNECT";
    private static final String STORE_SERVE = "WESTLAKE_PLAY_STORE_SERVE";
    private static final String STORE_CONNECT = "WESTLAKE_PLAY_STORE_CONNECT";
    private static final String PLAY_STORE = "com.android.vending";
    private static final String RUNTIME_ROOT = "WESTLAKE_RUNTIME_ROOT";
    private static final String REAL_ROOT = "WESTLAKE_RUNTIME_REAL_ROOT";
    private static final String TAG = "[WL-PLAY] ";
    /** Whether hello() has said this process has no WESTLAKE_SPAWN_SOCKET (once per process). */
    private static volatile boolean sNoSpawnSocketLogged;
    /** Whether hello() has logged where its fields end (once per process). */
    private static volatile boolean sHelloLayoutLogged;
    /** Set to a number of seconds to report, that long after launch, how the channel looks from this app. */
    private static final String CHECK = "WESTLAKE_PLAY_SERVICES_CHECK";

    private static final int HELLO = 1;
    private static final int PACKAGE_INFO = 2;
    private static final int APPLICATION_INFO = 3;
    private static final int SERVICE_INFO = 4;
    private static final int PROVIDER_INFO = 5;
    private static final int RESOLVE_PROVIDER = 6;
    private static final int QUERY_SERVICES = 7;
    private static final int BIND = 8;
    private static final int START = 9;
    private static final int PROVIDER = 10;
    private static final int STOP = 11;
    private static final int WHO_AM_I = 12;
    /** A system service the Play services process keeps, for a client that is a host too (the account manager). */
    private static final int SERVICE = 13;
    /** To an app's launched process: start one of the app's other processes if it isn't running (ADR-0002). */
    private static final int ENSURE_PROCESS = 14;
    /** To another process of the same app: a broadcast for one of its manifest receivers declared there. */
    private static final int RECEIVER = 15;
    /** To an app's launched process, from another of its processes: start one of its activities declared there. */
    private static final int ACTIVITY = 16;
    /** To an app's launched process, from another of its processes: ask the user to confirm a package change. */
    private static final int CONFIRM = 17;
    /** To a host, from a client: a read-only descriptor of a module file the host shares (ADR-0005, PlayModuleFiles). */
    private static final int MODULE_FILE = 18;
    /** To a host, from a client: the module files the host shares (ADR-0005, PlayModuleFiles.list). */
    private static final int MODULE_LIST = 19;
    /** A narrow, separately authorized account/token provider; never a Play API route. */
    private static final int AUTH_PROVIDER = 20;
    private static final int QUERY_ACTIVITIES = 21;
    private static final int START_ACTIVITY_FOR_RESULT = 22;
    private static final int DELEGATED_AUTH = 23;
    private static final int READ_DELEGATED_AUTH = 24;
    private static final int START_PENDING_ACTIVITY = 25;
    private static final int START_CREATOR_ACTIVITY = 26;
    /** From the host to a client's callback: a broadcast for that app, such as a push message. */
    private static final int DELIVER_BROADCAST = 1;
    /** From the host to a client's callback: send one of that app's own PendingIntents. */
    private static final int SEND_SENDER = 2;

    private static final long BIND_TIMEOUT_SECONDS = 30;
    private static final long BOOT_ANNOUNCE_DELAY_MS = 5000;
    private static final long HELLO_PACKAGE_FLAGS = PackageManager.GET_SIGNATURES
            | PackageManager.GET_SIGNING_CERTIFICATES | PackageManager.GET_META_DATA
            | PackageManager.GET_PERMISSIONS;

    /** Packages a client looks for in the other process. Absent ones are remembered as absent. */
    private static final Set<String> PLAY_PACKAGES = new HashSet<>(Arrays.asList(
            "com.google.android.gms", "com.google.android.gsf", "com.android.vending"));

    private PlayServicesChannel() {}

    private interface Marshal {
        void write(Parcel data);
    }

    // ---- App side ----

    private static final Object CLIENT_LOCK = new Object();
    /** Held here, since a channel only holds what it has been sent while the peer does. */
    private static final Binder sCallback = new ClientCallback();
    private static final Set<String> sAbsent = Collections.synchronizedSet(new HashSet<>());
    private static final Set<String> sPresent = Collections.synchronizedSet(new HashSet<>());

    /** The connection to one host: the Play services process, or the Play Store's. */
    private static final class Channel {
        final String env;
        final String label;
        /** One process's own socket (AppProcesses), for a channel not named by an environment variable. */
        final String fixed;
        /** Guarded by CLIENT_LOCK. */
        SocketBinder binder;
        volatile String remoteRoot;
        volatile String remoteRealRoot;
        /** The uid the host gave this app's channel (0 from a host that doesn't say). */
        volatile int uid;

        Channel(String env, String label) {
            this(env, label, null);
        }

        Channel(String env, String label, String fixed) {
            this.env = env;
            this.label = label;
            this.fixed = fixed;
        }

        String socket() {
            return fixed != null ? fixed : System.getenv(env);
        }
    }

    /** Channels to single processes of an app (ADR-0002), by socket. */
    private static final Map<String, Channel> PROCESS_CHANNELS = new HashMap<>();

    private static Channel processChannel(String socket) {
        synchronized (PROCESS_CHANNELS) {
            return PROCESS_CHANNELS.computeIfAbsent(socket, name -> new Channel(null, "process " + name, name));
        }
    }

    /** Asks an app's launched process, at root, to start its process if it isn't running. */
    private static boolean ensureProcess(IBinder root, String process) throws RemoteException {
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(process);
            root.transact(ENSURE_PROCESS, data, reply, 0);
            reply.readException();
            return reply.readBoolean();
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /**
     * The channel to the process another app declares a service for, when that app runs it (ADR-0002):
     * its launched process starts it on request, and this app connects to it with its own identity,
     * as on Android. Null when that app runs everything in one process, as Westlake did before.
     */
    private static Channel remoteProcess(Channel host, String packageName, String process) {
        String socket = host.socket();
        if (socket == null || process == null || process.equals(packageName)) return null;
        try {
            IBinder root = root(host);
            if (root == null || !ensureProcess(root, process)) return null;
            return processChannel(AppProcesses.socketOf(socket, packageName, process));
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + process + " of " + packageName + " not started: " + e);
            return null;
        }
    }

    /**
     * The channel to another process of this app (ADR-0002), started first if need be: by this process
     * if it is the one the app was launched as, else by asking that one. Null if it cannot start.
     */
    private static Channel ownProcess(String process) {
        try {
            if (AppProcesses.isSecondary()) {
                if (!process.equals(AppProcesses.home())) {
                    IBinder home = root(processChannel(AppProcesses.socketOf(AppProcesses.home())));
                    if (home == null || !ensureProcess(home, process)) return null;
                }
            } else {
                AppProcesses.ensure(process);
            }
            return processChannel(AppProcesses.socketOf(process));
        } catch (IOException | RemoteException | RuntimeException e) {
            System.err.println(TAG + "process " + process + " unavailable: " + e);
            return null;
        }
    }

    /**
     * One of this app's activities declared for its launched process, started from another of its processes
     * (ADR-0002): the activity manager starts an activity in the process it is declared for, so the start goes to
     * the launched process, which starts it there. Started here, it ran without its own process's objects: the
     * Play Store's :background process sent an app's PendingIntent for the store's MainActivity, which failed in
     * :background on that process's own Dagger component (ClassCastException). The caller asks this only for a
     * start without a result. False leaves the start to this process, as before; an activity declared for
     * another secondary process still starts where it is started.
     */
    public static boolean startInHome(Intent intent) {
        if (!AppProcesses.isSecondary() || intent == null || intent.getComponent() == null) return false;
        Application app = application();
        if (app == null || !app.getPackageName().equals(intent.getComponent().getPackageName())) return false;
        String process;
        try {
            process = AppProcesses.activityProcessOf(app.getPackageManager().getActivityInfo(intent.getComponent(), 0));
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return false;
        }
        if (process == null || !process.equals(AppProcesses.home())) return false;
        Channel home = ownProcess(process);
        IBinder root = home == null ? null : root(home);
        if (root == null) return false;
        Parcel data = Parcel.obtain();
        try {
            data.writeTypedObject(intent, 0);
            root.transact(ACTIVITY, data, null, IBinder.FLAG_ONEWAY);
            System.err.println(TAG + "activity " + intent.getComponent().flattenToShortString() + " started in " + process);
            return true;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "activity start in " + process + " failed: " + e);
            return false;
        } finally {
            data.recycle();
        }
    }

    /**
     * The user's confirmation of a package change, asked in this app's launched process, which has its windows
     * (InstallConfirmDialog): from the installer's :background process, where the Play Store's sessions live. The
     * answer comes on answer. False when that process could not be reached or had no activity to ask over.
     */
    public static boolean confirmInHome(android.os.Bundle request, IBinder answer) {
        Channel home = ownProcess(AppProcesses.home());
        IBinder root = home == null ? null : root(home);
        if (root == null) return false;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeBundle(request);
            data.writeStrongBinder(answer);
            root.transact(CONFIRM, data, reply, 0);
            reply.readException();
            return reply.readBoolean();
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "confirmation in " + AppProcesses.home() + " not asked: " + e);
            return false;
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    /**
     * bindService of this app's service declared for another of its processes: connected from a thread,
     * delivered on the main looper as bind() delivers. If that process cannot start, unavailable runs
     * instead (the caller binds the service here, as before).
     */
    public static void bindOwnProcess(final String process, final Intent intent, final int flags,
            final ComponentName component, final IServiceConnection connection, final Runnable unavailable) {
        Thread thread = new Thread(() -> {
            Channel channel = ownProcess(process);
            if (channel == null) {
                unavailable.run();
                return;
            }
            System.err.println(TAG + "bind " + component.flattenToShortString() + " in " + process);
            connect(() -> ownProcess(process), intent, flags, component, connection, 0);
        }, "app-process-bind");
        thread.setDaemon(true);
        thread.start();
    }

    /**
     * Binds this app's service declared for another of its processes and returns what its onBind gave, as
     * the system binds a JobService; null if that process cannot start. Blocks: never on the main thread.
     */
    static IBinder bindInOwnProcess(String process, Intent intent) {
        Channel channel = ownProcess(process);
        IBinder root = channel == null ? null : root(channel);
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeTypedObject(intent, 0);
            data.writeInt(Context.BIND_AUTO_CREATE);
            root.transact(BIND, data, reply, 0);
            reply.readException();
            System.err.println(TAG + "bound " + intent.getComponent().flattenToShortString() + " in " + process);
            return reply.readStrongBinder();
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "bind " + intent.getComponent() + " in " + process + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /** A broadcast for this app's manifest receiver declared for another of its processes, delivered there. */
    static void deliverInOwnProcess(final String process, final Intent intent) {
        Thread thread = new Thread(() -> {
            Channel channel = ownProcess(process);
            IBinder root = channel == null ? null : root(channel);
            if (root == null) {
                System.err.println(TAG + intent.getAction() + " for " + intent.getComponent() + " not delivered: "
                        + process + " unavailable");
                return;
            }
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeTypedObject(intent, 0);
                root.transact(RECEIVER, data, reply, 0);
                reply.readException();
            } catch (RemoteException | RuntimeException e) {
                System.err.println(TAG + intent.getAction() + " for " + intent.getComponent() + " in " + process
                        + " failed: " + e);
            } finally {
                data.recycle();
                reply.recycle();
            }
        }, "app-process-receiver");
        thread.setDaemon(true);
        thread.start();
    }

    /** startService (stop: stopService) of this app's service declared for another of its processes. */
    public static void startOwnProcess(final String process, final Intent intent, final boolean stop) {
        Thread thread = new Thread(() -> {
            Channel channel = ownProcess(process);
            if (channel == null) return;
            IBinder root = root(channel);
            if (root == null) return;
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeTypedObject(intent, 0);
                root.transact(stop ? STOP : START, data, reply, 0);
                reply.readException();
            } catch (RemoteException | RuntimeException e) {
                System.err.println(TAG + (stop ? "stop " : "start ") + intent.getComponent() + " in " + process
                        + " failed: " + e);
            } finally {
                data.recycle();
                reply.recycle();
            }
        }, "app-process-start");
        thread.setDaemon(true);
        thread.start();
    }

    private static final Channel SERVICES = new Channel(CONNECT, "play-services");
    private static final Channel STORE = new Channel(STORE_CONNECT, "play-store");
    private static final Channel AUTH = new Channel("WESTLAKE_GOOGLE_AUTH_CONNECT", "microg-auth");

    /** The hosts this process is a client of, the Play services process first. */
    private static List<Channel> channels() {
        List<Channel> found = new ArrayList<>(2);
        if (SERVICES.socket() != null) found.add(SERVICES);
        if (STORE.socket() != null) found.add(STORE);
        return found;
    }

    /**
     * The host a package's components run in: the Play Store's own process when there is one, as on
     * Android, where the Play Store is an app of its own; the Play services process otherwise, which
     * knows the Play Store as package details (and runs Companion's stand-ins for some of it).
     */
    private static Channel channelFor(String packageName) {
        return PLAY_STORE.equals(packageName) && STORE.socket() != null ? STORE : SERVICES;
    }

    /** The host for a service intent: by its package or component, else by what it resolves to. */
    private static Channel channelFor(Intent intent) {
        String target = targetPackage(intent);
        if (target == null) {
            List<ResolveInfo> found = RemotePackages.queryServices(intent, null, 0);
            if (!found.isEmpty() && found.get(0).serviceInfo != null) target = found.get(0).serviceInfo.packageName;
        }
        return channelFor(target);
    }

    /** This process's own package: a host that is also a client never asks another process about it. */
    private static String ownPackage() {
        return adapter.packagemanager.SourcePackageRegistry.packageForUid(Process.myUid());
    }

    static boolean routesActivity(Intent intent) {
        String target = intent == null ? null : targetPackage(intent);
        return target != null && PLAY_PACKAGES.contains(target) && !target.equals(ownPackage())
                && channelFor(target).socket() != null;
    }

    static List<ResolveInfo> queryRemoteActivities(Intent intent, String type, long flags) {
        if (!routesActivity(intent)) return Collections.emptyList();
        Channel c = channelFor(targetPackage(intent));
        IBinder root = root(c);
        if (root == null) return Collections.emptyList();
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeTypedObject(intent.cloneFilter(), 0); data.writeString(type); data.writeLong(flags);
            if (!root.transact(QUERY_ACTIVITIES, data, reply, 0)) return Collections.emptyList();
            reply.readException();
            List<ResolveInfo> found = reply.createTypedArrayList(ResolveInfo.CREATOR);
            if (found == null) return Collections.emptyList();
            for (ResolveInfo info : found) if (info.activityInfo != null) localize(c, info.activityInfo.applicationInfo);
            return found;
        } catch (RemoteException | RuntimeException error) {
            RemoteActivities.event("query_failed type=" + error.getClass().getSimpleName());
            return Collections.emptyList();
        } finally { data.recycle(); reply.recycle(); }
    }

    static IBinder activityProcess(ActivityInfo info) {
        Channel host = channelFor(info.packageName);
        String process = AppProcesses.activityProcessOf(info);
        Channel target = process.equals(info.packageName) ? host : remoteProcess(host, info.packageName, process);
        return target == null ? null : root(target);
    }

    static IBinder startRemoteActivity(IBinder root, Intent intent, ComponentName caller,
            boolean wantsResult, IBinder callback) throws RemoteException {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeTypedObject(intent, 0); data.writeTypedObject(caller, 0);
            data.writeBoolean(wantsResult); data.writeStrongBinder(callback);
            if (!root.transact(START_ACTIVITY_FOR_RESULT, data, reply, 0)) return null;
            reply.readException(); return reply.readStrongBinder();
        } finally { data.recycle(); reply.recycle(); }
    }

    static IBinder pendingActivityHost(String creator) {
        return creator != null && PLAY_PACKAGES.contains(creator) && !creator.equals(ownPackage())
                ? root(channelFor(creator)) : null;
    }

    static IBinder ownActivityProcess(ActivityInfo info) {
        if (!ownPackage().equals(info.packageName)) throw new SecurityException("not an own activity");
        Channel target = ownProcess(AppProcesses.activityProcessOf(info));
        return target == null ? null : root(target);
    }

    static IBinder startPendingActivity(IBinder host, IBinder sender, Intent fillIn, int mask, int values,
            ComponentName caller, boolean wantsResult, IBinder callback) throws RemoteException {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeStrongBinder(sender); data.writeTypedObject(fillIn, 0);
            data.writeInt(mask); data.writeInt(values); data.writeTypedObject(caller, 0);
            data.writeBoolean(wantsResult); data.writeStrongBinder(callback);
            if (!host.transact(START_PENDING_ACTIVITY, data, reply, 0)) return null;
            reply.readException(); return reply.readStrongBinder();
        } finally { data.recycle(); reply.recycle(); }
    }

    static IBinder startCreatorActivity(IBinder host, Intent intent, ComponentName caller, boolean wantsResult,
            IBinder callback, PackageInfo origin, int pid, String spawner, int uid) throws RemoteException {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeTypedObject(intent, 0); data.writeTypedObject(caller, 0);
            data.writeBoolean(wantsResult); data.writeStrongBinder(callback);
            data.writeTypedObject(origin, 0); data.writeInt(pid); data.writeString(spawner); data.writeInt(uid);
            if (!host.transact(START_CREATOR_ACTIVITY, data, reply, 0)) return null;
            reply.readException(); return reply.readStrongBinder();
        } finally { data.recycle(); reply.recycle(); }
    }

    /**
     * The other process's files, as this one must name them. Each process sees its own runtime at
     * the same logical path, so a path in the other's ApplicationInfo would name this process's own
     * files. Client libraries load code from Play services' APK through it (DynamiteModule).
     */
    private static String localPath(Channel channel, String path) {
        String root = channel.remoteRoot;
        String real = channel.remoteRealRoot;
        if (path == null || root == null || real == null || !path.startsWith(root + "/")) return path;
        return real + path.substring(root.length());
    }

    private static ApplicationInfo localize(Channel c, ApplicationInfo info) {
        if (info == null) return null;
        info.sourceDir = localPath(c, info.sourceDir);
        info.publicSourceDir = localPath(c, info.publicSourceDir);
        info.nativeLibraryDir = localPath(c, info.nativeLibraryDir);
        info.nativeLibraryRootDir = localPath(c, info.nativeLibraryRootDir);
        info.secondaryNativeLibraryDir = localPath(c, info.secondaryNativeLibraryDir);
        info.scanSourceDir = localPath(c, info.scanSourceDir);
        info.scanPublicSourceDir = localPath(c, info.scanPublicSourceDir);
        if (info.splitSourceDirs != null) {
            for (int i = 0; i < info.splitSourceDirs.length; i++) info.splitSourceDirs[i] = localPath(c, info.splitSourceDirs[i]);
        }
        if (info.splitPublicSourceDirs != null) {
            for (int i = 0; i < info.splitPublicSourceDirs.length; i++) info.splitPublicSourceDirs[i] = localPath(c, info.splitPublicSourceDirs[i]);
        }
        return info;
    }

    private static PackageInfo localize(Channel c, PackageInfo info) {
        if (info != null) localize(c, info.applicationInfo);
        return info;
    }

    private static ServiceInfo localize(Channel c, ServiceInfo info) {
        if (info != null) localize(c, info.applicationInfo);
        return info;
    }

    private static ProviderInfo localize(Channel c, ProviderInfo info) {
        if (info != null) localize(c, info.applicationInfo);
        return info;
    }

    /** Whether this process is connected to a Play services or Play Store host. */
    public static boolean isClient() {
        return System.getenv(CONNECT) != null || System.getenv(STORE_CONNECT) != null;
    }

    /** Makes Play services' packages visible to this process, if it is a client. */
    public static void installClient() {
        if (!isClient()) return;
        RemotePackages.setSource(new ClientSource());
        if (System.getenv(CHECK) != null) {
            Thread thread = new Thread(PlayServicesChannel::check, "play-services-check");
            thread.setDaemon(true);
            thread.start();
        }
    }

    /**
     * In the Play services and Play Store processes, a restricted settings read answers unset instead of
     * throwing, as GrapheneOS's Settings answers its GmsCompat apps (NameValueCache.getStringForUser
     * returns null there). Since Android 12, reading a Settings key that is not @Readable, or is readable
     * only up to a target SDK, throws SecurityException unless the app is a system or privileged app. On
     * Android these two apps are privileged, and their code reads such keys unguarded: Play services'
     * Nearby Presence service read Settings.Secure bluetooth_name on its main thread, and the uncaught
     * exception ended the process seconds after it started. NameValueCache checks a key only if it is in
     * its set of the class's keys, mAllFields, which it reads nowhere else; emptied, a restricted read
     * reaches Westlake's settings provider, which answers a key it does not hold as unset. Every other
     * process keeps Android's exception. Call before the application is bound.
     */
    public static void readRestrictedSettingsAsUnset() {
        if (!serves()) return;
        for (String name : new String[] {"android.provider.Settings$Secure", "android.provider.Settings$Global",
                "android.provider.Settings$System"}) {
            try {
                Object cache = null;
                for (java.lang.reflect.Field field : Class.forName(name).getDeclaredFields()) {
                    if (field.getType().getSimpleName().equals("NameValueCache")
                            && java.lang.reflect.Modifier.isStatic(field.getModifiers())) {
                        field.setAccessible(true);
                        cache = field.get(null);
                        break;
                    }
                }
                java.lang.reflect.Field keys = cache.getClass().getDeclaredField("mAllFields");
                keys.setAccessible(true);
                ((Set<?>) keys.get(cache)).clear();
            } catch (ReflectiveOperationException | RuntimeException e) {
                System.err.println(TAG + "restricted settings of " + name + " still throw: " + e);
            }
        }
        System.err.println(TAG + "restricted settings read as unset in this process");
    }

    /** For tests: the client library's own availability answer, and who the other process says this is. */
    private static void check() {
        try {
            Thread.sleep(Long.parseLong(System.getenv(CHECK)) * 1000);
            Application app = ActivityThread.currentApplication();
            try {
                Class<?> api = app.getClassLoader().loadClass("com.google.android.gms.common.GoogleApiAvailability");
                Object instance = api.getMethod("getInstance").invoke(null);
                Object answer = api.getMethod("isGooglePlayServicesAvailable", Context.class).invoke(instance, app);
                System.err.println(TAG + "CHECK isGooglePlayServicesAvailable=" + answer + " (0 is success)");
            } catch (ReflectiveOperationException e) {
                System.err.println(TAG + "CHECK client library not callable by name: " + e);
            }
            IBinder root = root(SERVICES);
            if (root == null) return;
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                root.transact(WHO_AM_I, data, reply, 0);
                reply.readException();
                System.err.println(TAG + "CHECK other process sees " + reply.readString());
            } finally {
                data.recycle();
                reply.recycle();
            }
        } catch (InterruptedException | RemoteException | RuntimeException e) {
            System.err.println(TAG + "CHECK failed: " + e);
        }
    }

    /** A host's root binder, connecting if there is no live channel. Null if unreachable. */
    private static IBinder root(Channel c) {
        String name = c.socket();
        if (name == null) return null;
        synchronized (CLIENT_LOCK) {
            if (c.binder != null && c.binder.isAlive()) return c.binder.getRemoteRoot();
            try {
                int fd = SocketBinder.connect(name);
                if (c == AUTH) {
                    try { verifyAuthProvider(fd); }
                    catch (IOException | RuntimeException e) {
                        android.os.ParcelFileDescriptor.adoptFd(fd).close();
                        throw e;
                    }
                }
                SocketBinder channel = new SocketBinder(fd, null, c.label);
                IBinder root = channel.getRemoteRoot();
                hello(c, root);
                // Capability provenance must be established in both connection directions. A package
                // name returned by IIntentSender is not proof of ownership. Only our own trusted
                // spawner can vouch for another process of this installation.
                SpawnerIdentity.Vouched own = SpawnerIdentity.ask(System.getenv("WESTLAKE_SPAWN_SOCKET"),
                        SocketBinder.peerPid(fd));
                if (own != null) {
                    try { channel.setVerifiedSameApp(ownPackage().equals(own.packageName) && own.apks.length > 0); }
                    finally { own.close(); }
                }
                c.binder = channel;
                System.err.println(TAG + "connected to " + name);
                return root;
            } catch (IOException | RemoteException | RuntimeException e) {
                System.err.println(TAG + "cannot reach " + name + ": " + e);
                return null;
            }
        }
    }

    private static void hello(Channel c, IBinder root) throws RemoteException {
        String packageName = adapter.packagemanager.SourcePackageRegistry.packageForUid(Process.myUid());
        PackageInfo own;
        try {
            own = ActivityThread.getPackageManager().getPackageInfo(packageName, HELLO_PACKAGE_FLAGS,
                    UserHandle.myUserId());
        } catch (RemoteException e) {
            throw e;
        }
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(packageName);
            data.writeTypedObject(own, 0);
            int afterInfo = data.dataPosition();
            data.writeStrongBinder(sCallback);
            writeApks(data, own.applicationInfo);
            int afterApks = data.dataPosition();
            // The spawner that started this process, which the host asks whether this is the package it
            // says it is (SpawnerIdentity). A host that predates it stops reading after the APKs.
            String spawnSocket = System.getenv("WESTLAKE_SPAWN_SOCKET");
            if (spawnSocket == null && !sNoSpawnSocketLogged) {
                // The launcher gives every app's spawner this variable, which the app inherits. An app's
                // native code can rewrite the environment; say what is left, so its host's "unverified" has
                // a cause.
                sNoSpawnSocketLogged = true;
                System.err.println(TAG + "no WESTLAKE_SPAWN_SOCKET in this process's environment ("
                        + System.getenv().size() + " variables; WESTLAKE_RUNTIME_ROOT "
                        + (System.getenv("WESTLAKE_RUNTIME_ROOT") != null ? "present" : "absent") + ")");
            }
            data.writeString(spawnSocket);
            if (!sHelloLayoutLogged) {
                // A host that reads a field from the wrong place says "spawner says null"; compare with its line.
                sHelloLayoutLogged = true;
                System.err.println(TAG + "HELLO layout " + packageName + ": package info ends at " + afterInfo
                        + ", APKs end at " + afterApks + ", " + data.dataSize() + " bytes");
            }
            root.transact(HELLO, data, reply, 0);
            reply.readException();
            c.remoteRoot = reply.readString();
            c.remoteRealRoot = reply.readString();
            c.uid = reply.dataAvail() >= 4 ? reply.readInt() : 0;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /** Pin the provider to the root spawner and APK selected by the launcher, before sending account data. */
    private static void verifyAuthProvider(int fd) throws IOException {
        SpawnerIdentity.Vouched provider = SpawnerIdentity.ask(
                System.getenv("WESTLAKE_GOOGLE_AUTH_SPAWNER"), SocketBinder.peerPid(fd));
        if (provider == null) throw new SecurityException("auth provider is not vouched for");
        try {
            String expected = System.getenv("WESTLAKE_GOOGLE_AUTH_APK_SHA256");
            if (!"com.google.android.gms".equals(provider.packageName) || provider.apks.length == 0
                    || expected == null || !expected.matches("[0-9a-f]{64}")) {
                throw new SecurityException("auth provider pin missing");
            }
            java.security.MessageDigest hash = java.security.MessageDigest.getInstance("SHA-256");
            try (java.io.InputStream in = new android.os.ParcelFileDescriptor.AutoCloseInputStream(
                    android.os.ParcelFileDescriptor.dup(provider.apks[0]))) {
                byte[] buffer = new byte[65536];
                for (int n; (n = in.read(buffer)) != -1;) hash.update(buffer, 0, n);
            }
            StringBuilder actual = new StringBuilder();
            for (byte b : hash.digest()) actual.append(String.format("%02x", b & 255));
            if (!expected.equals(actual.toString())) throw new SecurityException("auth provider APK differs");
            System.err.println("[WL-AUTH-PROXY] provider_launch_verified=true pid=" + SocketBinder.peerPid(fd));
        } catch (java.security.NoSuchAlgorithmException e) { throw new IOException("SHA-256 unavailable"); }
        finally { provider.close(); }
    }

    public static IBinder remoteAuthService() {
        IBinder root = root(AUTH);
        if (root == null) return null;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            root.transact(AUTH_PROVIDER, data, reply, 0);
            reply.readException(); return reply.readStrongBinder();
        } catch (RemoteException e) { return null; }
        finally { data.recycle(); reply.recycle(); }
    }

    /** Only the pinned microG connection can query an account-selection witness. */
    static boolean authProviderCalling() {
        IBinder provider = root(AUTH);
        return provider != null && SocketBinder.isCallingPeer(provider);
    }

    public static String readDelegatedAuth(String method, android.accounts.Account account, String key) throws RemoteException {
        if (!adapter.core.GoogleAuthDelegation.enabled() || !"com.google.android.gms".equals(application().getPackageName())) return null;
        // The authenticated app call owns this capability. Do not borrow another process's picker.
        if (GoogleAuthOperations.present()) return GoogleAuthOperations.read(method, account, key);
        String ui = "com.google.android.gms.ui";
        if (AppProcesses.runsHere(ui)) return RemoteActivities.readDelegatedAuth(method, account, key);
        // Cache reads must not start a background UI; the verified activity route starts it.
        Channel channel = processChannel(AppProcesses.socketOf(ui));
        IBinder target = channel == null ? null : root(channel);
        if (target == null) return null;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeString(method); data.writeTypedObject(account, 0); data.writeString(key);
            if (!target.transact(READ_DELEGATED_AUTH, data, reply, 0)) return null;
            reply.readException(); return reply.readString();
        } finally { data.recycle(); reply.recycle(); }
    }

    static android.os.Bundle delegatedAuth(PackageInfo origin, int pid, String spawner,
            android.accounts.Account account, String key, IBinder witness, String invalidate) throws RemoteException {
        IBinder provider = root(AUTH);
        if (provider == null) throw new RemoteException("auth provider unavailable");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeTypedObject(origin, 0); data.writeInt(pid); data.writeString(spawner);
            data.writeTypedObject(account, 0); data.writeString(key); data.writeStrongBinder(witness); data.writeString(invalidate);
            if (!provider.transact(DELEGATED_AUTH, data, reply, 0)) throw new RemoteException("delegation unavailable");
            reply.readException(); return reply.readBundle(PlayServicesChannel.class.getClassLoader());
        } finally { data.recycle(); reply.recycle(); }
    }

    private static <T extends Parcelable> T callForObject(Channel c, int code, Parcelable.Creator<T> creator, Marshal args) {
        IBinder root = root(c);
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            args.write(data);
            root.transact(code, data, reply, 0);
            reply.readException();
            return reply.readTypedObject(creator);
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "call " + code + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /** What the host calls in this app. */
    private static final class ClientCallback extends Binder {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == SEND_SENDER) {
                IBinder sender = data.readStrongBinder();
                int resultCode = data.readInt();
                Intent fillIn = data.readTypedObject(Intent.CREATOR);
                String type = data.readString();
                LocalIntentSenders.send(android.content.IIntentSender.Stub.asInterface(sender), resultCode, fillIn, type, null);
                return true;
            }
            if (code != DELIVER_BROADCAST) return super.onTransact(code, data, reply, flags);
            Intent intent = data.readTypedObject(Intent.CREATOR);
            String type = data.readString();
            IBinder result = data.readStrongBinder();
            int resultCode = data.readInt();
            String resultData = data.readString();
            android.os.Bundle map = data.readBundle(PlayServicesChannel.class.getClassLoader());
            boolean ordered = data.readBoolean();
            int user = UserHandle.myUserId();
            new Handler(Looper.getMainLooper()).post(() -> LocalBroadcasts.broadcast(intent, type,
                    android.content.IIntentReceiver.Stub.asInterface(result), resultCode, resultData, map, ordered, user));
            return true;
        }
    }

    private static final class ClientSource implements RemotePackages.Source {
        @Override
        public boolean owns(String packageName) {
            if (!PLAY_PACKAGES.contains(packageName) || sAbsent.contains(packageName)) return false;
            // A host that is also a client (the Play Store's) runs its own package itself.
            if (packageName.equals(ownPackage())) return false;
            if (sPresent.contains(packageName)) return true;
            IBinder root = root(channelFor(packageName));
            if (root == null) return false; // Not remembered: the other process may come back.
            if (packageInfo(packageName, 0) != null) {
                sPresent.add(packageName);
                return true;
            }
            sAbsent.add(packageName);
            return false;
        }

        @Override
        public PackageInfo packageInfo(String packageName, long flags) {
            Channel c = channelFor(packageName);
            return localize(c, callForObject(c, PACKAGE_INFO, PackageInfo.CREATOR, data -> {
                data.writeString(packageName);
                data.writeLong(flags);
            }));
        }

        @Override
        public ApplicationInfo applicationInfo(String packageName, long flags) {
            Channel c = channelFor(packageName);
            return localize(c, callForObject(c, APPLICATION_INFO, ApplicationInfo.CREATOR, data -> {
                data.writeString(packageName);
                data.writeLong(flags);
            }));
        }

        @Override
        public ServiceInfo serviceInfo(ComponentName component, long flags) {
            Channel c = channelFor(component == null ? null : component.getPackageName());
            return localize(c, callForObject(c, SERVICE_INFO, ServiceInfo.CREATOR, data -> {
                data.writeTypedObject(component, 0);
                data.writeLong(flags);
            }));
        }

        @Override
        public ProviderInfo providerInfo(ComponentName component, long flags) {
            Channel c = channelFor(component == null ? null : component.getPackageName());
            return localize(c, callForObject(c, PROVIDER_INFO, ProviderInfo.CREATOR, data -> {
                data.writeTypedObject(component, 0);
                data.writeLong(flags);
            }));
        }

        @Override
        public ProviderInfo resolveProvider(String authority, long flags) {
            for (Channel c : channels()) {
                ProviderInfo info = localize(c, callForObject(c, RESOLVE_PROVIDER, ProviderInfo.CREATOR, data -> {
                    data.writeString(authority);
                    data.writeLong(flags);
                }));
                if (info != null) return info;
            }
            return null;
        }

        /**
         * By its package when the intent names one; otherwise every host's answers, each keeping only
         * the packages that run there (the Play services process still lists Companion's stand-ins for
         * the Play Store's services, which the Play Store's own process answers once it runs).
         */
        @Override
        public List<ResolveInfo> queryServices(Intent intent, String type, long flags) {
            String target = targetPackage(intent);
            if (target != null) return query(channelFor(target), intent, type, flags);
            List<ResolveInfo> found = new ArrayList<>();
            for (Channel c : channels()) {
                for (ResolveInfo item : query(c, intent, type, flags)) {
                    if (item.serviceInfo != null && channelFor(item.serviceInfo.packageName) == c) found.add(item);
                }
            }
            return found;
        }

        private List<ResolveInfo> query(Channel c, Intent intent, String type, long flags) {
            IBinder root = root(c);
            if (root == null) return Collections.emptyList();
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeTypedObject(intent, 0);
                data.writeString(type);
                data.writeLong(flags);
                root.transact(QUERY_SERVICES, data, reply, 0);
                reply.readException();
                List<ResolveInfo> found = reply.createTypedArrayList(ResolveInfo.CREATOR);
                if (found == null) return Collections.emptyList();
                for (ResolveInfo item : found) localize(c, item.serviceInfo);
                return found;
            } catch (RemoteException | RuntimeException e) {
                System.err.println(TAG + "query failed: " + e);
                return Collections.emptyList();
            } finally {
                data.recycle();
                reply.recycle();
            }
        }
    }

    private static String targetPackage(Intent intent) {
        if (intent == null) return null;
        ComponentName component = intent.getComponent();
        return component != null ? component.getPackageName() : intent.getPackage();
    }

    /** Whether an intent names a service in the other process. */
    public static boolean handles(Intent intent) {
        return isClient() && RemotePackages.contains(targetPackage(intent));
    }

    /**
     * bindService of a service in the other process: 1 if it will connect, 0 if no such service, as
     * bindService answers. Connected on the main looper, and told if that process dies.
     */
    public static int bind(final Intent intent, final int flags, final IServiceConnection connection) {
        List<ResolveInfo> found = RemotePackages.queryServices(intent, null, 0);
        if (found.isEmpty() || found.get(0).serviceInfo == null) {
            System.err.println(TAG + "no service for " + intent);
            return 0;
        }
        ServiceInfo info = found.get(0).serviceInfo;
        final ComponentName component = new ComponentName(info.packageName, info.name);
        final Channel channel = channelFor(info.packageName);
        final String process = AppProcesses.processOf(info);
        Thread thread = new Thread(() -> {
            // A service the host declares for another of its processes runs there (ADR-0002).
            connect(() -> {
                Channel own = remoteProcess(channel, info.packageName, process);
                return own != null ? own : channel;
            }, intent, flags, component, connection, 0);
        }, "play-services-bind");
        thread.setDaemon(true);
        thread.start();
        return 1;
    }

    /** A bound service's process that died is started again: after 1 s, four times longer each time, at most 60 s. */
    private static final int MAX_REBINDS = 8;
    private static final long REBIND_RESET_MS = 60_000;

    private static void connect(Channel channel, Intent intent, int flags, ComponentName component,
            IServiceConnection connection) {
        connect(() -> channel, intent, flags, component, connection, 0);
    }

    /**
     * Binds through the channel where() finds and delivers the binder on the main looper. When the service's
     * process dies the connection hears it (onServiceDisconnected), and a binding made with BIND_AUTO_CREATE is
     * made again once where() finds the process, which it starts if it is one of this app's: ActiveServices
     * restarts the service and the client gets onServiceConnected again. rebinds counts the attempts since the
     * binding last lasted a minute.
     */
    private static void connect(java.util.function.Supplier<Channel> where, Intent intent, int flags,
            ComponentName component, IServiceConnection connection, int rebinds) {
        Channel channel = where.get();
        IBinder root = channel == null ? null : root(channel);
        if (root == null) {
            if (rebinds > 0) rebindLater(where, intent, flags, component, connection, rebinds + 1);
            return;
        }
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        IBinder service = null;
        try {
            data.writeTypedObject(intent, 0);
            data.writeInt(flags);
            root.transact(BIND, data, reply, 0);
            reply.readException();
            service = reply.readStrongBinder();
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "bind of " + component.flattenToShortString() + " failed: " + e);
            if (rebinds > 0) rebindLater(where, intent, flags, component, connection, rebinds + 1);
            return;
        } finally {
            data.recycle();
            reply.recycle();
        }
        final IBinder connected = service;
        System.err.println(TAG + "bound " + component.flattenToShortString() + " binder=" + connected);
        Handler main = new Handler(Looper.getMainLooper());
        main.post(() -> deliver(connection, component, connected, false));
        if (connected != null) {
            final long since = android.os.SystemClock.elapsedRealtime();
            Runnable died = () -> {
                main.post(() -> deliver(connection, component, null, true));
                if ((flags & Context.BIND_AUTO_CREATE) == 0) return;
                boolean lasted = android.os.SystemClock.elapsedRealtime() - since >= REBIND_RESET_MS;
                rebindLater(where, intent, flags, component, connection, lasted ? 1 : rebinds + 1);
            };
            try {
                connected.linkToDeath(died::run, 0);
            } catch (RemoteException | RuntimeException e) {
                died.run();
            }
        }
    }

    private static void rebindLater(java.util.function.Supplier<Channel> where, Intent intent, int flags,
            ComponentName component, IServiceConnection connection, int rebinds) {
        if (rebinds > MAX_REBINDS) {
            System.err.println(TAG + "gave up binding " + component.flattenToShortString() + " again");
            return;
        }
        long delay = Math.min(60_000L, 1000L << (2 * Math.min(rebinds - 1, 5)));
        Thread thread = new Thread(() -> {
            try {
                Thread.sleep(delay);
            } catch (InterruptedException e) {
                return;
            }
            System.err.println(TAG + "binding " + component.flattenToShortString() + " again (" + rebinds + ")");
            connect(where, intent, flags, component, connection, rebinds);
        }, "service-rebind");
        thread.setDaemon(true);
        thread.start();
    }

    private static void deliver(IServiceConnection connection, ComponentName component, IBinder binder, boolean dead) {
        try {
            connection.connected(component, binder, dead);
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "connection " + component.flattenToShortString() + " failed: " + e);
        }
    }

    /** startService of a service in the other process. Answers the component, or null if none. */
    public static ComponentName start(Intent intent) {
        return callForObject(channelFor(intent), START, ComponentName.CREATOR, data -> data.writeTypedObject(intent, 0));
    }

    public static boolean stop(Intent intent) {
        IBinder root = root(channelFor(intent));
        if (root == null) return false;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeTypedObject(intent, 0);
            root.transact(STOP, data, reply, 0);
            reply.readException();
            return reply.readBoolean();
        } catch (RemoteException | RuntimeException e) {
            return false;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /** getContentProvider for an authority in the other process, or null. */
    public static ContentProviderHolder provider(String authority) {
        if (isHost() && LocalGservices.AUTHORITY.equals(authority)) return LocalGservices.holder();
        ContentProviderHolder routed = ownProcessProvider(authority);
        if (routed != null) return routed;
        if (serves()) {
            ContentProviderHolder own = ownProvider(authority);
            if (own != null) return own;
        }
        if (!isClient() || authority == null) return null;
        ProviderInfo info = RemotePackages.resolveProvider(authority, PackageManager.GET_META_DATA);
        if (info == null) return null;
        // A provider the host declares for another of its processes runs there (ADR-0002), and this app asks that
        // process for it with its own identity, as Android hands a client the provider of the provider's own
        // process. Asked through the host's launched process, the call reached the provider as that process,
        // whose uid is not the one in the AttributionSource this app sends: Play services' GmsModuleProvider
        // (:persistent) refused every client ("Calling uid: <Play services> doesn't match source uid: <app>"),
        // so no client found a Dynamite module and the apps that wait for one stopped.
        Channel channel = channelFor(info.packageName);
        String process = AppProcesses.processOf(info);
        Channel own = remoteProcess(channel, info.packageName, process);
        if (own != null) channel = own;
        IBinder root = root(channel);
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(authority);
            root.transact(PROVIDER, data, reply, 0);
            reply.readException();
            IBinder binder = reply.readStrongBinder();
            if (binder == null) return null;
            ContentProviderHolder holder = new ContentProviderHolder(info);
            holder.provider = attributed(channel, ContentProviderNative.asInterface(binder), info.packageName, authority);
            // Play services' module code in this app opens module files at their paths in Play services' storage
            // (ADR-0005): with WESTLAKE_PLAY_MODULE_FILES=copy they are copied here before it can ask.
            if (PlayModuleFiles.modules(authority)) PlayModuleFiles.copyListed(info.packageName);
            holder.noReleaseNeeded = true;
            System.err.println(TAG + "provider " + authority + " -> " + info.packageName + "/" + info.name
                    + (own != null ? " in " + process : ""));
            return holder;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "provider " + authority + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /**
     * A provider in another host, with each call's AttributionSource naming this app as that host knows
     * it. The host checks, as Android does whenever one is unparceled, that the source's uid is the
     * caller's (enforceCallingUid); this process's own uid is the one every Westlake process shares, and
     * the host knows this app by the uid it gave the channel. Package, pid and tag stay as they are.
     */
    private static IContentProvider attributed(Channel c, IContentProvider provider, String hostPackage,
            String authority) {
        final int uid = c.uid;
        if (provider == null || uid == 0) return provider;
        return (IContentProvider) java.lang.reflect.Proxy.newProxyInstance(PlayServicesChannel.class.getClassLoader(),
                new Class<?>[] {IContentProvider.class}, (self, method, args) -> {
                    if (args != null) {
                        for (int i = 0; i < args.length; i++) {
                            if (args[i] instanceof android.content.AttributionSource) {
                                args[i] = withUid((android.content.AttributionSource) args[i], uid);
                            }
                        }
                    }
                    PlayModuleFiles.observeCall(authority, method.getName(), args);
                    try {
                        Object result = method.invoke(provider, args);
                        // A module loader in this app opens the files a host's provider names (ADR-0005).
                        return result instanceof android.database.Cursor
                                ? PlayModuleFiles.view((android.database.Cursor) result, hostPackage, authority) : result;
                    } catch (java.lang.reflect.InvocationTargetException e) {
                        throw e.getCause();
                    }
                });
    }

    /**
     * A file of the host's storage that the host shares with clients, read-only (ADR-0005), or null when the host
     * refuses it (its reason logged) or cannot be reached.
     */
    /** The module files the host shares, as it names them (ADR-0005), or none when it cannot be reached. */
    static String[] moduleList(String hostPackage) {
        IBinder root = root(channelFor(hostPackage));
        if (root == null) return new String[0];
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            root.transact(MODULE_LIST, data, reply, 0);
            reply.readException();
            String[] files = reply.createStringArray();
            return files != null ? files : new String[0];
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "module list of " + hostPackage + " failed: " + e);
            return new String[0];
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    static android.os.ParcelFileDescriptor moduleFile(String hostPackage, String path) {
        IBinder root = root(channelFor(hostPackage));
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(path);
            root.transact(MODULE_FILE, data, reply, 0);
            reply.readException();
            android.os.ParcelFileDescriptor file = reply.readTypedObject(android.os.ParcelFileDescriptor.CREATOR);
            String refusal = reply.readString();
            if (file == null) {
                System.err.println(TAG + "module file " + PlayModuleFiles.relative(hostPackage, path) + " refused by "
                        + hostPackage + ": " + refusal);
            }
            return file;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "module file " + PlayModuleFiles.relative(hostPackage, path) + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static android.content.AttributionSource withUid(android.content.AttributionSource source, int uid) {
        if (source.getUid() == uid) return source;
        android.content.AttributionSource.Builder builder = new android.content.AttributionSource.Builder(uid)
                .setPid(source.getPid())
                .setPackageName(source.getPackageName())
                .setAttributionTag(source.getAttributionTag());
        if (source.getNext() != null) builder.setNext(source.getNext());
        return builder.build();
    }

    /**
     * A service of this app's launched process, for another of its processes (ADR-0002). Play services' account
     * manager is the launched process's: each process had its own, so an account its UI process added (add-account
     * runs there) stayed unknown to the launched process, which answers the Play Store and other apps, until a
     * restart. Null in the launched process, or if it cannot be reached.
     */
    public static IBinder homeService(String name) {
        if (!AppProcesses.isSecondary()) return null;
        Channel home = ownProcess(AppProcesses.home());
        IBinder root = home == null ? null : root(home);
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(name);
            root.transact(SERVICE, data, reply, 0);
            reply.readException();
            IBinder service = reply.readStrongBinder();
            System.err.println(TAG + "service " + name + " from " + AppProcesses.home() + ": "
                    + (service != null ? "connected" : "refused"));
            return service;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "service " + name + " from " + AppProcesses.home() + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /**
     * A system service of the Play services process, for its clients: the account manager there is every
     * client's, the Play Store's included, once the host has verified who the client is (hostService;
     * LocalServiceBinders asks for it by name). Null if unreachable or refused.
     */
    public static IBinder remoteService(String name) {
        IBinder root = root(SERVICES);
        if (root == null) return null;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeString(name);
            root.transact(SERVICE, data, reply, 0);
            reply.readException();
            IBinder service = reply.readStrongBinder();
            System.err.println(TAG + "service " + name + " from the Play services process: "
                    + (service != null ? "connected" : "refused"));
            return service;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "service " + name + " failed: " + e);
            return null;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    // ---- Host side ----

    /** Whether this process is the shared Play services process. */
    public static boolean isHost() {
        return System.getenv(SERVE) != null;
    }

    /** Whether this process serves apps: the Play services process, or the Play Store's. */
    private static boolean serves() {
        return System.getenv(SERVE) != null || System.getenv(STORE_SERVE) != null;
    }

    /**
     * A provider of this host's own package that Android runs in another of the package's processes
     * (Play services' :persistent GmsModuleProvider, through which every client of Play services maps
     * an action to its component): those processes run in this one (ADR-0001), so it is made here when
     * first asked for, as the activity manager would start its process. The bind installs only the
     * providers of the main process, so the lookup failed in this process and for every client.
     */
    /**
     * A provider this app declares for another of its processes (ADR-0002): that process's, started first if
     * need be, as ActivityManager starts a provider's process for getContentProvider and waits for it to
     * publish. Null when it runs here, or when the app runs everything in one process.
     */
    private static ContentProviderHolder ownProcessProvider(String authority) {
        Application app = application();
        if (!AppProcesses.enabled() || app == null || authority == null) return null;
        for (ProviderInfo info : adapter.packagemanager.PackageManagerAdapter.getProvidersForPackage(app.getPackageName())) {
            if (info == null || info.authority == null) continue;
            for (String declared : info.authority.split(";")) {
                if (!authority.equals(declared.trim())) continue;
                String process = AppProcesses.processOf(info);
                if (AppProcesses.runsHere(process)) return null;
                Channel channel = ownProcess(process);
                IBinder root = channel == null ? null : root(channel);
                if (root == null) return null;
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeString(authority);
                    root.transact(PROVIDER, data, reply, 0);
                    reply.readException();
                    IBinder binder = reply.readStrongBinder();
                    if (binder == null) return null;
                    ContentProviderHolder holder = new ContentProviderHolder(info);
                    holder.provider = ContentProviderNative.asInterface(binder);
                    holder.noReleaseNeeded = true;
                    System.err.println(TAG + "provider " + authority + " -> " + info.name + " in " + process);
                    return holder;
                } catch (RemoteException | RuntimeException e) {
                    System.err.println(TAG + "provider " + authority + " in " + process + " failed: " + e);
                    return null;
                } finally {
                    data.recycle();
                    reply.recycle();
                }
            }
        }
        return null;
    }

    private static ContentProviderHolder ownProvider(String authority) {
        Application app = application();
        if (app == null || authority == null) return null;
        for (ProviderInfo info : adapter.packagemanager.PackageManagerAdapter.getProvidersForPackage(app.getPackageName())) {
            if (info == null || info.authority == null) continue;
            for (String declared : info.authority.split(";")) {
                if (!authority.equals(declared.trim())) continue;
                ContentProviderHolder holder = new ContentProviderHolder(info);
                holder.mLocal = true;
                holder.noReleaseNeeded = true;
                System.err.println(TAG + "provider " + authority + " -> " + info.name + " made in this process (declared for "
                        + info.processName + ")");
                return holder;
            }
        }
        return null;
    }

    private static final java.util.Map<String, IBinder> sClientCallbacks = new java.util.concurrent.ConcurrentHashMap<>();

    /**
     * Each process sees its own runtime at the same path, so an app's APK paths name nothing a host can
     * open. Android's services read an installed app's APKs (its resources for a label or icon, its
     * files for the Play Store); here the app hands its APKs over as open files with HELLO, read-only,
     * and the host names them /proc/self/fd/<n>, which opens the same file. Kept per package while the
     * app's channel is open: a reconnect replaces them, and its closing releases them and puts back the
     * paths they stood for, since a closed descriptor's number is soon another file's.
     */
    private static final java.util.Map<String, ClientApks> sClientApks = new java.util.concurrent.ConcurrentHashMap<>();

    /** A client's APKs as open files, and the ApplicationInfo paths they replaced. */
    private static final class ClientApks {
        final android.os.ParcelFileDescriptor[] files;
        final ApplicationInfo app;
        final String sourceDir;
        final String publicSourceDir;
        final String[] splitSourceDirs;
        final String[] splitPublicSourceDirs;

        ClientApks(android.os.ParcelFileDescriptor[] files, ApplicationInfo app) {
            this.files = files;
            this.app = app;
            sourceDir = app.sourceDir;
            publicSourceDir = app.publicSourceDir;
            splitSourceDirs = app.splitSourceDirs;
            splitPublicSourceDirs = app.splitPublicSourceDirs;
        }

        void restorePaths() {
            app.sourceDir = sourceDir;
            app.publicSourceDir = publicSourceDir;
            app.splitSourceDirs = splitSourceDirs;
            app.splitPublicSourceDirs = splitPublicSourceDirs;
        }
    }

    /** Appends the base and split APKs of this app, as open read-only files. */
    /**
     * The most APK files a HELLO carries: the socket binder sends at most 64 descriptors with one message
     * (socketbinder_natives.c MAX_FDS). An app with more, such as a game with dozens of asset-pack splits, hands
     * over none: all or none, since a host maps them by position.
     */
    private static final int MAX_HELLO_APKS = 64;

    private static void writeApks(Parcel data, ApplicationInfo app) {
        List<String> paths = new ArrayList<>();
        if (app != null && app.sourceDir != null) paths.add(app.sourceDir);
        if (app != null && app.splitSourceDirs != null) paths.addAll(Arrays.asList(app.splitSourceDirs));
        if (paths.size() > MAX_HELLO_APKS) {
            System.err.println(TAG + paths.size() + " APK files are more than a HELLO carries; handing over none");
            paths.clear();
        }
        List<android.os.ParcelFileDescriptor> apks = new ArrayList<>();
        for (String path : paths) {
            try {
                apks.add(android.os.ParcelFileDescriptor.open(new java.io.File(path),
                        android.os.ParcelFileDescriptor.MODE_READ_ONLY));
            } catch (IOException e) {
                apks.clear();   // all or none: a host maps them by position
                break;
            }
        }
        data.writeInt(apks.size());
        for (android.os.ParcelFileDescriptor apk : apks) {
            data.writeTypedObject(apk, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        }
    }

    /** The APKs a client appended to HELLO; none from a client that predates them. */
    private static android.os.ParcelFileDescriptor[] readApks(Parcel data) {
        if (data.dataAvail() < 4) return new android.os.ParcelFileDescriptor[0];
        int count = data.readInt();
        // Returning early would leave the entries unread, and the spawner socket after them would be read from the
        // middle of them: one Unity game with 48 asset-pack splits was "unverified" that way in every round.
        if (count < 0 || count > MAX_HELLO_APKS) {
            throw new SecurityException("malformed introduction: " + count + " APK files");
        }
        android.os.ParcelFileDescriptor[] apks = new android.os.ParcelFileDescriptor[count];
        for (int i = 0; i < count; i++) apks[i] = data.readTypedObject(android.os.ParcelFileDescriptor.CREATOR);
        return apks;
    }

    /** Points a client's base and split APK paths at the open files it handed over; null if it handed none. */
    private static ClientApks exposeApks(String packageName, ApplicationInfo app, android.os.ParcelFileDescriptor[] apks) {
        int splits = app.splitSourceDirs != null ? app.splitSourceDirs.length : 0;
        boolean complete = apks.length == 1 + splits;
        for (android.os.ParcelFileDescriptor apk : apks) {
            complete &= apk != null;
        }
        if (!complete) {
            closeAll(apks);
            return null;
        }
        ClientApks kept = new ClientApks(apks, app);
        exposeApkPaths(app, apks);
        ClientApks previous = sClientApks.put(packageName, kept);
        if (previous != null) {
            previous.restorePaths();   // its ApplicationInfo may still be held somewhere here
            closeAll(previous.files);
        }
        return kept;
    }

    private static void exposeApkPaths(ApplicationInfo app, android.os.ParcelFileDescriptor[] apks) {
        String base = "/proc/self/fd/" + apks[0].getFd();
        app.sourceDir = base;
        app.publicSourceDir = base;
        int splits = apks.length - 1;
        if (splits > 0) {
            String[] dirs = new String[splits];
            for (int i = 0; i < splits; i++) dirs[i] = "/proc/self/fd/" + apks[i + 1].getFd();
            app.splitSourceDirs = dirs;
            app.splitPublicSourceDirs = dirs.clone();
        }
    }

    /** Own descriptors for this operation; another activity/HELLO must not replace or close them. */
    private static ClientApks exposeForwardedOrigin(PackageInfo origin, SpawnerIdentity.Vouched proof) {
        android.os.ParcelFileDescriptor[] files = new android.os.ParcelFileDescriptor[proof.apks.length];
        try {
            for (int i = 0; i < files.length; i++) files[i] = android.os.ParcelFileDescriptor.dup(proof.apks[i]);
        } catch (IOException error) {
            proof.close(); closeAll(files);
            throw new SecurityException("original caller APKs unavailable");
        }
        if (!certificatesVerdict(origin.signatures, origin.signingInfo, proof).startsWith("match")) {
            closeAll(files); throw new SecurityException("original caller signer mismatch");
        }
        int splits = origin.applicationInfo.splitSourceDirs == null ? 0 : origin.applicationInfo.splitSourceDirs.length;
        if (files.length != 1 + splits) {
            closeAll(files); throw new SecurityException("original caller APK set mismatch");
        }
        ClientApks kept = new ClientApks(files, origin.applicationInfo);
        exposeApkPaths(origin.applicationInfo, files);
        return kept;
    }

    /** A client's channel closed: its APK files go, and its paths are what it said again. */
    private static void releaseApks(String packageName, ClientApks apks) {
        if (sClientApks.remove(packageName, apks)) {   // not if a reconnect has replaced them
            apks.restorePaths();
            closeAll(apks.files);
        }
    }

    private static void closeAll(android.os.ParcelFileDescriptor[] files) {
        for (android.os.ParcelFileDescriptor file : files) {
            if (file == null) continue;
            try {
                file.close();
            } catch (IOException ignored) {
                // already closed
            }
        }
    }

    /** A broadcast waiting for its app to connect. */
    private static final class Held {
        final Intent intent;
        final String type;
        final android.content.IIntentReceiver result;
        final int resultCode;
        final String resultData;
        final android.os.Bundle map;
        final boolean ordered;

        Held(Intent intent, String type, android.content.IIntentReceiver result, int resultCode, String resultData,
                android.os.Bundle map, boolean ordered) {
            this.intent = intent;
            this.type = type;
            this.result = result;
            this.resultCode = resultCode;
            this.resultData = resultData;
            this.map = map;
            this.ordered = ordered;
        }
    }

    private static final int MAX_HELD_PER_APP = 32;
    private static final java.util.Map<String, java.util.ArrayDeque<Held>> sHeld = new java.util.HashMap<>();

    /**
     * In the host, hands a broadcast aimed at an app that has connected to that app. False if it isn't
     * for one, and the caller goes on as usual. If that app has no process now, the broadcast waits and
     * this asks for the app to be started. Answering "not delivered" would be wrong: Play services
     * unregisters an app it can't deliver to.
     */
    public static boolean deliverToClient(Intent intent, String type, android.content.IIntentReceiver result,
            int resultCode, String resultData, android.os.Bundle map, boolean ordered) {
        if (!isHost() || intent == null) return false;
        String target = targetPackage(intent);
        if (target == null || RemotePackages.uidOf(target) < 0) return false;
        IBinder callback = sClientCallbacks.get(target);
        Held held = new Held(new Intent(intent), type, result, resultCode, resultData, map, ordered);
        if (callback != null && callback.isBinderAlive() && send(target, callback, held)) return true;
        hold(target, held);
        return true;
    }

    private static boolean send(String target, IBinder callback, Held held) {
        Parcel data = Parcel.obtain();
        try {
            data.writeTypedObject(held.intent, 0);
            data.writeString(held.type);
            data.writeStrongBinder(held.result != null ? held.result.asBinder() : null);
            data.writeInt(held.resultCode);
            data.writeString(held.resultData);
            data.writeBundle(held.map);
            data.writeBoolean(held.ordered);
            callback.transact(DELIVER_BROADCAST, data, null, IBinder.FLAG_ONEWAY);
            System.err.println(TAG + "delivered " + held.intent.getAction() + " to " + target);
            return true;
        } catch (RemoteException | RuntimeException e) {
            System.err.println(TAG + "delivery of " + held.intent.getAction() + " to " + target + " failed: " + e);
            return false;
        } finally {
            data.recycle();
        }
    }

    private static void hold(String target, Held held) {
        synchronized (sHeld) {
            java.util.ArrayDeque<Held> queue = sHeld.computeIfAbsent(target, k -> new java.util.ArrayDeque<>());
            if (queue.size() == MAX_HELD_PER_APP) queue.removeFirst();
            queue.addLast(held);
        }
        System.err.println(TAG + "holding " + held.intent.getAction() + " for closed app " + target);
        requestStart(target);
    }

    /**
     * Asks for an app to be started, as a file in this process's data directory. A Westlake process can't
     * start another app (OH refuses the call), so whatever launches apps on this device watches for it.
     */
    private static void requestStart(String target) {
        try {
            java.io.File dir = new java.io.File(adapter.packagemanager.SourcePackageRegistry.dataDir(), "westlake-start-requests");
            dir.mkdirs();
            if (!new java.io.File(dir, target).createNewFile()) return;
            System.err.println(TAG + "requested a start of " + target);
        } catch (IOException | RuntimeException e) {
            System.err.println(TAG + "cannot request a start of " + target + ": " + e);
        }
    }

    /** Sends what waited for an app, oldest first, now that it has connected. */
    private static void releaseHeld(String target, IBinder callback) {
        java.util.ArrayDeque<Held> queue;
        synchronized (sHeld) {
            queue = sHeld.remove(target);
        }
        if (queue == null) return;
        for (Held held : queue) send(target, callback, held);
    }

    /**
     * What the activity manager knows of a PendingIntent an app handed to this process: Play services
     * asks who made it, and that is how it knows which app it is working for. The sender says, from the
     * process that made it, however many processes it passed through: an app's sender that Play services'
     * main process passed on to its persistent one arrives there on the main process's channel, which is
     * Play services' own. Its uid is the one the maker has in every Westlake process (RemotePackages).
     * A sender that can't be asked is answered from the channel it arrived on, as a mutable broadcast
     * sender.
     */
    public static android.app.ActivityManager.PendingIntentInfo remoteSenderInfo(android.content.IIntentSender sender) {
        LocalIntentSenders.Facts facts = LocalIntentSenders.ask(sender);
        if (facts != null) {
            Application app = application();
            int uid = RemotePackages.uidForPackage(facts.packageName, Process.myUid(),
                    app == null ? null : app.getPackageName());
            if (uid != -1) {
                return new android.app.ActivityManager.PendingIntentInfo(facts.packageName, uid, facts.immutable,
                        facts.type);
            }
        }
        int uid = sender == null ? 0 : SocketBinder.ownerUid(sender.asBinder());
        String name = uid == 0 ? null : RemotePackages.packageForUid(uid);
        return name == null ? null : new android.app.ActivityManager.PendingIntentInfo(
                name, uid, false, android.app.ActivityManager.INTENT_SENDER_BROADCAST);
    }

    /**
     * Sends a sender this process holds only through another process (an app's, given to Play services'
     * persistent process by its main one): the call goes to the sender's own binder, which relays it back to
     * the process that made it (LocalIntentSenders.SEND_TRANSACTION). False if the binder can't be reached.
     */
    private static boolean sendThroughSender(android.content.IIntentSender sender, int code, Intent fillIn, String type) {
        IBinder binder = sender == null ? null : sender.asBinder();
        if (binder == null || !binder.isBinderAlive()) return false;
        Parcel data = Parcel.obtain();
        try {
            data.writeInterfaceToken("android.content.IIntentSender");
            data.writeInt(code);
            data.writeTypedObject(fillIn, 0);
            data.writeString(type);
            return binder.transact(LocalIntentSenders.SEND_TRANSACTION, data, null, IBinder.FLAG_ONEWAY);
        } catch (RemoteException | RuntimeException e) {
            return false;
        } finally {
            data.recycle();
        }
    }

    /** Sends an app's PendingIntent in that app. False if it isn't a connected app's. */
    public static boolean sendRemoteSender(android.content.IIntentSender sender, int code, Intent fillIn, String type) {
        int uid = sender == null ? 0 : SocketBinder.ownerUid(sender.asBinder());
        String name = uid == 0 ? null : RemotePackages.packageForUid(uid);
        IBinder callback = name == null ? null : sClientCallbacks.get(name);
        if (callback == null) return sendThroughSender(sender, code, fillIn, type);
        Parcel data = Parcel.obtain();
        try {
            data.writeStrongBinder(sender.asBinder());
            data.writeInt(code);
            data.writeTypedObject(fillIn, 0);
            data.writeString(type);
            callback.transact(SEND_SENDER, data, null, IBinder.FLAG_ONEWAY);
            return true;
        } catch (RemoteException | RuntimeException e) {
            return false;
        } finally {
            data.recycle();
        }
    }

    /** Starts serving if this process was asked to (Play services, or the Play Store). Call once the Application exists. */
    public static void startHostIfAsked() {
        final String name = AppProcesses.isSecondary() ? AppProcesses.socketOf(AppProcesses.self())
                : System.getenv(SERVE) != null ? System.getenv(SERVE) : System.getenv(STORE_SERVE);
        if (name == null) return;
        SocketBinder.installCallingIdentity();
        if (!AppProcesses.isSecondary()) announceBoot();
        Thread thread = new Thread(() -> serve(name), "play-services-host");
        thread.setDaemon(true);
        thread.start();
    }

    private static boolean sServingLaunched;

    /**
     * An app that is not a host serves its launched process too once it starts another process, so
     * that the other can reach the components declared for it (ADR-0002). A host serves already.
     */
    static void serveLaunchedProcess() {
        if (serves() || AppProcesses.isSecondary()) return;
        synchronized (PlayServicesChannel.class) {
            if (sServingLaunched) return;
            sServingLaunched = true;
        }
        final String name = AppProcesses.socketOf(AppProcesses.home());
        SocketBinder.installCallingIdentity();
        Thread thread = new Thread(() -> serve(name), "app-process-host");
        thread.setDaemon(true);
        thread.start();
    }

    /**
     * This process stands in for one that has been running since the device booted, and Play services
     * starts its check-in with Google, which push registration needs, from these broadcasts.
     */
    private static void announceBoot() {
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            String self = application().getPackageName();
            for (String action : new String[] {Intent.ACTION_LOCKED_BOOT_COMPLETED, Intent.ACTION_BOOT_COMPLETED}) {
                application().sendBroadcast(new Intent(action).setPackage(self));
            }
            System.err.println(TAG + "announced boot");
        }, BOOT_ANNOUNCE_DELAY_MS);
    }

    private static void serve(String name) {
        try {
            int listener = SocketBinder.listen(name);
            System.err.println(TAG + "serving on " + name);
            LocalGservices.watchChanges();
            while (true) {
                int fd = SocketBinder.accept(listener);
                Root root = new Root(SocketBinder.peerPid(fd));
                root.mChannel = new SocketBinder(fd, root, "client-" + root.mPeerPid);
            }
        } catch (IOException | RuntimeException e) {
            System.err.println(TAG + "stopped serving: " + e);
        }
    }

    /** What the host offers one connected app. */
    private static final class Root extends Binder {
        final int mPeerPid;
        volatile SocketBinder mChannel;
        private volatile boolean mIdentified;
        private boolean mIdentityEnforced;
        private boolean mOwnApplication;
        private PackageInfo mVerifiedInfo;
        private String mVerifiedSpawner;
        private IBinder mAuthProvider;

        Root(int peerPid) {
            mPeerPid = peerPid;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == INTERFACE_TRANSACTION) return super.onTransact(code, data, reply, flags);
            if (code == HELLO) {
                int uid = hello(data);
                reply.writeNoException();
                reply.writeString(System.getenv(RUNTIME_ROOT));
                reply.writeString(System.getenv(REAL_ROOT));
                reply.writeInt(uid);
                return true;
            }
            if (!mIdentified) throw new SecurityException("channel has not said who it is");
            if (adapter.core.GoogleAuthProxy.provider() && !mOwnApplication && code != AUTH_PROVIDER && code != DELEGATED_AUTH) {
                throw new SecurityException("authentication namespace exposes only its token provider");
            }
            IPackageManager packages = ActivityThread.getPackageManager();
            int user = UserHandle.myUserId();
            reply.writeNoException();
            switch (code) {
                case START_PENDING_ACTIVITY: {
                    if (!mIdentityEnforced || mVerifiedInfo == null) throw new SecurityException("activity identity not verified");
                    IBinder sender = data.readStrongBinder(); Intent fillIn = data.readTypedObject(Intent.CREATOR);
                    int mask = data.readInt(), values = data.readInt();
                    ComponentName caller = data.readTypedObject(ComponentName.CREATOR);
                    boolean wantsResult = data.readBoolean(); IBinder callback = data.readStrongBinder();
                    reply.writeStrongBinder(PendingActivityLaunch.accept(sender, fillIn, mask, values,
                            caller, wantsResult, callback, mVerifiedInfo, mPeerPid, mVerifiedSpawner, Binder.getCallingUid()));
                    return true;
                }
                case START_CREATOR_ACTIVITY: {
                    if (!mIdentityEnforced || !mOwnApplication) throw new SecurityException("creator process not verified");
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    ComponentName caller = data.readTypedObject(ComponentName.CREATOR);
                    boolean wantsResult = data.readBoolean(); IBinder callback = data.readStrongBinder();
                    PackageInfo origin = data.readTypedObject(PackageInfo.CREATOR);
                    int pid = data.readInt(); String spawner = data.readString(); int uid = data.readInt();
                    if (origin == null || origin.applicationInfo == null
                            || origin.signatures == null || origin.signingInfo == null)
                        throw new SecurityException("missing original caller");
                    SpawnerIdentity.Vouched proof = SpawnerIdentity.ask(spawner, pid);
                    if (proof == null) throw new SecurityException("original caller is not live");
                    if (!origin.packageName.equals(proof.packageName) || proof.apks.length == 0) {
                        proof.close(); throw new SecurityException("original caller launch mismatch");
                    }
                    // HELLO's /proc/self/fd paths belong to the intermediate process. Duplicate the
                    // spawner's verified APKs here so this destination can load the caller's label/icon.
                    ClientApks apks = exposeForwardedOrigin(origin, proof);
                    java.util.concurrent.atomic.AtomicBoolean released = new java.util.concurrent.atomic.AtomicBoolean();
                    Runnable cleanup = () -> {
                        if (!released.compareAndSet(false, true)) return;
                        RemotePackages.unregisterTemporaryClient(origin);
                        apks.restorePaths(); closeAll(apks.files);
                    };
                    try {
                        if (RemotePackages.registerTemporaryClient(origin, Process.myUid()) != uid)
                            throw new SecurityException("original caller uid mismatch");
                        reply.writeStrongBinder(RemoteActivities.accept(intent, caller, wantsResult, callback,
                                origin, pid, spawner, uid, true, cleanup));
                    } catch (RemoteException | RuntimeException error) { cleanup.run(); throw error; }
                    return true;
                }
                case READ_DELEGATED_AUTH: {
                    if (!mIdentityEnforced || !mOwnApplication || !adapter.core.GoogleAuthDelegation.enabled()
                            || !"com.google.android.gms".equals(application().getPackageName()))
                        throw new SecurityException("delegated reads require the verified official app");
                    String method = data.readString();
                    android.accounts.Account account = data.readTypedObject(android.accounts.Account.CREATOR);
                    reply.writeString(RemoteActivities.readDelegatedAuth(method, account, data.readString()));
                    return true;
                }
                case DELEGATED_AUTH: {
                    if (!mIdentityEnforced || mVerifiedInfo == null || mVerifiedInfo.signingInfo == null)
                        throw new SecurityException("unverified broker");
                    android.content.pm.Signature[] broker = mVerifiedInfo.signingInfo.getApkContentsSigners();
                    if (broker.length != 1) throw new SecurityException("ambiguous broker signer");
                    try {
                        adapter.core.GoogleAuthDelegation.requireBroker(mVerifiedInfo.packageName, broker[0].toByteArray());
                        PackageInfo origin = data.readTypedObject(PackageInfo.CREATOR);
                        int originPid = data.readInt(); String spawner = data.readString();
                        android.accounts.Account account = data.readTypedObject(android.accounts.Account.CREATOR);
                        String key = data.readString(); IBinder witness = data.readStrongBinder(); String invalidate = data.readString();
                        if (origin == null || origin.signingInfo == null || origin.signatures == null
                                || origin.signatures.length != 1 || origin.signingInfo.getApkContentsSigners().length != 1)
                            throw new SecurityException("unverified origin");
                        SpawnerIdentity.Vouched proof = SpawnerIdentity.ask(spawner, originPid);
                        if (proof == null) throw new SecurityException("origin is not live");
                        if (!origin.packageName.equals(proof.packageName) || proof.apks.length == 0) {
                            proof.close(); throw new SecurityException("origin launch mismatch");
                        }
                        if (!certificatesVerdict(origin.signatures, origin.signingInfo, proof).startsWith("match"))
                            throw new SecurityException("origin signer mismatch");
                        reply.writeBundle(adapter.core.GoogleAuthDelegation.provide(mVerifiedInfo.packageName, broker[0].toByteArray(),
                                origin.packageName, origin.signingInfo.getApkContentsSigners()[0].toByteArray(),
                                origin.signatures[0].toByteArray(), account, key, witness, invalidate));
                        return true;
                    } catch (Exception error) {
                        adapter.core.GoogleAuthDelegation.event("provider_denied=true " + adapter.core.GoogleAuthDelegation.failure(error));
                        throw new SecurityException("delegation refused");
                    }
                }
                case QUERY_ACTIVITIES: {
                    if (!mIdentityEnforced || mVerifiedInfo == null) throw new SecurityException("activity identity not verified");
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    String type = data.readString(); long queryFlags = data.readLong();
                    reply.writeTypedList(RemoteActivities.resolveHere(intent, type, queryFlags, mOwnApplication));
                    return true;
                }
                case START_ACTIVITY_FOR_RESULT: {
                    if (!mIdentityEnforced || mVerifiedInfo == null) throw new SecurityException("activity identity not verified");
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    ComponentName caller = data.readTypedObject(ComponentName.CREATOR);
                    boolean wantsResult = data.readBoolean(); IBinder callback = data.readStrongBinder();
                    reply.writeStrongBinder(RemoteActivities.accept(intent, caller, wantsResult, callback,
                            mVerifiedInfo, mPeerPid, mVerifiedSpawner, Binder.getCallingUid(), mOwnApplication));
                    return true;
                }
                case AUTH_PROVIDER: {
                    if (!mIdentityEnforced || mVerifiedInfo == null || mVerifiedInfo.signatures == null
                            || mVerifiedInfo.signatures.length != 1) {
                        throw new SecurityException("auth client identity is not enforced");
                    }
                    if (mAuthProvider == null) {
                        android.content.pm.Signature[] current = mVerifiedInfo.signingInfo == null ? null
                                : mVerifiedInfo.signingInfo.getApkContentsSigners();
                        if (current == null || current.length != 1) {
                            throw new SecurityException("auth client requires a verified current signer");
                        }
                        mAuthProvider = adapter.core.GoogleAuthProxy.providerBinder(mVerifiedInfo.packageName,
                                current[0].toByteArray(), mVerifiedInfo.signatures[0].toByteArray(), mPeerPid);
                    }
                    reply.writeStrongBinder(mAuthProvider); return true;
                }
                case PACKAGE_INFO: {
                    String name = data.readString();
                    long packageFlags = data.readLong();
                    reply.writeTypedObject(own(name) ? packages.getPackageInfo(name, packageFlags, user) : null, 0);
                    return true;
                }
                case APPLICATION_INFO: {
                    String name = data.readString();
                    long packageFlags = data.readLong();
                    reply.writeTypedObject(own(name) ? packages.getApplicationInfo(name, packageFlags, user) : null, 0);
                    return true;
                }
                case SERVICE_INFO: {
                    ComponentName component = data.readTypedObject(ComponentName.CREATOR);
                    long infoFlags = data.readLong();
                    reply.writeTypedObject(component != null && own(component.getPackageName())
                            ? packages.getServiceInfo(component, infoFlags, user) : null, 0);
                    return true;
                }
                case PROVIDER_INFO: {
                    ComponentName component = data.readTypedObject(ComponentName.CREATOR);
                    long infoFlags = data.readLong();
                    reply.writeTypedObject(component != null && own(component.getPackageName())
                            ? packages.getProviderInfo(component, infoFlags, user) : null, 0);
                    return true;
                }
                case RESOLVE_PROVIDER: {
                    String authority = data.readString();
                    long providerFlags = data.readLong();
                    // Services Framework's settings table, which this process keeps (LocalGservices).
                    if (isHost() && LocalGservices.AUTHORITY.equals(authority)) {
                        ContentProviderHolder holder = LocalGservices.holder();
                        reply.writeTypedObject(holder != null ? holder.info : null, 0);
                        return true;
                    }
                    ProviderInfo info = localProviderInfo(authority, providerFlags);
                    reply.writeTypedObject(info != null && runsHere(info.packageName) ? info : null, 0);
                    return true;
                }
                case QUERY_SERVICES: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    String type = data.readString();
                    long packageFlags = data.readLong();
                    reply.writeTypedList(localServices(intent, type, packageFlags));
                    return true;
                }
                case BIND: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    reply.writeStrongBinder(bindLocally(intent, data.readInt()));
                    return true;
                }
                case START: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    reply.writeTypedObject(application().startService(explicit(intent)), 0);
                    return true;
                }
                case STOP: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    reply.writeBoolean(application().stopService(explicit(intent)));
                    return true;
                }
                case PROVIDER: {
                    reply.writeStrongBinder(localProvider(data.readString()));
                    return true;
                }
                case WHO_AM_I: {
                    reply.writeString(describeCaller());
                    return true;
                }
                case SERVICE: {
                    reply.writeStrongBinder(hostService(data.readString(), mIdentityEnforced && mVerifiedInfo != null));
                    return true;
                }
                case ACTIVITY: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    if (Binder.getCallingUid() != Process.myUid()) {
                        throw new SecurityException("only this app's own processes start its activities here");
                    }
                    if (intent != null) {
                        final Intent start = new Intent(intent).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        new Handler(Looper.getMainLooper()).post(() -> {
                            Application app = application();
                            if (app != null) app.startActivity(start);
                        });
                    }
                    return true;
                }
                case MODULE_FILE: {
                    String path = data.readString();
                    Application app = application();
                    String dataDir = app != null ? app.getApplicationInfo().dataDir : null;
                    String[] refusal = new String[1];
                    android.os.ParcelFileDescriptor file = PlayModuleFiles.open(dataDir, path, refusal);
                    String name = app != null ? PlayModuleFiles.relative(app.getPackageName(), path) : path;
                    System.err.println(TAG + "module file " + name + " for " + describeCaller() + ": "
                            + (file != null ? "served read-only" : "refused (" + refusal[0] + ")"));
                    reply.writeTypedObject(file, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
                    reply.writeString(refusal[0]);
                    return true;
                }
                case MODULE_LIST: {
                    Application app = application();
                    String[] files = PlayModuleFiles.list(app != null ? app.getApplicationInfo().dataDir : null);
                    System.err.println(TAG + "module list (" + files.length + " files) for " + describeCaller());
                    reply.writeStringArray(files);
                    return true;
                }
                case CONFIRM: {
                    android.os.Bundle request = data.readBundle(getClass().getClassLoader());
                    IBinder answer = data.readStrongBinder();
                    if (Binder.getCallingUid() != Process.myUid()) {
                        throw new SecurityException("only this app's own processes ask its user here");
                    }
                    reply.writeBoolean(InstallConfirmDialog.show(request, answer));
                    return true;
                }
                case RECEIVER: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    if (Binder.getCallingUid() != Process.myUid()) {
                        throw new SecurityException("only this app's own processes deliver to its receivers");
                    }
                    ManifestReceivers.deliverHere(intent);
                    return true;
                }
                case ENSURE_PROCESS: {
                    String process = data.readString();
                    try {
                        AppProcesses.ensure(process);
                        reply.writeBoolean(true);
                    } catch (IOException | RuntimeException e) {
                        System.err.println(TAG + "process " + process + " not started for uid "
                                + Binder.getCallingUid() + ": " + e);
                        reply.writeBoolean(false);
                    }
                    return true;
                }
                default:
                    throw new IllegalArgumentException("unknown call " + code);
            }
        }

        /** Registers the app this channel says it is; answers the uid it now has here. */
        private synchronized int hello(Parcel data) {
            if (mIdentified) throw new SecurityException("channel already identified");
            String claimed = data.readString();
            PackageInfo info = data.readTypedObject(PackageInfo.CREATOR);
            IBinder callback = data.readStrongBinder();
            if (claimed == null || info == null || info.applicationInfo == null || !claimed.equals(info.packageName)) {
                throw new SecurityException("malformed introduction");
            }
            int afterCallback = data.dataPosition();
            android.os.ParcelFileDescriptor[] apkFiles = readApks(data);
            int afterApks = data.dataPosition();
            String spawner = data.dataAvail() > 0 ? data.readString() : null;
            if (spawner == null) {
                System.err.println(TAG + "HELLO layout " + claimed + " as read: callback ends at " + afterCallback
                        + ", APKs (" + apkFiles.length + ") end at " + afterApks + ", " + data.dataSize()
                        + " bytes; no spawner socket");
            }
            checkIdentity(this, claimed, info, spawner, apkFiles);
            mIdentityEnforced = "enforce".equals(System.getenv("WESTLAKE_CHANNEL_IDENTITY"));
            mVerifiedInfo = info;
            mVerifiedSpawner = spawner;
            try {
                PackageInfo own = application().getPackageManager().getPackageInfo(application().getPackageName(),
                        PackageManager.GET_SIGNATURES);
                mOwnApplication = claimed.equals(own.packageName) && sameSignatures(info.signatures, own.signatures);
            } catch (PackageManager.NameNotFoundException e) { throw new SecurityException("host package unavailable"); }
            mChannel.setVerifiedSameApp(mIdentityEnforced && mOwnApplication);
            if (mIdentityEnforced && !mOwnApplication && GoogleAuthOperations.enabled()) {
                mChannel.setCallScopes((target, code, request, flags) ->
                        GoogleAuthOperations.enter(mVerifiedInfo, mPeerPid, mVerifiedSpawner, mChannel,
                                target, code, request, flags));
            }
            if (claimed.equals(application().getPackageName())) {
                // Another process of this app (ADR-0002): the same app, so the same uid, as on Android.
                closeAll(apkFiles);
                int uid = Process.myUid();
                mChannel.setCaller(uid, mPeerPid);
                mIdentified = true;
                System.err.println(TAG + "process of this app pid=" + mPeerPid + " uid=" + uid);
                return uid;
            }
            ClientApks apks = exposeApks(claimed, info.applicationInfo, apkFiles);
            if (apks != null && callback != null) {
                try {
                    callback.linkToDeath(() -> releaseApks(claimed, apks), 0);
                } catch (RemoteException e) {
                    releaseApks(claimed, apks);   // the app is already gone
                }
            }
            int uid = RemotePackages.registerClient(info, Process.myUid());
            mChannel.setCaller(uid, mPeerPid);
            if (callback != null) {
                sClientCallbacks.put(claimed, callback);
                // After this call returns, so that the app has its channel when these arrive.
                new Handler(Looper.getMainLooper()).post(() -> releaseHeld(claimed, callback));
            }
            mIdentified = true;
            System.err.println(TAG + "client " + claimed + " pid=" + mPeerPid + " uid=" + uid);
            return uid;
        }
    }

    /**
     * Whether the client is what it says it is, asked of the spawner that started it (SpawnerIdentity,
     * ADR-0003 decision 3), in three parts:
     * - the package: the spawner's own;
     * - the APK files the client handed over: each one a file the spawner launched;
     * - the certificates in the client's PackageInfo: those of the base APK the spawner launched. Until
     *   this check, a client could state any certificate, Google's included.
     *
     * With WESTLAKE_CHANNEL_IDENTITY=enforce an introduction that fails any part is refused. Otherwise each
     * answer is logged ([WL-CHANNEL-IDENTITY]), so a round can show that every real client passes before
     * refusing is switched on; the certificates are then checked off this thread, because verifying an APK
     * reads all of it and a log-only round should not change how long an introduction takes.
     */
    private static void checkIdentity(Root root, String claimed, PackageInfo info, String spawner,
            android.os.ParcelFileDescriptor[] apkFiles) {
        int pid = root.mPeerPid;
        SpawnerIdentity.Vouched vouched = SpawnerIdentity.ask(spawner, pid);
        String said = vouched == null ? null : vouched.packageName;
        String verdict = said == null ? "unverified" : said.equals(claimed) ? "match" : "mismatch";
        String files = "match".equals(verdict) ? apkFilesVerdict(vouched.apks, apkFiles) : "unchecked";
        System.err.println("[WL-CHANNEL-IDENTITY] pid=" + pid + " claims " + claimed + "; spawner says " + said
                + " (" + verdict + "); apk files " + files);
        boolean enforce = "enforce".equals(System.getenv("WESTLAKE_CHANNEL_IDENTITY"));
        boolean checkCertificates = "match".equals(verdict) && vouched.apks.length > 0;
        if (enforce && (!"match".equals(verdict) || !files.startsWith("match") || !checkCertificates)) {
            if (vouched != null) vouched.close();
            closeAll(apkFiles);
            throw new SecurityException("introduction as " + claimed + " not vouched for by its spawner");
        }
        if (!checkCertificates) {
            if (vouched != null) vouched.close();
            return;
        }
        android.content.pm.Signature[] stated = info.signatures;
        android.content.pm.SigningInfo statedInfo = info.signingInfo;
        if (enforce) {
            String certificates = certificatesVerdict(stated, statedInfo, vouched);
            System.err.println("[WL-CHANNEL-IDENTITY] pid=" + pid + " " + claimed + " certificates " + certificates);
            if (!certificates.startsWith("match")) {
                closeAll(apkFiles);
                throw new SecurityException("introduction as " + claimed + " states certificates its APK lacks");
            }
            return;
        }
        new Thread(() -> System.err.println("[WL-CHANNEL-IDENTITY] pid=" + pid + " " + claimed + " certificates "
                + certificatesVerdict(stated, statedInfo, vouched)), "wl-channel-identity").start();
    }

    /**
     * "match" when every APK file the client handed over is one of the files its spawner launched, compared
     * by device and inode, followed by the counts; "differ" otherwise.
     */
    private static String apkFilesVerdict(java.io.FileDescriptor[] launched,
            android.os.ParcelFileDescriptor[] presented) {
        try {
            Set<String> ids = new HashSet<>();
            for (java.io.FileDescriptor apk : launched) ids.add(fileId(android.system.Os.fstat(apk)));
            for (android.os.ParcelFileDescriptor apk : presented) {
                if (apk == null || !ids.contains(fileId(android.system.Os.fstat(apk.getFileDescriptor())))) {
                    return "differ";
                }
            }
            return "match (" + presented.length + " presented, " + launched.length + " launched)";
        } catch (android.system.ErrnoException | RuntimeException e) {
            return "unread (" + e + ")";
        }
    }

    private static String fileId(android.system.StructStat stat) {
        return stat.st_dev + ":" + stat.st_ino;
    }

    /**
     * "match" when the certificates a client states are those of the base APK its spawner launched, as
     * PackageManager reports them: GET_SIGNATURES gives the oldest certificate of a rotated signing key and
     * the current signers otherwise; the SigningInfo gives the current signers. Closes the spawner's files.
     */
    private static String certificatesVerdict(android.content.pm.Signature[] stated,
            android.content.pm.SigningInfo statedInfo, SpawnerIdentity.Vouched vouched) {
        long start = android.os.SystemClock.elapsedRealtime();
        android.os.ParcelFileDescriptor base = null;
        try {
            base = android.os.ParcelFileDescriptor.dup(vouched.apks[0]);
            android.content.pm.parsing.result.ParseResult<android.content.pm.SigningDetails> result =
                    android.util.apk.ApkSignatureVerifier.verify(
                            android.content.pm.parsing.result.ParseTypeImpl.forParsingWithoutPlatformCompat().reset(),
                            "/proc/self/fd/" + base.getFd(),
                            android.content.pm.SigningDetails.SignatureSchemeVersion.JAR);
            if (result.isError()) return "unread (" + result.getErrorMessage() + ")";
            android.content.pm.SigningDetails details = result.getResult();
            android.content.pm.Signature[] signers = details.getSignatures();
            android.content.pm.Signature[] expected = details.hasPastSigningCertificates()
                    ? new android.content.pm.Signature[] {details.getPastSigningCertificates()[0]}
                    : signers;
            boolean same = sameSignatures(stated, expected)
                    && (statedInfo == null || sameSignatures(statedInfo.getApkContentsSigners(), signers));
            return (same ? "match" : "differ") + " (" + (android.os.SystemClock.elapsedRealtime() - start) + " ms)";
        } catch (IOException | RuntimeException | LinkageError e) {
            return "unread (" + e + ")";
        } finally {
            if (base != null) {
                try {
                    base.close();
                } catch (IOException ignored) {
                }
            }
            vouched.close();
        }
    }

    private static boolean sameSignatures(android.content.pm.Signature[] a, android.content.pm.Signature[] b) {
        if (a == null || b == null || a.length == 0 || b.length == 0) return false;
        return new HashSet<>(Arrays.asList(a)).equals(new HashSet<>(Arrays.asList(b)));
    }

    /**
     * A system service this process keeps for its clients: the account manager, as Android's one
     * AccountManagerService answers every app. LocalAccounts answers each call for the calling uid, with
     * Android's account visibility, and keeps an account's secrets behind the same-certificate check, so
     * a client gets this binder only when its uid is its own: when its spawner vouched for the package
     * on this channel (WESTLAKE_CHANNEL_IDENTITY=enforce). The Play Store's process gets it either way,
     * as before. Any other client keeps the plain answers (no accounts).
     */
    private static IBinder hostService(String name, boolean verified) {
        if (!isHost() || !"account".equals(name)) return null;
        // This app's other processes (ADR-0002): one account manager per app, as Android's one system service.
        if (Binder.getCallingUid() == Process.myUid() && !AppProcesses.isSecondary()) {
            return android.os.ServiceManager.getService(name);
        }
        String[] callers = application().getPackageManager().getPackagesForUid(Binder.getCallingUid());
        boolean store = callers != null && Arrays.asList(callers).contains(PLAY_STORE);
        if (!store && !verified) {
            System.err.println(TAG + "service " + name + " refused to uid " + Binder.getCallingUid()
                    + ": its identity is not verified");
            return null;
        }
        return android.os.ServiceManager.getService(name);
    }

    /**
     * In the host, hands a broadcast to the app it names while that app is connected, and drops it otherwise: a
     * notice for the app's registered receivers only (an account listener's ACTION_VISIBLE_ACCOUNTS_CHANGED),
     * for which no app is started (deliverToClient would hold it and ask for one). False if not delivered.
     */
    public static boolean deliverToLiveClient(Intent intent) {
        if (!isHost() || intent == null) return false;
        String target = targetPackage(intent);
        if (target == null || RemotePackages.uidOf(target) < 0) return false;
        IBinder callback = sClientCallbacks.get(target);
        return callback != null && callback.isBinderAlive()
                && send(target, callback, new Held(new Intent(intent), null, null, 0, null, null, false));
    }

    /** The calling app as the APIs Play services uses describe it. */
    private static String describeCaller() throws RemoteException {
        int uid = Binder.getCallingUid();
        PackageManager manager = application().getPackageManager();
        String[] names = manager.getPackagesForUid(uid);
        String name = names == null || names.length == 0 ? null : names[0];
        String certificate = "none";
        if (name != null) {
            try {
                PackageInfo info = manager.getPackageInfo(name, PackageManager.GET_SIGNING_CERTIFICATES);
                android.content.pm.Signature[] signers = info.signingInfo.getApkContentsSigners();
                java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
                byte[] hash = digest.digest(signers[0].toByteArray());
                certificate = String.format("%02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
            } catch (PackageManager.NameNotFoundException | java.security.NoSuchAlgorithmException | RuntimeException e) {
                certificate = e.toString();
            }
        }
        return "uid=" + uid + " pid=" + Binder.getCallingPid() + " package=" + name + " certificate=" + certificate;
    }

    private static Application application() {
        return ActivityThread.currentApplication();
    }

    /**
     * This process's package, or one known to it as a guest. In this process a guest (Play Store) is
     * package details only, for the availability check: its code doesn't run here, because a failure
     * in it would end the process every app depends on (GuestHost).
     */
    private static boolean own(String packageName) {
        return application().getPackageName().equals(packageName)
                || adapter.packagemanager.GuestPackages.contains(packageName);
    }

    private static boolean runsHere(String packageName) {
        return application().getPackageName().equals(packageName);
    }

    /** A service of this process's package, or a code guest's that runs here (GuestPackages.runsAsCode). */
    private static boolean runsHere(ServiceInfo info) {
        return info != null && (runsHere(info.packageName)
                || adapter.packagemanager.GuestPackages.runsAsCode(new ComponentName(info.packageName, info.name)));
    }

    private static boolean runsHere(ComponentName component) {
        return component != null && (runsHere(component.getPackageName())
                || adapter.packagemanager.GuestPackages.runsAsCode(component));
    }

    /** Discovery replies are terminal: calling the aggregate PM here recurses GMS -> Store -> GMS. */
    private static List<ResolveInfo> localServices(Intent intent, String type, long flags) {
        List<ResolveInfo> found = new ArrayList<>(
                adapter.packagemanager.SourcePackageRegistry.queryServices(intent, type, flags));
        found.addAll(adapter.packagemanager.GuestPackages.queryServices(intent, type, flags));
        found.removeIf(info -> !runsHere(info.serviceInfo));
        return found;
    }

    private static ProviderInfo localProviderInfo(String authority, long flags) {
        if (authority == null || authority.isEmpty()) return null;
        PackageInfo info = adapter.packagemanager.SourcePackageRegistry.packageInfo(
                ownPackage(), flags | PackageManager.GET_PROVIDERS);
        if (info == null || info.providers == null) return null;
        for (ProviderInfo provider : info.providers) {
            if (provider.authority == null) continue;
            for (String declared : provider.authority.split(";")) {
                if (authority.equals(declared.trim())) return provider;
            }
        }
        return null;
    }

    /** Resolved to a component in this process, as the activity manager adapter requires. */
    private static Intent explicit(Intent intent) throws RemoteException {
        Intent resolved = new Intent(intent);
        if (resolved.getComponent() != null) {
            if (!runsHere(resolved.getComponent())) throw new IllegalArgumentException("service not run here");
            return resolved;
        }
        List<ResolveInfo> found = localServices(intent, null, 0);
        if (found.isEmpty() || found.get(0).serviceInfo == null) {
            throw new IllegalArgumentException("no service for " + intent);
        }
        ServiceInfo info = found.get(0).serviceInfo;
        if (!runsHere(info)) throw new IllegalArgumentException("not run here: " + intent);
        resolved.setComponent(new ComponentName(info.packageName, info.name));
        return resolved;
    }

    /** Binds a service of this process for a client and waits for the binder its onBind gave. */
    private static IBinder bindLocally(Intent intent, int flags) throws RemoteException {
        final Intent target = explicit(intent);
        if (!runsHere(target.getComponent())) {
            throw new IllegalArgumentException("not run here: " + intent);
        }
        final CountDownLatch connected = new CountDownLatch(1);
        final IBinder[] service = new IBinder[1];
        ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder binder) {
                service[0] = binder;
                connected.countDown();
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
            }

            @Override
            public void onNullBinding(ComponentName name) {
                connected.countDown();
            }
        };
        // This service's own identity, not the client's: a service is bound once, by the system.
        final long identity = Binder.clearCallingIdentity();
        try {
            if (!application().bindService(target, connection, flags | Context.BIND_AUTO_CREATE)) {
                throw new IllegalArgumentException("cannot bind " + target);
            }
            if (!connected.await(BIND_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                throw new IllegalStateException("no connection from " + target);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(e);
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
        return service[0];
    }

    /** authority/uid pairs already logged as a provider handed out that Android would refuse. */
    private static final java.util.Set<String> UNEXPORTED_LOGGED = java.util.concurrent.ConcurrentHashMap.newKeySet();

    private static IBinder localProvider(String authority) throws RemoteException {
        if (isHost() && LocalGservices.AUTHORITY.equals(authority)) {
            ContentProviderHolder holder = LocalGservices.holder();
            return holder != null && holder.provider != null ? holder.provider.asBinder() : null;
        }
        ProviderInfo info = localProviderInfo(authority, 0);
        if (info == null || !runsHere(info.packageName)) return null;
        // Android's activity manager hands a provider that is not exported only to its own app. Logged, not
        // refused, until ProviderInfo.exported is known to be right here for every package (ADR-0004).
        int caller = Binder.getCallingUid();
        if (!info.exported && caller != Process.myUid() && UNEXPORTED_LOGGED.add(authority + "/" + caller)) {
            System.err.println("[WL-PERM] provider:" + authority + " for uid " + caller
                    + ": would deny (not exported); Westlake hands it out");
        }
        IContentProvider provider = ActivityThread.currentActivityThread().acquireProvider(
                application(), authority, UserHandle.myUserId(), true);
        return provider == null ? null : provider.asBinder();
    }
}

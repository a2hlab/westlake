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
                SocketBinder channel = new SocketBinder(SocketBinder.connect(name), null, c.label);
                IBinder root = channel.getRemoteRoot();
                hello(c, root);
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
            data.writeStrongBinder(sCallback);
            writeApks(data, own.applicationInfo);
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
        IBinder root = root(channelFor(info.packageName));
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
            holder.provider = attributed(channelFor(info.packageName), ContentProviderNative.asInterface(binder));
            holder.noReleaseNeeded = true;
            System.err.println(TAG + "provider " + authority + " -> " + info.packageName + "/" + info.name);
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
    private static IContentProvider attributed(Channel c, IContentProvider provider) {
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
                    try {
                        return method.invoke(provider, args);
                    } catch (java.lang.reflect.InvocationTargetException e) {
                        throw e.getCause();
                    }
                });
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
     * A system service of the Play services process, for a client that is a host too: the Play Store's
     * account manager is the one there (LocalServiceBinders asks for it by name). Null if unreachable.
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
    private static void writeApks(Parcel data, ApplicationInfo app) {
        List<String> paths = new ArrayList<>();
        if (app != null && app.sourceDir != null) paths.add(app.sourceDir);
        if (app != null && app.splitSourceDirs != null) paths.addAll(Arrays.asList(app.splitSourceDirs));
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
        if (count < 0 || count > 32) return new android.os.ParcelFileDescriptor[0];
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
        String base = "/proc/self/fd/" + apks[0].getFd();
        app.sourceDir = base;
        app.publicSourceDir = base;
        if (splits > 0) {
            String[] dirs = new String[splits];
            for (int i = 0; i < splits; i++) dirs[i] = "/proc/self/fd/" + apks[i + 1].getFd();
            app.splitSourceDirs = dirs;
            app.splitPublicSourceDirs = dirs.clone();
        }
        ClientApks previous = sClientApks.put(packageName, kept);
        if (previous != null) {
            previous.restorePaths();   // its ApplicationInfo may still be held somewhere here
            closeAll(previous.files);
        }
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
            IPackageManager packages = ActivityThread.getPackageManager();
            int user = UserHandle.myUserId();
            reply.writeNoException();
            switch (code) {
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
                    reply.writeTypedObject(packages.getServiceInfo(component, data.readLong(), user), 0);
                    return true;
                }
                case PROVIDER_INFO: {
                    ComponentName component = data.readTypedObject(ComponentName.CREATOR);
                    reply.writeTypedObject(packages.getProviderInfo(component, data.readLong(), user), 0);
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
                    ProviderInfo info = packages.resolveContentProvider(authority, providerFlags, user);
                    reply.writeTypedObject(info != null && runsHere(info.packageName) ? info : null, 0);
                    return true;
                }
                case QUERY_SERVICES: {
                    Intent intent = data.readTypedObject(Intent.CREATOR);
                    String type = data.readString();
                    long packageFlags = data.readLong();
                    List<ResolveInfo> found = new ArrayList<>();
                    for (Object item : packages.queryIntentServices(intent, type, packageFlags, user).getList()) {
                        ResolveInfo resolved = (ResolveInfo) item;
                        if (resolved.serviceInfo != null && runsHere(resolved.serviceInfo)) found.add(resolved);
                    }
                    reply.writeTypedList(found);
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
                    reply.writeStrongBinder(hostService(data.readString()));
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
        private int hello(Parcel data) {
            String claimed = data.readString();
            PackageInfo info = data.readTypedObject(PackageInfo.CREATOR);
            IBinder callback = data.readStrongBinder();
            if (claimed == null || info == null || info.applicationInfo == null || !claimed.equals(info.packageName)) {
                throw new SecurityException("malformed introduction");
            }
            if (claimed.equals(application().getPackageName())) {
                // Another process of this app (ADR-0002): the same app, so the same uid, as on Android.
                int uid = Process.myUid();
                mChannel.setCaller(uid, mPeerPid);
                mIdentified = true;
                System.err.println(TAG + "process of this app pid=" + mPeerPid + " uid=" + uid);
                return uid;
            }
            ClientApks apks = exposeApks(claimed, info.applicationInfo, readApks(data));
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
     * A system service this process keeps, for a client that is a host too: the account manager, for
     * the Play Store's process only. Other apps keep the plain answers, as an app without visibility of
     * Google accounts sees on Android, and the account's secrets stay behind the same-certificate check
     * (LocalAccounts) whoever holds the binder. Each channel's package is what the client says it is,
     * so this protects nothing until appspawn-x creates the channels (ADR-0001).
     */
    private static IBinder hostService(String name) {
        if (!isHost() || !"account".equals(name)) return null;
        String[] callers = application().getPackageManager().getPackagesForUid(Binder.getCallingUid());
        if (callers == null || !Arrays.asList(callers).contains(PLAY_STORE)) {
            System.err.println(TAG + "service " + name + " refused to uid " + Binder.getCallingUid());
            return null;
        }
        return android.os.ServiceManager.getService(name);
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

    /** Resolved to a component in this process, as the activity manager adapter requires. */
    private static Intent explicit(Intent intent) throws RemoteException {
        Intent resolved = new Intent(intent);
        if (resolved.getComponent() != null) return resolved;
        List<ResolveInfo> found = ActivityThread.getPackageManager()
                .queryIntentServices(intent, null, 0, UserHandle.myUserId()).getList();
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

    private static IBinder localProvider(String authority) throws RemoteException {
        if (isHost() && LocalGservices.AUTHORITY.equals(authority)) {
            ContentProviderHolder holder = LocalGservices.holder();
            return holder != null && holder.provider != null ? holder.provider.asBinder() : null;
        }
        ProviderInfo info = ActivityThread.getPackageManager().resolveContentProvider(authority, 0, UserHandle.myUserId());
        if (info == null || !runsHere(info.packageName)) return null;
        IContentProvider provider = ActivityThread.currentActivityThread().acquireProvider(
                application(), authority, UserHandle.myUserId(), true);
        return provider == null ? null : provider.asBinder();
    }
}

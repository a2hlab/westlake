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
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Connects apps to one shared Play services process over SocketBinder (ADR-0001).
 *
 * The Play services process serves when WESTLAKE_PLAY_SERVICES_SERVE names a socket; an app connects
 * when WESTLAKE_PLAY_SERVICES_CONNECT does. An app then asks the other process about Play services'
 * packages, binds and starts its services and acquires its content providers. What comes back are
 * real binders, so client libraries talk to Play services directly from there on.
 *
 * Each app says which package it is, and nothing checks that. All Westlake processes share one uid,
 * the kernel reports only a pid, and OH's hidepid hides one app's /proc entries from another, so the
 * host can't look. An app can claim to be another until appspawn-x creates the channels, so this
 * must not hold real data before then.
 */
public final class PlayServicesChannel {
    private static final String SERVE = "WESTLAKE_PLAY_SERVICES_SERVE";
    private static final String CONNECT = "WESTLAKE_PLAY_SERVICES_CONNECT";
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
    /** Held here, since the channel only holds what it has been sent while the peer does. */
    private static final Binder sCallback = new ClientCallback();
    private static SocketBinder sChannel;
    private static final Set<String> sAbsent = Collections.synchronizedSet(new HashSet<>());
    private static final Set<String> sPresent = Collections.synchronizedSet(new HashSet<>());
    private static volatile String sRemoteRoot;
    private static volatile String sRemoteRealRoot;

    /**
     * The other process's files, as this one must name them. Each process sees its own runtime at
     * the same logical path, so a path in the other's ApplicationInfo would name this process's own
     * files. Client libraries load code from Play services' APK through it (DynamiteModule).
     */
    private static String localPath(String path) {
        String root = sRemoteRoot;
        String real = sRemoteRealRoot;
        if (path == null || root == null || real == null || !path.startsWith(root + "/")) return path;
        return real + path.substring(root.length());
    }

    private static ApplicationInfo localize(ApplicationInfo info) {
        if (info == null) return null;
        info.sourceDir = localPath(info.sourceDir);
        info.publicSourceDir = localPath(info.publicSourceDir);
        info.nativeLibraryDir = localPath(info.nativeLibraryDir);
        info.nativeLibraryRootDir = localPath(info.nativeLibraryRootDir);
        info.secondaryNativeLibraryDir = localPath(info.secondaryNativeLibraryDir);
        info.scanSourceDir = localPath(info.scanSourceDir);
        info.scanPublicSourceDir = localPath(info.scanPublicSourceDir);
        if (info.splitSourceDirs != null) {
            for (int i = 0; i < info.splitSourceDirs.length; i++) info.splitSourceDirs[i] = localPath(info.splitSourceDirs[i]);
        }
        if (info.splitPublicSourceDirs != null) {
            for (int i = 0; i < info.splitPublicSourceDirs.length; i++) info.splitPublicSourceDirs[i] = localPath(info.splitPublicSourceDirs[i]);
        }
        return info;
    }

    private static PackageInfo localize(PackageInfo info) {
        if (info != null) localize(info.applicationInfo);
        return info;
    }

    private static ServiceInfo localize(ServiceInfo info) {
        if (info != null) localize(info.applicationInfo);
        return info;
    }

    private static ProviderInfo localize(ProviderInfo info) {
        if (info != null) localize(info.applicationInfo);
        return info;
    }

    /** Whether this process is an app connected to a Play services process. */
    public static boolean isClient() {
        return System.getenv(CONNECT) != null;
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
            IBinder root = root();
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

    /** The other process's root binder, connecting if there is no live channel. Null if unreachable. */
    private static IBinder root() {
        String name = System.getenv(CONNECT);
        if (name == null) return null;
        synchronized (CLIENT_LOCK) {
            if (sChannel != null && sChannel.isAlive()) return sChannel.getRemoteRoot();
            try {
                SocketBinder channel = new SocketBinder(SocketBinder.connect(name), null, "play-services");
                IBinder root = channel.getRemoteRoot();
                hello(root);
                sChannel = channel;
                System.err.println(TAG + "connected to " + name);
                return root;
            } catch (IOException | RemoteException | RuntimeException e) {
                System.err.println(TAG + "cannot reach " + name + ": " + e);
                return null;
            }
        }
    }

    private static void hello(IBinder root) throws RemoteException {
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
            root.transact(HELLO, data, reply, 0);
            reply.readException();
            sRemoteRoot = reply.readString();
            sRemoteRealRoot = reply.readString();
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static <T extends Parcelable> T callForObject(int code, Parcelable.Creator<T> creator, Marshal args) {
        IBinder root = root();
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
            if (sPresent.contains(packageName)) return true;
            IBinder root = root();
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
            return localize(callForObject(PACKAGE_INFO, PackageInfo.CREATOR, data -> {
                data.writeString(packageName);
                data.writeLong(flags);
            }));
        }

        @Override
        public ApplicationInfo applicationInfo(String packageName, long flags) {
            return localize(callForObject(APPLICATION_INFO, ApplicationInfo.CREATOR, data -> {
                data.writeString(packageName);
                data.writeLong(flags);
            }));
        }

        @Override
        public ServiceInfo serviceInfo(ComponentName component, long flags) {
            return localize(callForObject(SERVICE_INFO, ServiceInfo.CREATOR, data -> {
                data.writeTypedObject(component, 0);
                data.writeLong(flags);
            }));
        }

        @Override
        public ProviderInfo providerInfo(ComponentName component, long flags) {
            return localize(callForObject(PROVIDER_INFO, ProviderInfo.CREATOR, data -> {
                data.writeTypedObject(component, 0);
                data.writeLong(flags);
            }));
        }

        @Override
        public ProviderInfo resolveProvider(String authority, long flags) {
            return localize(callForObject(RESOLVE_PROVIDER, ProviderInfo.CREATOR, data -> {
                data.writeString(authority);
                data.writeLong(flags);
            }));
        }

        @Override
        public List<ResolveInfo> queryServices(Intent intent, String type, long flags) {
            IBinder root = root();
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
                for (ResolveInfo item : found) localize(item.serviceInfo);
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
        Thread thread = new Thread(() -> connect(intent, flags, component, connection), "play-services-bind");
        thread.setDaemon(true);
        thread.start();
        return 1;
    }

    private static void connect(Intent intent, int flags, ComponentName component, IServiceConnection connection) {
        IBinder root = root();
        if (root == null) return;
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
            try {
                connected.linkToDeath(() -> main.post(() -> deliver(connection, component, null, true)), 0);
            } catch (RemoteException | RuntimeException e) {
                main.post(() -> deliver(connection, component, null, true));
            }
        }
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
        return callForObject(START, ComponentName.CREATOR, data -> data.writeTypedObject(intent, 0));
    }

    public static boolean stop(Intent intent) {
        IBinder root = root();
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
        if (!isClient() || authority == null) return null;
        ProviderInfo info = RemotePackages.resolveProvider(authority, PackageManager.GET_META_DATA);
        if (info == null) return null;
        IBinder root = root();
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

    // ---- Play services side ----

    /** Whether this process is the shared Play services process. */
    public static boolean isHost() {
        return System.getenv(SERVE) != null;
    }

    private static final java.util.Map<String, IBinder> sClientCallbacks = new java.util.concurrent.ConcurrentHashMap<>();

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
     * asks who made it, and that is how it knows which app it is working for. The channel it arrived on
     * says. Whether it is immutable stays with the app, so this answers as a mutable broadcast sender.
     */
    public static android.app.ActivityManager.PendingIntentInfo remoteSenderInfo(android.content.IIntentSender sender) {
        int uid = sender == null ? 0 : SocketBinder.ownerUid(sender.asBinder());
        String name = uid == 0 ? null : RemotePackages.packageForUid(uid);
        return name == null ? null : new android.app.ActivityManager.PendingIntentInfo(
                name, uid, false, android.app.ActivityManager.INTENT_SENDER_BROADCAST);
    }

    /** Sends an app's PendingIntent in that app. False if it isn't a connected app's. */
    public static boolean sendRemoteSender(android.content.IIntentSender sender, int code, Intent fillIn, String type) {
        int uid = sender == null ? 0 : SocketBinder.ownerUid(sender.asBinder());
        String name = uid == 0 ? null : RemotePackages.packageForUid(uid);
        IBinder callback = name == null ? null : sClientCallbacks.get(name);
        if (callback == null) return false;
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

    /** Starts serving if this process was asked to. Call once the Application exists. */
    public static void startHostIfAsked() {
        final String name = System.getenv(SERVE);
        if (name == null) return;
        SocketBinder.installCallingIdentity();
        announceBoot();
        Thread thread = new Thread(() -> serve(name), "play-services-host");
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
                hello(data);
                reply.writeNoException();
                reply.writeString(System.getenv(RUNTIME_ROOT));
                reply.writeString(System.getenv(REAL_ROOT));
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
                    ProviderInfo info = packages.resolveContentProvider(authority, data.readLong(), user);
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
                        if (resolved.serviceInfo != null && runsHere(resolved.serviceInfo.packageName)) found.add(resolved);
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
                default:
                    throw new IllegalArgumentException("unknown call " + code);
            }
        }

        private void hello(Parcel data) {
            String claimed = data.readString();
            PackageInfo info = data.readTypedObject(PackageInfo.CREATOR);
            IBinder callback = data.readStrongBinder();
            if (claimed == null || info == null || info.applicationInfo == null || !claimed.equals(info.packageName)) {
                throw new SecurityException("malformed introduction");
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
        }
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
        if (!runsHere(info.packageName)) throw new IllegalArgumentException("not run here: " + intent);
        resolved.setComponent(new ComponentName(info.packageName, info.name));
        return resolved;
    }

    /** Binds a service of this process for a client and waits for the binder its onBind gave. */
    private static IBinder bindLocally(Intent intent, int flags) throws RemoteException {
        final Intent target = explicit(intent);
        if (!runsHere(target.getComponent().getPackageName())) {
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
        ProviderInfo info = ActivityThread.getPackageManager().resolveContentProvider(authority, 0, UserHandle.myUserId());
        if (info == null || !runsHere(info.packageName)) return null;
        IContentProvider provider = ActivityThread.currentActivityThread().acquireProvider(
                application(), authority, UserHandle.myUserId(), true);
        return provider == null ? null : provider.asBinder();
    }
}

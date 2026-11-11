/*
 * GuestHost.java
 *
 * Runs a guest package's services and providers (adapter.packagemanager.GuestPackages: microG's
 * GmsCore and Companion) in the app's process, the way ActivityThread runs a second package's code
 * in a shared process: the guest's own LoadedApk, Application and contexts, so its classes, its
 * resources and its data directory are its own.
 *
 * Services: a bind or start of a guest service creates it as handleCreateService does, once per
 * process, and hands onBind's binder to the caller's IServiceConnection on the main looper, as
 * InProcessServiceBinder does for the app's own. Calls cross through Binder.transact on a local
 * Binder, which marshals through a Parcel, so no class of one package reaches the other.
 *
 * Providers: getContentProvider answers a guest authority with a holder marked local, and
 * ActivityThread.installProvider makes the provider in the guest's package context itself.
 */
package adapter.activity;

import android.app.ActivityThread;
import android.app.Application;
import android.app.IServiceConnection;
import android.app.LoadedApk;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.security.net.config.ApplicationConfig;
import android.util.Log;

import adapter.packagemanager.GuestPackages;

import java.util.List;
import java.util.concurrent.ConcurrentHashMap;

public final class GuestHost {

    private static final String TAG = "WL_GuestHost";

    // className -> live guest Service (created once, bound for the life of the process)
    private static final ConcurrentHashMap<String, Service> sServices = new ConcurrentHashMap<>();

    private GuestHost() {}

    /**
     * The guest service an intent names (explicitly, or by action within a guest package), or null.
     * In the shared Play services process, only a code guest's allowlisted service (Companion's
     * asset module service, GuestPackages.codeService): there a guest is otherwise details only.
     */
    public static ServiceInfo resolveService(Intent intent) {
        if (intent == null) return null;
        if (PlayServicesChannel.isHost()) return GuestPackages.codeService(intent);
        ComponentName comp = intent.getComponent();
        String pkg = comp != null ? comp.getPackageName() : intent.getPackage();
        if (!GuestPackages.contains(pkg) && !GuestPackages.answersBesidePlayServices(intent)) return null;
        List<ResolveInfo> found = GuestPackages.queryServices(intent, null, 0);
        return found.isEmpty() ? null : found.get(0).serviceInfo;
    }

    /** The guest's Application, made on first use. Main thread. */
    private static Application application(LoadedApk apk) {
        ActivityThread thread = ActivityThread.currentActivityThread();
        // makeApplicationInner sets the calling thread's context class loader to the guest's; the
        // main thread's must stay the app's (ServiceLoader and reflection libraries read it).
        Thread current = Thread.currentThread();
        ClassLoader saved = current.getContextClassLoader();
        // makeApplicationInner also hands the guest to NetworkSecurityConfigProvider
        // .handleNewApplication, which merges a second package's network security config into the
        // process's. microG's has per-domain rules, so wherever the app's cleartext default differs
        // it threw "Found multiple conflicting per-domain rules" and microG's Application was never
        // made (every GCM and advertising-ID bind failed); where it agreed, microG's config
        // replaced the app's own. The guest runs on the app's network policy: no config of its
        // own, the app's cleartext default, and the app's config left in place afterwards.
        ApplicationConfig network = ApplicationConfig.getDefaultInstance();
        if (network != null) {
            ApplicationInfo info = apk.getApplicationInfo();
            info.networkSecurityConfigRes = 0;
            if (network.isCleartextTrafficPermitted()) {
                info.flags |= ApplicationInfo.FLAG_USES_CLEARTEXT_TRAFFIC;
            } else {
                info.flags &= ~ApplicationInfo.FLAG_USES_CLEARTEXT_TRAFFIC;
            }
        }
        try {
            return apk.makeApplicationInner(false, thread.getInstrumentation());
        } finally {
            current.setContextClassLoader(saved);
            if (network != null) ApplicationConfig.setDefaultInstance(network);
        }
    }

    /** The live guest service, created and onCreate'd on first use. Main thread. */
    private static Service serviceFor(ServiceInfo info, Intent intent) throws Exception {
        Service svc = sServices.get(info.name);
        if (svc != null) return svc;
        ActivityThread thread = ActivityThread.currentActivityThread();
        LoadedApk apk = thread.getPackageInfoNoCheck(info.applicationInfo);
        // ActivityThread keeps one LoadedApk per package name. In the Play services process the
        // Play Store's details are Google's APK; if its code had been loaded under that name first,
        // this would run the genuine Play Store's class of the same name, which must not run there.
        if (PlayServicesChannel.isHost() && !info.applicationInfo.sourceDir.equals(apk.getAppDir())) {
            throw new IllegalStateException(info.packageName + " is loaded from " + apk.getAppDir()
                    + ", not the guest's " + info.applicationInfo.sourceDir);
        }
        Application app = application(apk);
        svc = apk.getAppFactory().instantiateService(apk.getClassLoader(), info.name, intent);
        Context base = svc.createServiceBaseContext(thread, apk);
        // ContextImpl.setOuterContext is package-private; handleCreateService calls it so that the
        // context's getOuterContext (activity starts, dialogs) answers the service.
        java.lang.reflect.Method outer = base.getClass().getDeclaredMethod("setOuterContext", Context.class);
        outer.setAccessible(true);
        outer.invoke(base, svc);
        svc.attach(base, thread, info.name, new Binder(), app, android.app.ActivityManager.getService());
        svc.onCreate();
        sServices.put(info.name, svc);
        Log.i(TAG, "created guest service " + info.packageName + "/" + info.name);
        return svc;
    }

    /** bindService of a guest service: connected on the main looper, as Android delivers it. */
    public static void bind(final Intent intent, final ServiceInfo info, final IServiceConnection connection) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                ComponentName comp = new ComponentName(info.packageName, info.name);
                try {
                    Intent args = new Intent(intent);
                    args.setComponent(comp);
                    Service svc = serviceFor(info, args);
                    args.setExtrasClassLoader(svc.getClassLoader());
                    IBinder binder = svc.onBind(args);
                    if (binder != null) {
                        binder = new GuestBinder(binder, comp.flattenToShortString(), PlayServicesChannel.isHost());
                    }
                    connection.connected(comp, binder, false);
                    System.err.println("[WL-GUEST] bound " + comp.flattenToShortString() + " binder=" + binder);
                } catch (Throwable t) {
                    Log.e(TAG, "guest bind failed for " + comp, t);
                    System.err.println("[WL-GUEST] bind failed for " + comp.flattenToShortString() + ": " + t);
                }
            }
        });
    }

    /**
     * A guest service's binder as the app sees it, with the process boundary's semantics that a
     * local Binder.transact lacks: a oneway call returns at once and runs later, in order, on the
     * guest's own thread (Companion's checkLicenseV2 is oneway and goes to the network), and a
     * failure in the guest reaches the caller as the death of the service process
     * (DeadObjectException) rather than as an exception thrown through the caller's own code.
     *
     * In the shared Play services process the caller is another process, across SocketBinder,
     * which already gives oneway calls their semantics; there each call runs on the channel's
     * thread, because the caller's identity (Binder.getCallingUid) belongs to that thread and
     * Companion checks it on every call. Nothing a guest throws, Errors included, may leave the call:
     * it would end the process every app depends on.
     */
    private static final class GuestBinder extends Binder {
        private static final java.util.concurrent.ExecutorService ONEWAY =
                java.util.concurrent.Executors.newSingleThreadExecutor(r -> new Thread(r, "WL-guest-oneway"));
        private final IBinder mTarget;
        private final String mName;
        private final boolean mCallerThread;

        GuestBinder(IBinder target, String name, boolean callerThread) {
            mTarget = target;
            mName = name;
            mCallerThread = callerThread;
        }

        @Override
        public String getInterfaceDescriptor() {
            try {
                return mTarget.getInterfaceDescriptor();
            } catch (android.os.RemoteException e) {
                return null;
            }
        }

        @Override
        protected boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags)
                throws android.os.RemoteException {
            if (mCallerThread) {
                try {
                    return mTarget.transact(code, data, reply, flags);
                } catch (Throwable t) {
                    Log.e(TAG, "call " + code + " to " + mName + " failed", t);
                    if ((flags & FLAG_ONEWAY) != 0) return true;
                    throw new android.os.DeadObjectException("guest " + mName + " failed: " + t);
                }
            }
            if ((flags & FLAG_ONEWAY) != 0) {
                final android.os.Parcel copy = android.os.Parcel.obtain();
                copy.appendFrom(data, 0, data.dataSize());
                final int c = code, f = flags;
                ONEWAY.execute(() -> {
                    try {
                        mTarget.transact(c, copy, null, f);
                    } catch (Throwable t) {
                        Log.w(TAG, "oneway call " + c + " to " + mName + " failed", t);
                    } finally {
                        copy.recycle();
                    }
                });
                return true;
            }
            try {
                return mTarget.transact(code, data, reply, flags);
            } catch (RuntimeException | Error e) {
                Log.e(TAG, "call " + code + " to " + mName + " failed", e);
                throw new android.os.DeadObjectException("guest " + mName + " failed: " + e);
            }
        }
    }

    /**
     * getContentProvider for a guest authority: a holder marked local, which ActivityThread installs
     * by making the provider in the guest's package context (installProvider's own path for a
     * provider of another package in this process). Null when no guest declares the authority.
     */
    public static android.app.ContentProviderHolder provider(String authority) {
        // In the shared Play services process, only a code guest's allowlisted provider (Companion's
        // settings, which its asset module service reads).
        ProviderInfo info = PlayServicesChannel.isHost() ? GuestPackages.codeProvider(authority)
                : GuestPackages.resolveProvider(authority, android.content.pm.PackageManager.GET_META_DATA);
        if (info == null) return null;
        android.app.ContentProviderHolder holder = new android.app.ContentProviderHolder(info);
        holder.mLocal = true;
        holder.noReleaseNeeded = true;
        System.err.println("[WL-GUEST] provider " + authority + " -> " + info.packageName + "/" + info.name);
        return holder;
    }
}

/*
 * InProcessServiceBinder.java
 *
 * [FIX-AUDIO 2026-06-30] In-app Android services (e.g. noice
 * SoundPlaybackService) are NOT OHOS abilities, so routing bindService to OH
 * ConnectAbility fails and the app blocks forever waiting for
 * onServiceConnected. This binds such services IN-PROCESS the way
 * ActivityThread.handleBindService does: instantiate the Service, attach +
 * onCreate + onBind, then deliver the binder to the app's IServiceConnection.
 *
 * Runs on the main (Looper) thread, posted from
 * ActivityManagerAdapter.bindService, matching AOSP's asynchronous bind
 * contract (onServiceConnected is never delivered synchronously inside
 * bindService).
 *
 * Started services (startService, startForegroundService) run here too: the direct-launch
 * IActivityManager answered them null, so an app's own service was never created. Draw Anywhere
 * starts the service that puts its overlay up and finishes its activity; nothing came up.
 *
 * Top-level (not anonymous/nested) so d8 dexes it without enclosing-method
 * issues.
 */
package adapter.activity;

import android.app.Application;
import android.app.ActivityThread;
import android.app.Service;
import android.app.IServiceConnection;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class InProcessServiceBinder implements Runnable {

    private static final String TAG = "OH_InProcSvc";

    // className -> live Service instance (created once, reused across binds)
    private static final ConcurrentHashMap<String, Service> sServices =
            new ConcurrentHashMap<>();
    // className -> the last start id handed to onStartCommand, numbered from 1 as ActiveServices does
    private static final ConcurrentHashMap<String, Integer> sStartIds = new ConcurrentHashMap<>();
    // Services bound in process. A bound service outlives stopService and stopSelf, as in AOSP;
    // nothing tracks an unbind here, so one stays bound for the life of the process.
    private static final Set<String> sBound = ConcurrentHashMap.newKeySet();
    // className -> why it cannot be created here: a class that does not load or link (a library the
    // platform has and this runtime does not) fails the same way every time. Play services started
    // its GoogleLocationManagerService about 68 times a second, each start failing on
    // com.android.location.provider.GeofenceProvider with a 25-line stack trace in the log: its main
    // thread spent 70% of a core on it and hilogd 40%, for the whole of a round. Such a service is
    // answered at once as no such service, with one line the first time.
    private static final ConcurrentHashMap<String, String> sUnloadable = new ConcurrentHashMap<>();

    /** Whether t, or a cause of it, is a class that cannot be found or linked. */
    private static boolean unloadable(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            if (c instanceof LinkageError || c instanceof ClassNotFoundException) return true;
        }
        return false;
    }

    private static void noteUnloadable(String cls, Throwable t) {
        if (!unloadable(t)) return;
        Throwable root = t;
        while (root.getCause() != null) root = root.getCause();
        if (sUnloadable.putIfAbsent(cls, String.valueOf(root)) == null) {
            Log.e(TAG, cls + " cannot be created here (" + root + "); later starts and binds answer at once");
        }
    }

    private final Intent mService;
    private final ComponentName mComp;
    private final IServiceConnection mConnection;

    public InProcessServiceBinder(Intent service, ComponentName comp,
            IServiceConnection connection) {
        this.mService = service;
        this.mComp = comp;
        this.mConnection = connection;
    }

    /** True if comp targets a service in this app's own package. */
    public static boolean isInApp(ComponentName comp) {
        if (comp == null) return false;
        Application app = ActivityThread.currentApplication();
        if (app == null) return false;
        String pkg = app.getPackageName();
        return pkg != null && pkg.equals(comp.getPackageName());
    }

    /** The live instance of an in-app service, created and onCreate'd on first use. Main thread. */
    private static Service serviceFor(ComponentName comp, Intent intent) throws Exception {
        String cls = comp.getClassName();
        Service svc = sServices.get(cls);
        if (svc != null) return svc;
        String reason = sUnloadable.get(cls);
        if (reason != null) throw new IllegalStateException(cls + " cannot be created here: " + reason);
        Application app = ActivityThread.currentApplication();
        ActivityThread thread = ActivityThread.currentActivityThread();
        try {
            Class<?> svcClass = app.getClassLoader().loadClass(cls);
            svc = (Service) svcClass.newInstance();
            // The last argument is the IActivityManager the service reports to. Null made
            // Service.startForeground NPE on setServiceForeground -- every foreground service
            // (Termux's session service, media players, downloaders) died in onCreate. Hand it
            // the process's own manager, as ActivityThread.handleCreateService does.
            svc.attach(app, thread, cls, new Binder(), app,
                    android.app.ActivityManager.getService());
            svc.onCreate();
        } catch (Throwable t) {
            noteUnloadable(cls, t);
            throw t;
        }
        sServices.put(cls, svc);
        Log.i(TAG, "created in-process service " + cls);
        return svc;
    }

    @Override
    public void run() {
        // Known not to be creatable here: no connection, as after the first failure, without the work.
        if (sUnloadable.containsKey(mComp.getClassName())) return;
        try {
            Service svc = serviceFor(mComp, mService);
            sBound.add(mComp.getClassName());
            IBinder binder = svc.onBind(mService);
            mConnection.connected(mComp, binder, false);
            Log.i(TAG, "connected " + mComp.getClassName() + " binder=" + binder);
        } catch (Throwable t) {
            Log.e(TAG, "in-process bind failed for " + mComp, t);
        }
    }

    /**
     * A binding the system makes to an in-app service, as JobScheduler binds a JobService: the
     * service is created on first use and stays bound for the life of the process, and this
     * returns what its onBind answers. Main thread.
     */
    static IBinder bindForSystem(ComponentName comp, Intent intent) throws Exception {
        Service svc = serviceFor(comp, intent);
        sBound.add(comp.getClassName());
        return svc.onBind(intent);
    }

    /**
     * The in-app service an explicit or package-scoped service intent names, or null when it names
     * none of this app's declared services (what ActivityManager answers for a service it cannot
     * find).
     */
    public static ComponentName resolveInApp(Intent intent) {
        if (intent == null) return null;
        Application app = ActivityThread.currentApplication();
        if (app == null) return null;
        ComponentName comp = intent.getComponent();
        try {
            if (comp == null) {
                if (!app.getPackageName().equals(intent.getPackage())) return null;
                ResolveInfo info = app.getPackageManager().resolveService(intent, 0);
                if (info == null || info.serviceInfo == null) return null;
                comp = new ComponentName(info.serviceInfo.packageName, info.serviceInfo.name);
            }
            if (!isInApp(comp)) return null;
            app.getPackageManager().getServiceInfo(comp, 0);
            return comp;
        } catch (Throwable t) {
            return null;
        }
    }

    /**
     * startService / startForegroundService for an in-app service: on the main thread after the
     * caller's message, as ActivityThread.handleCreateService and handleServiceArgs run them, the
     * service is created once and gets onStartCommand with the next start id. Returns the service's
     * component, which is what ContextImpl hands back to the caller.
     */
    public static ComponentName start(final Intent intent) {
        final ComponentName comp = resolveInApp(intent);
        if (comp == null) return null;
        if (sUnloadable.containsKey(comp.getClassName())) return null;   // as for no such service
        final Intent args = new Intent(intent);
        args.setComponent(comp);
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    Service svc = serviceFor(comp, args);
                    args.setExtrasClassLoader(svc.getClassLoader());
                    int startId = sStartIds.merge(comp.getClassName(), 1, Integer::sum);
                    svc.onStartCommand(args, 0, startId);
                    Log.i(TAG, "started " + comp.getClassName() + " startId=" + startId);
                } catch (Throwable t) {
                    Log.e(TAG, "in-process start failed for " + comp, t);
                }
            }
        });
        return comp;
    }

    /**
     * stopService (startId -1) and Service.stopSelf / stopSelfResult: a started service that is not
     * bound is destroyed, and for a start id, only if it is the last one handed out. Returns whether
     * it was stopped.
     */
    public static boolean stop(ComponentName comp, int startId) {
        if (comp == null) return false;
        final String cls = comp.getClassName();
        final Service svc = sServices.get(cls);
        if (svc == null) return false;
        Integer last = sStartIds.get(cls);
        if (startId >= 0 && (last == null || last != startId)) return false;
        if (sBound.contains(cls)) return false;
        sServices.remove(cls);
        sStartIds.remove(cls);
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                try {
                    svc.onDestroy();
                    Log.i(TAG, "destroyed " + cls);
                } catch (Throwable t) {
                    Log.e(TAG, "in-process stop failed for " + cls, t);
                }
            }
        });
        return true;
    }
}

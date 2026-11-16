package adapter.activity;

import adapter.core.SocketBinder;
import adapter.packagemanager.SourcePackageRegistry;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityThread;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/** Activity/result transport for verified, explicitly connected package hosts. */
public final class RemoteActivities {
    private static final String RESULT = "adapter.activity.RemoteActivityResult";
    private static final String CANCEL = "adapter.activity.RemoteActivityCancel";
    private static final Set<Outgoing> OUTGOING = new HashSet<>();
    private static final Set<Incoming> INCOMING = new HashSet<>();
    private static final int LIMIT = 32;

    private RemoteActivities() {}

    public static boolean enabled() {
        return "1".equals(System.getenv("WESTLAKE_REMOTE_ACTIVITIES"));
    }

    private static Handler main() { return Handler.createAsync(Looper.getMainLooper()); }

    /** Query through the same target namespace used for launch; never through the auth provider. */
    public static List<ResolveInfo> query(Intent intent, String type, long flags) {
        return enabled() ? PlayServicesChannel.queryRemoteActivities(intent, type, flags)
                : java.util.Collections.emptyList();
    }

    /** null means this intent is outside the explicitly configured host route. */
    static Integer start(Intent intent, String type, IBinder callerToken, ActivityResults.Request request) {
        if (!enabled() || !PlayServicesChannel.routesActivity(intent)) return null;
        if (callerToken == null) return ActivityManager.START_PERMISSION_DENIED;
        ActivityThread thread = ActivityThread.currentActivityThread();
        Activity caller = thread == null ? null : thread.getActivity(callerToken);
        if (caller == null || caller.isFinishing() || caller.isDestroyed()) return ActivityManager.START_CANCELED;
        List<ResolveInfo> found = query(intent, type, PackageManager.MATCH_DEFAULT_ONLY);
        if (found.isEmpty()) return ActivityManager.START_INTENT_NOT_RESOLVED;
        ActivityInfo target = found.get(0).activityInfo;
        IBinder host = PlayServicesChannel.activityProcess(target);
        if (host == null) return ActivityManager.START_CANCELED;
        Outgoing pending = new Outgoing(host, callerToken, request);
        synchronized (OUTGOING) {
            if (OUTGOING.size() >= LIMIT) return ActivityManager.START_CANCELED;
            OUTGOING.add(pending);
        }
        try {
            host.linkToDeath(pending, 0);
            Intent selected = new Intent(intent).setComponent(new ComponentName(target.packageName, target.name));
            IBinder cancellation = PlayServicesChannel.startRemoteActivity(host, selected,
                    caller.getComponentName(), request != null, pending);
            if (cancellation == null) {
                pending.abandon();
                return ActivityManager.START_CANCELED;
            }
            pending.cancellation = cancellation;
            if (pending.done.get()) pending.cancelRemote();
            event("start_sent target=" + target.packageName + " process=" + target.processName);
            return ActivityManager.START_SUCCESS;
        } catch (RemoteException | RuntimeException error) {
            pending.abandon();
            event("start_failed type=" + error.getClass().getSimpleName());
            return ActivityManager.START_CANCELED;
        }
    }

    /** Called on the target host after its channel has verified the client APK and signer. */
    static List<ResolveInfo> resolveHere(Intent intent, String type, long flags, boolean sameApp) {
        List<ResolveInfo> allowed = new ArrayList<>();
        if (!enabled() || intent == null) return allowed;
        for (ResolveInfo match : SourcePackageRegistry.queryActivities(intent, type, flags)) {
            ActivityInfo info = match.activityInfo;
            if (info == null || !info.enabled || info.applicationInfo == null || !info.applicationInfo.enabled) continue;
            // Permission-bearing foreign activities need a trusted grant service, not permissions
            // self-reported in HELLO or the shared native uid. Fail closed until that exists.
            if (!sameApp && (!info.exported || (info.permission != null && !info.permission.isEmpty()))) continue;
            allowed.add(match);
        }
        return allowed;
    }

    static IBinder accept(Intent intent, ComponentName statedCaller, boolean wantsResult, IBinder callback,
            String verifiedPackage, int verifiedUid, boolean sameApp) throws RemoteException {
        if (!enabled() || callback == null || !SocketBinder.isCallingPeer(callback))
            throw new SecurityException("activity callback must belong to the verified caller channel");
        if (statedCaller == null || !verifiedPackage.equals(statedCaller.getPackageName()))
            throw new SecurityException("activity caller package mismatch");
        int grants = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;
        if (intent == null || (intent.getFlags() & grants) != 0)
            throw new SecurityException("remote activity URI grants are not implemented");
        List<ResolveInfo> matches = resolveHere(intent, intent.getType(), PackageManager.MATCH_DEFAULT_ONLY, sameApp);
        if (matches.isEmpty()) throw new SecurityException("activity is not an enabled exported target");
        ActivityInfo info = matches.get(0).activityInfo;
        if (!AppProcesses.runsHere(AppProcesses.processOf(info)))
            throw new SecurityException("activity sent to the wrong process");
        Intent launch = new Intent(intent);
        launch.setExtrasClassLoader(ActivityThread.currentApplication().getClassLoader());
        String actualClass = info.targetActivity == null ? info.name : info.targetActivity;
        launch.setComponent(new ComponentName(info.packageName, actualClass));
        Incoming pending = new Incoming(callback, statedCaller, verifiedUid, wantsResult);
        synchronized (INCOMING) {
            if (INCOMING.size() >= LIMIT) throw new IllegalStateException("too many remote activities");
            INCOMING.add(pending);
        }
        try { callback.linkToDeath(pending, 0); }
        catch (RemoteException | RuntimeException error) { pending.complete(0, null, false, false); throw error; }
        main().post(() -> {
            if (pending.done.get()) return;
            event("launch_received caller=" + verifiedPackage + " process=" + AppProcesses.self()
                    + " class=" + actualClass);
            AppSchedulerBridge.launchRemoteActivity(launch, new ActivityResults.Request(pending));
        });
        return pending;
    }

    /** Explicit interactive requests allow their host's nested UI until the request finishes. */
    static boolean interactive() { synchronized (INCOMING) { return !INCOMING.isEmpty(); } }

    /** Account-picker metadata may be changed only for this live, verified UI request's caller. */
    public static boolean isInteractiveCaller(String packageName) {
        synchronized (INCOMING) {
            for (Incoming request : INCOMING)
                if (!request.done.get() && request.caller.getPackageName().equals(packageName)) return true;
        }
        return false;
    }

    static void callerDestroyed(IBinder token) {
        List<Outgoing> cancel = new ArrayList<>();
        synchronized (OUTGOING) { for (Outgoing p : OUTGOING) if (p.callerToken == token) cancel.add(p); }
        for (Outgoing pending : cancel) { pending.abandon(); pending.cancelRemote(); }
    }

    static final class Incoming extends Binder implements IBinder.DeathRecipient {
        final IBinder callback;
        final ComponentName caller;
        final int callerUid;
        final boolean wantsResult;
        final AtomicBoolean done = new AtomicBoolean();
        volatile IBinder activityToken;

        Incoming(IBinder callback, ComponentName caller, int uid, boolean wantsResult) {
            this.callback = callback; this.caller = caller; this.callerUid = uid; this.wantsResult = wantsResult;
        }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != 1 || !SocketBinder.isCallingPeer(callback)) throw new SecurityException("wrong cancellation peer");
            data.enforceInterface(CANCEL);
            complete(Activity.RESULT_CANCELED, null, false, true);
            if (reply != null) reply.writeNoException();
            return true;
        }

        @Override public void binderDied() { complete(Activity.RESULT_CANCELED, null, false, true); }

        void complete(int result, Intent intent, boolean notify, boolean close) {
            if (!done.compareAndSet(false, true)) return;
            synchronized (INCOMING) { INCOMING.remove(this); }
            adapter.core.GoogleAuthProxy.clearInteractiveVisibility(caller.getPackageName());
            callback.unlinkToDeath(this, 0);
            if (notify) {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(RESULT); data.writeInt(result); data.writeTypedObject(intent, 0);
                    callback.transact(1, data, null, IBinder.FLAG_ONEWAY);
                    event("result_sent caller=" + caller.getPackageName() + " result_code=" + result);
                } catch (RemoteException | RuntimeException error) { event("result_peer_unavailable=true"); }
                finally { data.recycle(); }
            }
            if (close) main().post(() -> {
                ActivityThread thread = ActivityThread.currentActivityThread();
                Activity activity = thread == null || activityToken == null ? null : thread.getActivity(activityToken);
                if (activity != null && !activity.isFinishing()) activity.finish();
            });
        }
    }

    private static final class Outgoing extends Binder implements IBinder.DeathRecipient {
        final IBinder host;
        final IBinder callerToken;
        final ActivityResults.Request request;
        final AtomicBoolean done = new AtomicBoolean();
        volatile IBinder cancellation;

        Outgoing(IBinder host, IBinder token, ActivityResults.Request request) {
            this.host = host; this.callerToken = token; this.request = request;
        }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != 1 || !SocketBinder.isCallingPeer(host)) throw new SecurityException("wrong result peer");
            data.enforceInterface(RESULT);
            int result = data.readInt(); Intent intent = data.readTypedObject(Intent.CREATOR);
            finish(result, intent);
            if (reply != null) reply.writeNoException();
            return true;
        }

        @Override public void binderDied() { finish(Activity.RESULT_CANCELED, null); }

        void finish(int result, Intent data) {
            if (!done.compareAndSet(false, true)) return;
            release();
            main().post(() -> {
                if (data != null) data.setExtrasClassLoader(ActivityThread.currentApplication().getClassLoader());
                event("result_received result_code=" + result);
                if (request != null) ActivityResults.sendNow(request.resultTo, request.resultWho,
                        request.requestCode, result, data);
            });
        }

        void abandon() { if (done.compareAndSet(false, true)) release(); }
        private void release() {
            synchronized (OUTGOING) { OUTGOING.remove(this); }
            host.unlinkToDeath(this, 0);
        }
        void cancelRemote() {
            IBinder target = cancellation;
            if (target == null) return;
            Parcel data = Parcel.obtain();
            try { data.writeInterfaceToken(CANCEL); target.transact(1, data, null, IBinder.FLAG_ONEWAY); }
            catch (RemoteException | RuntimeException ignored) {}
            finally { data.recycle(); }
        }
    }

    static void event(String value) { System.err.println("[WL-REMOTE-ACTIVITY] time=" + System.currentTimeMillis() + " " + value); }
}

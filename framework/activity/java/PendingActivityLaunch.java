package adapter.activity;

import adapter.core.SocketBinder;
import android.app.ActivityThread;
import android.content.ComponentName;
import android.content.IIntentSender;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.IBinder;
import android.os.Binder;
import android.os.Parcel;
import android.os.RemoteException;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Execute a held activity capability inside its verified creator namespace, retaining the real caller. */
final class PendingActivityLaunch {
    private static final ThreadPoolExecutor WORKERS = new ThreadPoolExecutor(2, 2, 30, TimeUnit.SECONDS,
            new ArrayBlockingQueue<>(32), task -> {
                Thread thread = new Thread(task, "PendingActivityLaunch"); thread.setDaemon(true); return thread;
            }, new ThreadPoolExecutor.AbortPolicy());

    private PendingActivityLaunch() {}

    static IBinder accept(IBinder sender, Intent fillIn, int mask, int values, ComponentName caller,
            boolean wantsResult, IBinder callback, PackageInfo origin, int pid, String spawner, int uid)
            throws RemoteException {
        if (!RemoteActivities.enabled() || callback == null || !SocketBinder.isCallingPeer(callback)
                || caller == null || !origin.packageName.equals(caller.getPackageName()))
            throw new SecurityException("pending activity caller is not verified");
        Relay relay = new Relay(callback);
        callback.linkToDeath(relay, 0);
        // Preparation stays inside the creator's namespace. A worker starts a new execution operation;
        // it does not expose the same-app Intent-reading transaction to the initiating foreign call.
        Future<IBinder> future;
        try { future = WORKERS.submit(() -> {
            Intent intent = LocalIntentSenders.prepareForExecution(IIntentSender.Stub.asInterface(sender),
                    fillIn, mask, values);
            if (intent == null || relay.done.get()) { relay.stop(); return null; }
            List<ResolveInfo> matches = RemoteActivities.resolveHere(intent, intent.getType(),
                    PackageManager.MATCH_DEFAULT_ONLY, true);
            if (matches.isEmpty()) throw new SecurityException("pending activity target unavailable");
            ActivityInfo info = matches.get(0).activityInfo;
            if (!ActivityThread.currentPackageName().equals(info.packageName))
                throw new SecurityException("pending activity leaves creator namespace");
            intent.setComponent(new ComponentName(info.packageName, info.name));
            IBinder launched;
            if (AppProcesses.runsHere(AppProcesses.processOf(info))) {
                launched = RemoteActivities.launchVerified(intent, caller, wantsResult, relay,
                        origin, pid, spawner, uid, true);
            } else {
                IBinder host = PlayServicesChannel.ownActivityProcess(info);
                if (host == null || relay.done.get()) { relay.stop(); return null; }
                relay.host = host;
                host.linkToDeath(relay.hostDeath, 0);
                if (relay.done.get()) { relay.release(); return null; }
                launched = PlayServicesChannel.startCreatorActivity(host, intent, caller, wantsResult,
                        relay, origin, pid, spawner, uid);
            }
            relay.target = launched;
            if (relay.done.get()) { relay.release(); cancel(launched); }
            if (launched == null) { relay.stop(); return null; }
            return relay;
        }); } catch (RuntimeException error) { relay.stop(); throw error; }
        try {
            return future.get(15, TimeUnit.SECONDS);
        } catch (Exception error) {
            relay.stop();
            future.cancel(true);
            if (error instanceof InterruptedException) Thread.currentThread().interrupt();
            RemoteActivities.event("pending_activity_creator_failed type=" + error.getClass().getSimpleName());
            return null;
        }
    }

    /** Forward death as well as parcels: a forwarded binder's transport peer can outlive its owner. */
    private static final class Relay extends Binder implements IBinder.DeathRecipient {
        final IBinder callback;
        final AtomicBoolean done = new AtomicBoolean();
        volatile IBinder host, target;
        final IBinder.DeathRecipient hostDeath = () -> finish(0, null);

        Relay(IBinder callback) { this.callback = callback; }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != 1) throw new SecurityException("unknown pending activity operation");
            if (SocketBinder.isCallingPeer(callback)) {
                data.enforceInterface("adapter.activity.RemoteActivityCancel");
                stop();
            } else {
                IBinder expected = host;
                if (expected == null ? SocketBinder.callingPeer() != null : !SocketBinder.isCallingPeer(expected))
                    throw new SecurityException("wrong pending activity result peer");
                data.enforceInterface("adapter.activity.RemoteActivityResult");
                finish(data.readInt(), data.readTypedObject(Intent.CREATOR));
            }
            if (reply != null) reply.writeNoException();
            return true;
        }

        @Override public void binderDied() { stop(); }

        void stop() {
            if (!done.compareAndSet(false, true)) return;
            release(); cancel(target);
        }

        void finish(int result, Intent intent) {
            if (!done.compareAndSet(false, true)) return;
            release();
            Parcel data = Parcel.obtain();
            try {
                data.writeInterfaceToken("adapter.activity.RemoteActivityResult");
                data.writeInt(result); data.writeTypedObject(intent, 0);
                callback.transact(1, data, null, IBinder.FLAG_ONEWAY);
            } catch (RemoteException | RuntimeException ignored) {
            } finally { data.recycle(); }
        }

        private void release() {
            callback.unlinkToDeath(this, 0);
            IBinder expected = host;
            if (expected != null) expected.unlinkToDeath(hostDeath, 0);
        }
    }

    private static void cancel(IBinder target) {
        if (target == null) return;
        if (target instanceof RemoteActivities.Incoming) {
            ((RemoteActivities.Incoming) target).complete(0, null, false, true);
            return;
        }
        Parcel data = Parcel.obtain();
        try {
            data.writeInterfaceToken("adapter.activity.RemoteActivityCancel");
            target.transact(1, data, null, IBinder.FLAG_ONEWAY);
        } catch (RemoteException | RuntimeException ignored) {
        } finally { data.recycle(); }
    }
}

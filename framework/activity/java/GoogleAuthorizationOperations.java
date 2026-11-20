package adapter.activity;

import adapter.core.GoogleAuthDelegation;
import adapter.core.GoogleAuthProxy;
import adapter.core.GoogleAuthorizationLeases;
import adapter.core.SocketBinder;
import android.content.pm.PackageInfo;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;

/**
 * Experimental mediation for a verified app's own asynchronous Authorization API request
 * (IAuthorizationService.authorize, for example a server auth code after a chooser sign-in).
 *
 * Play services checks which accounts the app may see on a worker, after the incoming call has
 * returned, so GoogleAuthProxy no longer sees the call as being served and answers "none". While the
 * request is live (until its callback, its caller's death or a deadline), GoogleAuthProxy may answer
 * from the app's own signer-pinned grant instead. Tokens still pass the broker's delegation policy.
 */
public final class GoogleAuthorizationOperations {
    static final String SERVICE = "com.google.android.gms.auth.api.identity.internal.IAuthorizationService";
    private static final String CALLBACK = "com.google.android.gms.auth.api.identity.internal.IAuthorizationCallback";
    private static final GoogleAuthorizationLeases LEASES = new GoogleAuthorizationLeases();
    private GoogleAuthorizationOperations() {}

    static boolean enabled() {
        return GoogleAuthOperations.enabled() && "1".equals(System.getenv("WESTLAKE_GOOGLE_ASYNC_AUTHORIZATION"));
    }

    /** The signer of target's live authorization request, for GoogleAuthProxy in Play services; else null. */
    public static String liveSigner(String target) {
        if (!enabled() || target == null) return null;
        return LEASES.liveSigner(target, SystemClock.elapsedRealtime());
    }

    /** Called only after HELLO independently verifies the original APK, signer and live spawner. */
    static SocketBinder.CallScope enter(PackageInfo origin, SocketBinder channel, IBinder target, int code,
            Parcel data, int flags) throws RemoteException {
        if (!enabled() || code != 1 || !SERVICE.equals(target.getInterfaceDescriptor())) return null;
        int position = data.dataPosition();
        Operation operation = null;
        try {
            if (!AppProcesses.runsHere("com.google.android.gms") || (flags & IBinder.FLAG_ONEWAY) != 0)
                throw new SecurityException("authorization request unsupported");
            if (origin.signingInfo == null || origin.signingInfo.getApkContentsSigners().length != 1)
                throw new SecurityException("ambiguous origin signer");
            data.enforceInterface(SERVICE);
            IBinder callback = data.readStrongBinder();
            if (callback == null || !SocketBinder.isCallingPeer(callback)
                    || !CALLBACK.equals(callback.getInterfaceDescriptor()))
                throw new SecurityException("wrong callback peer");
            String signer = GoogleAuthProxy.digest(origin.signingInfo.getApkContentsSigners()[0].toByteArray(), "SHA-256");
            operation = new Operation(callback);
            final Operation pending = operation;
            pending.lease = LEASES.open(origin.packageName, signer, SystemClock.elapsedRealtime(),
                    () -> channel.isAlive() && callback.isBinderAlive());
            // IAuthorizationCallback code 1 delivers the result: the request ends there.
            pending.observer = SocketBinder.observeCallback(callback, 1, pending::close);
            callback.linkToDeath(pending, 0);
            GoogleAuthDelegation.event("async_authorization_accepted=true owner=" + origin.packageName);
            return new SocketBinder.CallScope() {
                boolean accepted;
                @Override public void returned(boolean handled, Parcel reply) {
                    int at = reply.dataPosition();
                    try { reply.setDataPosition(0); accepted = handled && reply.dataAvail() >= 4 && reply.readInt() == 0; }
                    finally { reply.setDataPosition(at); }
                }
                @Override public void close() { if (!accepted) pending.close(); }
            };
        } catch (Exception denied) {
            if (operation != null) operation.close();
            GoogleAuthDelegation.event("async_authorization_denied=true " + GoogleAuthDelegation.failure(denied));
        } finally { data.setDataPosition(position); }
        return null;
    }

    private static final class Operation implements IBinder.DeathRecipient {
        final IBinder callback;
        final java.util.concurrent.atomic.AtomicBoolean closed = new java.util.concurrent.atomic.AtomicBoolean();
        volatile GoogleAuthorizationLeases.Lease lease;
        volatile SocketBinder.CallScope observer;

        Operation(IBinder callback) { this.callback = callback; }

        @Override public void binderDied() { close(); }

        void close() {
            if (!closed.compareAndSet(false, true)) return;
            LEASES.close(lease);
            SocketBinder.CallScope watching = observer;
            if (watching != null) watching.close();
            try { callback.unlinkToDeath(this, 0); } catch (RuntimeException notLinked) { }
        }
    }
}

package adapter.activity;

import adapter.core.GoogleAsyncAuthLease;
import adapter.core.GoogleAuthDelegation;
import adapter.core.GoogleAuthDelegationPolicy;
import adapter.core.GoogleAuthProxy;
import adapter.core.GoogleSignInOptionsReader;
import adapter.core.SocketBinder;
import android.accounts.Account;
import android.content.pm.PackageInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;
import java.util.ArrayList;
import java.util.List;

/** Experimental mediation for the pinned official GMS legacy silentSignIn request. */
final class GoogleAsyncAuthOperations {
    private static final String SERVICE = "com.google.android.gms.auth.api.signin.internal.ISignInService";
    private static final String CALLBACK = "com.google.android.gms.auth.api.signin.internal.ISignInCallbacks";
    private static final List<Operation> PENDING = new ArrayList<>();
    private GoogleAsyncAuthOperations() {}

    static boolean enabled() {
        return GoogleAuthOperations.enabled() && "1".equals(System.getenv("WESTLAKE_GOOGLE_ASYNC_SIGNIN"));
    }

    /** Called only after HELLO independently verifies the original APK, signer and live spawner. */
    static SocketBinder.CallScope enter(PackageInfo origin, int pid, String spawner, SocketBinder channel,
            IBinder target, int code, Parcel data, int flags) throws RemoteException {
        if (!enabled() || code != 101 || !SERVICE.equals(target.getInterfaceDescriptor())) return null;
        int position = data.dataPosition();
        Operation operation = null;
        try {
            if (!AppProcesses.runsHere("com.google.android.gms") || (flags & IBinder.FLAG_ONEWAY) != 0)
                throw new SecurityException("silent request unsupported");
            data.enforceInterface(SERVICE);
            IBinder callback = data.readStrongBinder();
            if (!SocketBinder.isCallingPeer(callback) || !CALLBACK.equals(callback.getInterfaceDescriptor()))
                throw new SecurityException("wrong witness peer");
            GoogleSignInOptionsReader options = GoogleSignInOptionsReader.read(new GoogleSignInOptionsReader.Reader() {
                public int position() { return data.dataPosition(); }
                public int size() { return data.dataSize(); }
                public int integer() { return data.readInt(); }
                public String string() { return data.readString(); }
                public String[] account() {
                    Account value = Account.CREATOR.createFromParcel(data);
                    return new String[] {value.name, value.type};
                }
            });
            operation = new Operation(origin, pid, spawner, channel, callback, options);
            final Operation pending = operation;
            List<Operation> old;
            synchronized (PENDING) { old = new ArrayList<>(PENDING); }
            for (Operation expired : old) if (!expired.live()) expired.close();
            synchronized (PENDING) {
                if (PENDING.size() >= 16) throw new SecurityException("too many auth operations");
                PENDING.add(pending);
            }
            pending.observer = SocketBinder.observeCallback(callback, 101, pending::close);
            callback.linkToDeath(pending, 0);
            // One bounded timeout also releases references if GMS never calls the callback.
            pending.timeoutHandler.postDelayed(pending.timeout, 100_000L);
            if (!pending.live()) pending.close();
            GoogleAuthDelegation.event("async_signin_accepted=true owner=" + origin.packageName
                    + " scope_count=" + options.scopes.size() + " account_explicit=" + (options.accountName != null));
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
            GoogleAuthDelegation.event("async_signin_denied=true " + GoogleAuthDelegation.failure(denied));
        } finally { data.setDataPosition(position); }
        return null;
    }

    /** Only the verified official GMS main process may service its own workers' cache reads. */
    static Bundle read(String method, Account account, String input) {
        Bundle answer = new Bundle();
        if (!enabled() || !AppProcesses.runsHere("com.google.android.gms") || account == null || input == null) return answer;
        boolean token = "peekAuthToken".equals(method);
        boolean expiry = "getUserData".equals(method) && input.startsWith("EXP:");
        boolean scopes = "getUserData".equals(method) && input.startsWith("SCOPES:");
        if (!token && !expiry && !scopes) return answer;
        try {
            String key = token ? input : input.substring(expiry ? 4 : 7);
            GoogleAuthDelegationPolicy request = GoogleAuthDelegationPolicy.parse(key);
            List<Operation> candidates;
            synchronized (PENDING) { candidates = new ArrayList<>(PENDING); }
            Operation selected = null;
            for (Operation candidate : candidates) {
                if (!candidate.origin.packageName.equals(request.owner) || !candidate.live()) continue;
                answer.putBoolean("handled", true);
                if (!candidate.allowed(account, request)) continue;
                // Do not guess which concurrent same-app operation owns an internal GMS request.
                if (selected != null) {
                    GoogleAuthDelegation.event("async_signin_ambiguous=true owner=" + request.owner);
                    return answer;
                }
                selected = candidate;
            }
            if (selected != null) answer.putString("value", selected.read(method, account, key, request, token, expiry));
        } catch (Exception denied) {
            GoogleAuthDelegation.event("async_signin_read_denied=true " + GoogleAuthDelegation.failure(denied));
        }
        return answer;
    }

    private static final class Operation implements IBinder.DeathRecipient {
        final PackageInfo origin;
        final int pid;
        final String spawner;
        final SocketBinder channel;
        final IBinder callback;
        final GoogleAsyncAuthLease lease;
        final android.os.Handler timeoutHandler = android.os.Handler.createAsync(android.os.Looper.getMainLooper());
        final Runnable timeout = this::close;
        final java.util.concurrent.atomic.AtomicBoolean closed = new java.util.concurrent.atomic.AtomicBoolean();
        volatile SocketBinder.CallScope observer;
        volatile Bundle issued;
        volatile String issuedKey;

        Operation(PackageInfo origin, int pid, String spawner, SocketBinder channel,
                IBinder callback, GoogleSignInOptionsReader options) throws Exception {
            this.origin = origin; this.pid = pid; this.spawner = spawner; this.channel = channel; this.callback = callback;
            if (origin.signingInfo == null || origin.signingInfo.getApkContentsSigners().length != 1
                    || origin.signatures == null || origin.signatures.length != 1)
                throw new SecurityException("ambiguous origin signer");
            lease = new GoogleAsyncAuthLease(GoogleAuthOperations.directory(), origin.packageName,
                    GoogleAuthProxy.digest(origin.signingInfo.getApkContentsSigners()[0].toByteArray(), "SHA-256"),
                    GoogleAuthProxy.digest(origin.signatures[0].toByteArray(), "SHA-1"), options.accountName,
                    "oauth2:" + String.join(" ", options.scopes), SystemClock.elapsedRealtime());
        }

        boolean live() { return lease.live(SystemClock.elapsedRealtime(), channel.isAlive() && callback.isBinderAlive()); }
        boolean allowed(Account account, GoogleAuthDelegationPolicy request) {
            return lease.allows(account.type, account.name, request, SystemClock.elapsedRealtime(),
                    channel.isAlive() && callback.isBinderAlive());
        }

        String read(String method, Account account, String key, GoogleAuthDelegationPolicy request,
                boolean token, boolean expiry) throws RemoteException {
            if (!allowed(account, request)) return null;
            if (!token) {
                Bundle saved = issued;
                return saved != null && key.equals(issuedKey) ? saved.getString(expiry ? "expiry" : "scopes") : null;
            }
            IBinder witness = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                    if (code != 1 || !PlayServicesChannel.authProviderCalling()) throw new SecurityException("wrong witness peer");
                    data.enforceInterface(GoogleAuthDelegation.WITNESS);
                    reply.writeNoException(); reply.writeBoolean(allowed(account, request)); return true;
                }
            };
            Bundle result = PlayServicesChannel.delegatedAuth(origin, pid, spawner, account, key, witness, null);
            if (!allowed(account, request) || result == null || result.getString("token") == null) return null;
            issuedKey = key; issued = result;
            GoogleAuthDelegation.event("async_signin_token=true owner=" + origin.packageName + " token_present=true");
            return result.getString("token");
        }

        @Override public void binderDied() { close(); }
        void close() {
            timeoutHandler.removeCallbacks(timeout);
            if (!closed.compareAndSet(false, true)) return;
            lease.close(); issued = null; issuedKey = null;
            SocketBinder.CallScope watching = observer;
            if (watching != null) watching.close();
            callback.unlinkToDeath(this, 0);
            synchronized (PENDING) { PENDING.remove(this); }
        }
    }
}

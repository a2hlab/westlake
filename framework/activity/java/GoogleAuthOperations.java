package adapter.activity;

import adapter.core.GoogleAuthDelegation;
import adapter.core.GoogleAuthDelegationPolicy;
import adapter.core.GoogleAuthProxy;
import adapter.core.GoogleAuthSessions;
import adapter.core.SocketBinder;
import android.accounts.Account;
import android.app.ActivityThread;
import android.content.pm.PackageInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;
import java.nio.file.Path;

/** A verified, synchronous legacy-auth call owns its capability; ending a picker grants no background access. */
public final class GoogleAuthOperations {
    private static final String AUTH = "com.google.android.auth.IAuthManagerService";
    // Lookup only: authority is the explicit Operation, verified socket, saved selection and provider policy.
    private static final ThreadLocal<Operation> CURRENT = new ThreadLocal<>();
    private GoogleAuthOperations() {}

    public static boolean enabled() {
        return GoogleAuthDelegation.enabled() && "1".equals(System.getenv("WESTLAKE_GOOGLE_SILENT_AUTH"))
                && "com.google.android.gms".equals(ActivityThread.currentPackageName())
                && !GoogleAuthProxy.provider();
    }

    static Path directory() {
        return ActivityThread.currentApplication().getDataDir().toPath().resolve("westlake-auth-sessions");
    }

    static String generation(String owner) throws Exception { return GoogleAuthSessions.generation(directory(), owner); }

    public static void revoke(String owner) throws Exception {
        if (enabled()) { GoogleAuthSessions.revoke(directory(), owner); GoogleAuthDelegation.event("session_revoked=true"); }
    }

    static void remember(PackageInfo origin, Account account, String providerKey, String epoch) throws Exception {
        if (!enabled()) return;
        GoogleAuthSessions.grant(directory(), origin.packageName,
                GoogleAuthProxy.digest(origin.signingInfo.getApkContentsSigners()[0].toByteArray(), "SHA-256"),
                GoogleAuthProxy.digest(origin.signatures[0].toByteArray(), "SHA-1"),
                account.type, account.name, providerKey, epoch);
        GoogleAuthDelegation.event("session_granted=true owner=" + origin.packageName);
    }

    /** Installed only by HELLO after independent APK/spawner verification, never from request extras. */
    static SocketBinder.CallScope enter(PackageInfo origin, int pid, String spawner, SocketBinder channel,
            IBinder target, int code, Parcel data, int flags) throws RemoteException {
        if (!enabled() || code != 5 || !AUTH.equals(target.getInterfaceDescriptor())) return null;
        Operation operation = new Operation(origin, pid, spawner, channel, CURRENT.get());
        int position = data.dataPosition();
        try {
            if ((flags & IBinder.FLAG_ONEWAY) != 0) throw new SecurityException("silent oneway unsupported");
            data.enforceInterface(AUTH);
            operation.account = data.readTypedObject(Account.CREATOR);
            operation.service = data.readString();
            Bundle extras = data.readTypedObject(Bundle.CREATOR);
            if (data.dataAvail() != 0 || operation.account == null || !"com.google".equals(operation.account.type)
                    || operation.service == null || operation.service.length() > 512
                    || !operation.service.matches("audience:server:client_id:[A-Za-z0-9_-]+\\.apps\\.googleusercontent\\.com"))
                throw new SecurityException("silent request unsupported");
            if (extras != null) for (String key : extras.keySet()) {
                if ("androidPackageName".equals(key) || "clientPackageName".equals(key)) {
                    if (!origin.packageName.equals(extras.getString(key))) throw new SecurityException("silent origin mismatch");
                } else if ("service_connection_start_time_millis".equals(key)) {
                    if (!(extras.get(key) instanceof Long)) throw new SecurityException("silent timing invalid");
                } else throw new SecurityException("silent options unsupported");
            }
            operation.epoch = generation(origin.packageName);
            operation.accepted = true;
        } catch (Exception error) {
            // Still install a denying scope so an unsupported foreign call cannot borrow a live picker.
            GoogleAuthDelegation.event("silent_request_denied=true " + GoogleAuthDelegation.failure(error));
        } finally { data.setDataPosition(position); }
        CURRENT.set(operation);
        return operation;
    }

    static boolean present() { return CURRENT.get() != null; }

    static String read(String method, Account account, String key) {
        Operation operation = CURRENT.get();
        if (operation == null || !SocketBinder.isCallingChannel(operation.channel)) return null;
        return operation.read(method, account, key);
    }

    private static final class Operation implements SocketBinder.CallScope {
        final PackageInfo origin;
        final int pid;
        final String spawner;
        final SocketBinder channel;
        final Operation outer;
        final long deadline = SystemClock.elapsedRealtime() + 100_000L;
        final String signer, legacySigner;
        volatile boolean active = true;
        boolean accepted;
        Account account;
        String service, epoch, issuedKey;
        Bundle issued;

        Operation(PackageInfo origin, int pid, String spawner, SocketBinder channel, Operation outer) {
            this.origin = origin; this.pid = pid; this.spawner = spawner; this.channel = channel; this.outer = outer;
            try {
                if (origin.signingInfo == null || origin.signingInfo.getApkContentsSigners().length != 1
                        || origin.signatures == null || origin.signatures.length != 1)
                    throw new SecurityException("ambiguous origin signer");
                signer = GoogleAuthProxy.digest(origin.signingInfo.getApkContentsSigners()[0].toByteArray(), "SHA-256");
                legacySigner = GoogleAuthProxy.digest(origin.signatures[0].toByteArray(), "SHA-1");
            } catch (Exception error) { throw new SecurityException("origin signer unavailable"); }
        }

        boolean allowed(GoogleAuthDelegationPolicy request) {
            try {
                return accepted && active && channel.isAlive() && SystemClock.elapsedRealtime() < deadline
                        && service.equals(request.service) && GoogleAuthSessions.allows(directory(), origin.packageName,
                        signer, legacySigner, account.type, account.name, request, epoch);
            } catch (Exception denied) { return false; }
        }

        String read(String method, Account requested, String input) {
            try {
                boolean token = "peekAuthToken".equals(method);
                boolean expiry = "getUserData".equals(method) && input != null && input.startsWith("EXP:");
                boolean scopes = "getUserData".equals(method) && input != null && input.startsWith("SCOPES:");
                if ((!token && !expiry && !scopes) || account == null || !account.equals(requested)) return null;
                String key = token ? input : input.substring(expiry ? 4 : 7);
                GoogleAuthDelegationPolicy parsed = GoogleAuthDelegationPolicy.parse(key);
                if (!allowed(parsed)) {
                    GoogleAuthDelegation.event("silent_read_denied=true owner=" + origin.packageName);
                    return null;
                }
                if (!token) return key.equals(issuedKey) && issued != null ? issued.getString(expiry ? "expiry" : "scopes") : null;
                IBinder witness = new Binder() {
                    @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                        if (code != 1 || !PlayServicesChannel.authProviderCalling()) throw new SecurityException("wrong witness peer");
                        data.enforceInterface(GoogleAuthDelegation.WITNESS);
                        reply.writeNoException(); reply.writeBoolean(allowed(parsed)); return true;
                    }
                };
                Bundle result = PlayServicesChannel.delegatedAuth(origin, pid, spawner, requested, key, witness, null);
                if (!allowed(parsed) || result == null || result.getString("token") == null) return null;
                issuedKey = key; issued = result;
                GoogleAuthDelegation.event("silent_token=true owner=" + origin.packageName + " token_present=true");
                return result.getString("token");
            } catch (Exception error) {
                GoogleAuthDelegation.event("silent_read_denied=true " + GoogleAuthDelegation.failure(error));
                return null;
            }
        }

        @Override public void close() {
            active = false; issued = null; issuedKey = null;
            if (outer == null) CURRENT.remove(); else CURRENT.set(outer);
        }
    }
}

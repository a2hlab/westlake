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
    static SocketBinder.CallScope enter(PackageInfo origin, int pid, String spawner, SocketBinder channel,
            IBinder target, int code, Parcel data, int flags) throws RemoteException {
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
            if ("1".equals(System.getenv("WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE"))) traceRequest(data, origin.packageName);
            if ("1".equals(System.getenv(ACCOUNT_ID_FLAG))) nameAccountById(data, origin.packageName);
            String signer = GoogleAuthProxy.digest(origin.signingInfo.getApkContentsSigners()[0].toByteArray(), "SHA-256");
            String[] facts = requestFacts(data);
            String legacySigner = origin.signatures != null && origin.signatures.length == 1
                    ? GoogleAuthProxy.digest(origin.signatures[0].toByteArray(), "SHA-1") : null;
            operation = new Operation(callback, origin, pid, spawner,
                    facts[0] == null ? null : new android.accounts.Account(facts[0], facts[1]), facts[2], legacySigner);
            final Operation pending = operation;
            pending.lease = LEASES.open(origin.packageName, signer, SystemClock.elapsedRealtime(),
                    () -> channel.isAlive() && callback.isBinderAlive());
            synchronized (OPERATIONS) { OPERATIONS.add(pending); }
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

    private static final java.util.List<Operation> OPERATIONS = new java.util.ArrayList<>();
    private static final int REQUEST_CLIENT_FIELD = 2;

    /** The request's account name and type and its server client ID (fields 5 and 2); nulls if absent. */
    private static String[] requestFacts(Parcel data) {
        String[] facts = new String[3];
        int position = data.dataPosition();
        try {
            if (data.readInt() == 0) return facts;
            int header = data.readInt();
            if ((header & 0xFFFF) != SAFE_PARCEL_MAGIC || (header & 0xFFFF0000) != 0xFFFF0000) return facts;
            int size = data.readInt();
            int end = data.dataPosition() + size;
            while (data.dataPosition() < end) {
                int field = data.readInt();
                int length = (field & 0xFFFF0000) == 0xFFFF0000 ? data.readInt() : field >>> 16;
                int start = data.dataPosition();
                if ((field & 0xFFFF) == REQUEST_ACCOUNT_FIELD) { facts[0] = data.readString(); facts[1] = data.readString(); }
                else if ((field & 0xFFFF) == REQUEST_CLIENT_FIELD) facts[2] = data.readString();
                data.setDataPosition(start + length);
            }
        } catch (RuntimeException unreadable) {
            return new String[3];
        } finally {
            data.setDataPosition(position);
        }
        return facts;
    }

    /**
     * Play services reads the app's own token for this live request from its token cache (peekAuthToken, and
     * EXP:/SCOPES: beside it), possibly from :persistent through the launched process
     * (PlayServicesChannel.readAsyncAuth). Answered only for the request's own app and signer, the account it
     * names, its own server client ID, and while it is live; the broker still applies the app's grant.
     */
    static android.os.Bundle read(String method, android.accounts.Account account, String input) {
        android.os.Bundle answer = new android.os.Bundle();
        if (!enabled() || !"1".equals(System.getenv(TOKEN_FLAG)) || account == null || input == null) return answer;
        boolean token = "peekAuthToken".equals(method);
        boolean expiry = "getUserData".equals(method) && input.startsWith("EXP:");
        boolean scopes = "getUserData".equals(method) && input.startsWith("SCOPES:");
        if (!token && !expiry && !scopes) return answer;
        try {
            String key = token ? input : input.substring(expiry ? 4 : 7);
            adapter.core.GoogleAuthDelegationPolicy request = adapter.core.GoogleAuthDelegationPolicy.parse(key);
            java.util.List<Operation> candidates;
            synchronized (OPERATIONS) { candidates = new java.util.ArrayList<>(OPERATIONS); }
            Operation selected = null;
            for (Operation candidate : candidates) {
                if (!candidate.origin.packageName.equals(request.owner) || !candidate.live()) continue;
                answer.putBoolean("handled", true);
                if (!candidate.allowed(account, request)) continue;
                if (selected != null) {
                    GoogleAuthDelegation.event("async_authorization_ambiguous=true owner=" + request.owner);
                    return answer;
                }
                selected = candidate;
            }
            if (selected != null) answer.putString("value", selected.read(account, key, request, token, expiry));
        } catch (Exception denied) {
            GoogleAuthDelegation.event("async_authorization_read_denied=true " + GoogleAuthDelegation.failure(denied));
        }
        return answer;
    }

    /** Token reads for a live request are answered only with this experiment flag as well. */
    static final String TOKEN_FLAG = "WESTLAKE_GOOGLE_AUTHORIZATION_TOKENS";

    static final String ACCOUNT_ID_FLAG = "WESTLAKE_GOOGLE_AUTHORIZATION_ACCOUNT_ID";
    private static final int SAFE_PARCEL_MAGIC = 20293;
    private static final int REQUEST_ACCOUNT_FIELD = 5;

    /**
     * Experiment (user-approved 2026-11-20): an app that names its account by its numeric Google ID.
     *
     * On the uncertified board Play services' sign-in UI cannot sync the account's details, so a Sign in with
     * Google credential carries the account's numeric ID where Android has its email. Apps pass that id to the
     * Authorization API as the account (WEBTOON: new Account(credential.getId(), "com.google")), and Play
     * services refuses it as an account the app cannot see. When the name is exactly the Google ID of the one
     * account the app's own grant already shows it, the request names that account instead. Nothing else in
     * the request changes, no account the app is not granted can be named, and names are never logged.
     * Off unless WESTLAKE_GOOGLE_AUTHORIZATION_ACCOUNT_ID=1.
     *
     * data is positioned at the request (after the callback); on return it holds the same transaction with
     * only AuthorizationRequest field 5 (the Account) replaced, and its position is unchanged.
     */
    private static void nameAccountById(Parcel data, String owner) {
        int position = data.dataPosition();
        String outcome;
        try {
            outcome = renameRequestAccount(data, owner, position);
        } catch (RuntimeException unreadable) {
            outcome = "unreadable_" + unreadable.getClass().getSimpleName();
        } finally {
            data.setDataPosition(position);
        }
        GoogleAuthDelegation.event("async_authorization_account_id owner=" + owner + " outcome=" + outcome);
    }

    private static String renameRequestAccount(Parcel data, String owner, int requestStart) {
        if (data.readInt() == 0) return "no_request";
        int header = data.readInt();
        if ((header & 0xFFFF) != SAFE_PARCEL_MAGIC || (header & 0xFFFF0000) != 0xFFFF0000) return "not_safe_parcel";
        int objectSizeAt = data.dataPosition();
        int objectSize = data.readInt();
        int fieldsStart = data.dataPosition();
        int objectEnd = fieldsStart + objectSize;
        if (objectSize < 0 || objectEnd > data.dataSize()) return "bad_size";
        int accountAt = -1, accountEnd = -1;
        String name = null, type = null;
        while (data.dataPosition() < objectEnd) {
            int at = data.dataPosition();
            int field = data.readInt();
            int length = (field & 0xFFFF0000) == 0xFFFF0000 ? data.readInt() : field >>> 16;
            int start = data.dataPosition();
            if (length < 0 || start + length > objectEnd) return "bad_field";
            if ((field & 0xFFFF) == REQUEST_ACCOUNT_FIELD) {
                accountAt = at; accountEnd = start + length;
                name = data.readString(); type = data.readString();
            }
            data.setDataPosition(start + length);
        }
        if (accountAt < 0 || name == null) return "no_account";
        if (!"com.google".equals(type) || name.indexOf('@') >= 0 || !name.matches("[0-9]{6,40}")) return "not_an_id";
        android.accounts.AccountManager manager = android.accounts.AccountManager.get(
                android.app.ActivityThread.currentApplication());
        android.accounts.Account match = null;
        // The accounts this app's own grant shows it (asked while serving the app's own call).
        for (android.accounts.Account granted : manager.getAccountsByTypeForPackage("com.google", owner)) {
            if (name.equals(manager.getUserData(granted, "GoogleUserId"))) {
                if (match != null) return "ambiguous";
                match = granted;
            }
        }
        if (match == null) return "no_granted_account_with_id";
        Parcel rebuilt = Parcel.obtain();
        try {
            // Everything before the account field as it was (interface header, callback, earlier fields).
            rebuilt.appendFrom(data, 0, accountAt);
            rebuilt.writeInt(0xFFFF0000 | REQUEST_ACCOUNT_FIELD);
            int lengthAt = rebuilt.dataPosition();
            rebuilt.writeInt(0);
            int accountStart = rebuilt.dataPosition();
            new android.accounts.Account(match.name, match.type).writeToParcel(rebuilt, 0);
            int accountStop = rebuilt.dataPosition();
            rebuilt.setDataPosition(lengthAt);
            rebuilt.writeInt(accountStop - accountStart);
            rebuilt.setDataPosition(accountStop);
            // The later fields and anything after the request, unchanged.
            rebuilt.appendFrom(data, accountEnd, data.dataSize() - accountEnd);
            int shift = (accountStop - accountAt) - (accountEnd - accountAt);
            rebuilt.setDataPosition(objectSizeAt);
            rebuilt.writeInt(objectSize + shift);
            data.setDataSize(0);
            data.appendFrom(rebuilt, 0, rebuilt.dataSize());
        } finally {
            rebuilt.recycle();
        }
        return "translated";
    }

    /**
     * Value-free description of the request (AuthorizationRequest's SafeParcel fields: 3 offline access,
     * 5 account, 6 hosted domain): only whether its account is one Play services sees, never names.
     */
    private static void traceRequest(Parcel data, String owner) {
        int position = data.dataPosition();
        try {
            if (data.readInt() == 0) { GoogleAuthDelegation.event("async_authorization_request=absent owner=" + owner); return; }
            int header = data.readInt();
            if ((header & 0xFFFF) != 20293) throw new IllegalArgumentException("not a safe parcel");
            int size = (header & 0xFFFF0000) == 0xFFFF0000 ? data.readInt() : header >>> 16;
            int end = data.dataPosition() + size;
            String name = null, type = null; boolean offline = false, hosted = false;
            while (data.dataPosition() < end) {
                int field = data.readInt();
                int length = (field & 0xFFFF0000) == 0xFFFF0000 ? data.readInt() : field >>> 16;
                int start = data.dataPosition();
                switch (field & 0xFFFF) {
                    case 3: offline = data.readInt() != 0; break;
                    case 5: name = data.readString(); type = data.readString(); break;
                    case 6: String domain = data.readString(); hosted = domain != null && !domain.isEmpty(); break;
                    default: break;
                }
                data.setDataPosition(start + length);
            }
            boolean exact = false, folded = false;
            if (name != null) {
                for (android.accounts.Account seen : android.accounts.AccountManager.get(
                        android.app.ActivityThread.currentApplication()).getAccountsByType("com.google")) {
                    exact |= name.equals(seen.name);
                    folded |= name.equalsIgnoreCase(seen.name);
                }
            }
            GoogleAuthDelegation.event("async_authorization_request owner=" + owner + " account_present=" + (name != null)
                    + " account_type_google=" + "com.google".equals(type) + " matches_seen_account=" + exact
                    + " matches_ignoring_case=" + folded + " name_length=" + (name == null ? 0 : name.length())
                    + " has_at=" + (name != null && name.indexOf('@') > 0) + " offline=" + offline + " hosted_domain=" + hosted);
        } catch (RuntimeException unreadable) {
            GoogleAuthDelegation.event("async_authorization_request=unreadable " + unreadable.getClass().getSimpleName());
        } finally {
            data.setDataPosition(position);
        }
    }

    private static final class Operation implements IBinder.DeathRecipient {
        final IBinder callback;
        final PackageInfo origin;
        final int pid;
        final String spawner;
        final android.accounts.Account account;
        final String serverClientId;
        final String legacySigner;
        final java.util.concurrent.atomic.AtomicBoolean closed = new java.util.concurrent.atomic.AtomicBoolean();
        volatile GoogleAuthorizationLeases.Lease lease;
        volatile SocketBinder.CallScope observer;
        volatile android.os.Bundle issued;
        volatile String issuedKey;

        Operation(IBinder callback, PackageInfo origin, int pid, String spawner, android.accounts.Account account,
                String serverClientId, String legacySigner) {
            this.callback = callback; this.origin = origin; this.pid = pid; this.spawner = spawner;
            this.account = account; this.serverClientId = serverClientId; this.legacySigner = legacySigner;
        }

        boolean live() {
            GoogleAuthorizationLeases.Lease held = lease;
            return !closed.get() && held != null && held.live(SystemClock.elapsedRealtime());
        }

        boolean allowed(android.accounts.Account requested, adapter.core.GoogleAuthDelegationPolicy request) {
            return live() && account != null && account.equals(requested) && legacySigner != null
                    && legacySigner.equals(request.signer)
                    && (request.clientId == null || request.clientId.equals(serverClientId));
        }

        String read(android.accounts.Account requested, String key, adapter.core.GoogleAuthDelegationPolicy request,
                boolean token, boolean expiry) throws RemoteException {
            if (!allowed(requested, request)) return null;
            if (!token) {
                android.os.Bundle saved = issued;
                return saved != null && key.equals(issuedKey) ? saved.getString(expiry ? "expiry" : "scopes") : null;
            }
            IBinder witness = new android.os.Binder() {
                @Override protected boolean onTransact(int code, Parcel in, Parcel reply, int flags) {
                    if (code != 1 || !PlayServicesChannel.authProviderCalling()) throw new SecurityException("wrong witness peer");
                    in.enforceInterface(GoogleAuthDelegation.WITNESS);
                    reply.writeNoException(); reply.writeBoolean(allowed(requested, request)); return true;
                }
            };
            android.os.Bundle result = PlayServicesChannel.delegatedAuth(origin, pid, spawner, requested, key, witness, null);
            String value = result == null ? null : result.getString("token");
            GoogleAuthDelegation.event("async_authorization_token=true owner=" + origin.packageName + " kind="
                    + (request.serverCode ? "server_code" : request.clientId != null ? "id_token" : "oauth2")
                    + " token_present=" + (value != null) + " consent_required="
                    + (result != null && result.getBoolean("consentRequired")));
            if (!allowed(requested, request) || value == null) return null;
            issuedKey = key; issued = result;
            return value;
        }

        @Override public void binderDied() { close(); }

        void close() {
            if (!closed.compareAndSet(false, true)) return;
            synchronized (OPERATIONS) { OPERATIONS.remove(this); }
            issued = null; issuedKey = null;
            LEASES.close(lease);
            SocketBinder.CallScope watching = observer;
            if (watching != null) watching.close();
            try { callback.unlinkToDeath(this, 0); } catch (RuntimeException notLinked) { }
        }
    }
}

package adapter.core;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.ActivityThread;
import android.content.Context;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.StrictMode;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Experimental, live-picker-bound mediation of pinned official GMS token-cache reads. */
public final class GoogleAuthDelegation {
    public static final String WITNESS = "org.westlake.GoogleAuthSelection.v1";
    private static final Object LOCK = new Object();
    private static final ThreadPoolExecutor NETWORK = new ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS,
            new ArrayBlockingQueue<>(1), task -> {
                Thread thread = new Thread(task, "GoogleAuthDelegation-network");
                thread.setDaemon(true);
                return thread;
            });
    private GoogleAuthDelegation() {}

    public static boolean enabled() {
        return "1".equals(System.getenv("WESTLAKE_GOOGLE_AUTH_DELEGATION"))
                && "enforce".equals(System.getenv("WESTLAKE_CHANNEL_IDENTITY"))
                && "1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"));
    }

    public static String read(String method, Account account, String key) {
        if (!enabled() || !"com.google.android.gms".equals(ActivityThread.currentPackageName())) return null;
        // Unsupported background caches and arbitrary user data retain the original withheld behavior.
        if (key == null || (!method.equals("peekAuthToken") && !key.startsWith("EXP:") && !key.startsWith("SCOPES:"))) return null;
        try {
            GoogleAuthDelegationPolicy.parse(method.equals("peekAuthToken") ? key
                    : key.substring(key.startsWith("EXP:") ? 4 : 7));
        } catch (SecurityException unsupported) { return null; }
        try {
            return (String) Class.forName("adapter.activity.PlayServicesChannel")
                    .getMethod("readDelegatedAuth", String.class, Account.class, String.class)
                    .invoke(null, method, account, key);
        } catch (Exception error) {
            event("read_failed=true method=" + method);
            return null;
        }
    }

    public static void invalidate(String token) {
        if (token == null || token.isEmpty()) return;
        try {
            Class.forName("adapter.activity.PlayServicesChannel")
                    .getMethod("readDelegatedAuth", String.class, Account.class, String.class)
                    .invoke(null, "invalidateAuthToken", null, token);
        } catch (Exception error) { event("invalidate_failed=true"); }
    }

    public static void requireBroker(String packageName, byte[] currentSigner) throws Exception {
        if (!enabled() || !GoogleAuthProxy.provider()) throw new SecurityException("delegation disabled");
        GoogleAuthDelegationPolicy.requireBroker(GoogleAuthProxy.readGrants(), packageName,
                GoogleAuthProxy.digest(currentSigner, "SHA-256"));
    }

    private static void selected(IBinder witness) throws Exception {
        if (witness == null || !SocketBinder.isCallingPeer(witness))
            throw new SecurityException("selection witness must belong to verified broker channel");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(WITNESS);
            if (!witness.transact(1, data, reply, 0)) throw new SecurityException("selection witness unavailable");
            reply.readException();
            if (!reply.readBoolean()) throw new SecurityException("selection no longer active");
        } finally { data.recycle(); reply.recycle(); }
    }

    private static Object requestAuth(Class<?> type, Object manager) throws Exception {
        // SocketBinder can re-enter the original (including UI) thread while it waits for a reply.
        // Keep witness/peer checks on that inbound thread, and perform only HTTP work on this worker.
        Future<Object> request = NETWORK.submit(() -> {
            StrictMode.ThreadPolicy previous = StrictMode.getThreadPolicy();
            StrictMode.setThreadPolicy(new StrictMode.ThreadPolicy.Builder(previous).permitNetwork().build());
            try { return type.getMethod("requestAuth", boolean.class).invoke(manager, true); }
            finally { StrictMode.setThreadPolicy(previous); }
        });
        try { return request.get(90, TimeUnit.SECONDS); }
        catch (ExecutionException error) {
            Throwable cause = error.getCause();
            if (cause instanceof Exception) throw (Exception) cause;
            throw new IllegalStateException("authentication worker failed");
        } finally { if (!request.isDone()) request.cancel(true); }
    }

    /** All identities here were verified independently from the origin's root-owned live spawner. */
    public static Bundle provide(String broker, byte[] brokerSigner, String origin, byte[] currentSigner,
            byte[] legacySigner, Account requested, String key, IBinder witness, String invalidate) throws Exception {
        requireBroker(broker, brokerSigner);
        GoogleAuthDelegationPolicy parsed = GoogleAuthDelegationPolicy.parse(key);
        String sha256 = GoogleAuthProxy.digest(currentSigner, "SHA-256");
        String sha1 = GoogleAuthProxy.digest(legacySigner, "SHA-1");
        parsed.require(GoogleAuthProxy.readGrants(), origin, sha256, sha1);
        selected(witness);
        Context app = ActivityThread.currentApplication();
        Account[] accounts = AccountManager.get(app).getAccountsByType("com.google");
        if (accounts.length != 1 || !accounts[0].equals(requested)) throw new SecurityException("account not granted");
        Bundle result = new Bundle();
        synchronized (LOCK) {
            selected(witness);
            Class<?> type = app.getClassLoader().loadClass("org.microg.gms.auth.AuthManager");
            Object manager = type.getConstructor(Context.class, String.class, String.class, String.class)
                    .newInstance(app, requested.name, origin, parsed.service);
            type.getMethod("setPackageSignature", String.class).invoke(manager, sha1);
            type.getMethod("setAccountType", String.class).invoke(manager, requested.type);
            for (String option : new String[] {"include_email", "include_profile"}) {
                String value = parsed.options.get(option);
                if (value != null) type.getField(option.equals("include_email") ? "includeEmail" : "includeProfile").set(manager, value);
            }
            if (parsed.options.containsKey("token_request_options"))
                type.getMethod("setTokenRequestOptions", String.class).invoke(manager, parsed.options.get("token_request_options"));
            if (invalidate != null) {
                String cached = (String) type.getMethod("peekAuthToken").invoke(manager);
                boolean match = !invalidate.isEmpty() && invalidate.equals(cached);
                if (match) type.getMethod("invalidateAuthToken", String.class).invoke(manager, invalidate);
                event("invalidate=true owner=" + origin + " cache_removed=" + match);
                return result;
            }
            // The operator's private grant plus the live account selection is local scope permission.
            // Google server consent is still required; a consent response is never converted to success.
            type.getMethod("setPermitted", boolean.class).invoke(manager, true);
            type.getField("includeGrantedScopes").set(manager, "0");
            // Cache reads do not carry original freshness intent. Always refresh in this experiment.
            type.getField("forceRefreshToken").setBoolean(manager, true);
            if (parsed.serverCode) {
                type.getMethod("setOauth2Prompt", String.class).invoke(manager, "auto");
                type.getMethod("setItCaveatTypes", String.class).invoke(manager, "2");
            }
            Object response = requestAuth(type, manager);
            Class<?> responseType = response.getClass();
            String token = (String) responseType.getField("auth").get(response);
            String advice = (String) responseType.getField("issueAdvice").get(response);
            boolean consent = "remote_consent".equals(advice);
            long expiry = ((Number) responseType.getField("expiry").get(response)).longValue();
            String scopes = (String) responseType.getField("grantedScopes").get(response);
            // Recheck both revocation and selection after network I/O, before releasing a credential.
            requireBroker(broker, brokerSigner);
            parsed.require(GoogleAuthProxy.readGrants(), origin, sha256, sha1);
            parsed.requireGrantedScopes(GoogleAuthProxy.readGrants(), scopes);
            selected(witness);
            if (!consent && token != null && !token.isEmpty() && (expiry <= 0 || expiry > System.currentTimeMillis() / 1000L)) {
                result.putString("token", token);
                if (expiry > 0) result.putString("expiry", Long.toString(expiry));
                if (scopes != null) result.putString("scopes", scopes);
            }
            result.putBoolean("consentRequired", consent);
            event("token=true owner=" + origin + " kind=" + (parsed.serverCode ? "server_code" : parsed.clientId != null ? "id_token" : "oauth2")
                    + " token_present=" + result.containsKey("token") + " expiry_present=" + result.containsKey("expiry")
                    + " consent_required=" + consent);
        }
        return result;
    }

    public static void event(String detail) {
        System.err.println("[WL-AUTH-DELEGATION] time=" + System.currentTimeMillis() + " " + detail);
    }

    /** Never prints an arbitrary exception message, HTTP response body, key, account or token. */
    public static String failure(Throwable error) {
        while (error instanceof java.lang.reflect.InvocationTargetException && error.getCause() != null) error = error.getCause();
        String detail = "type=" + error.getClass().getSimpleName();
        String message = error.getMessage();
        for (String known : new String[] {"origin mismatch", "origin not granted", "client id not granted", "scope not granted",
                "opaque option not granted", "broker not granted", "key identity", "key query", "unsupported option",
                "scope syntax", "unsupported service", "selection no longer active", "wrong witness peer",
                "cache signer differs from origin", "origin signer mismatch", "origin launch mismatch", "origin is not live"}) {
            if (known.equals(message)) detail += " reason=" + known.replace(' ', '_');
        }
        if (message != null) for (String code : new String[] {"BadAuthentication", "NeedsBrowser", "NeedsPermission",
                "UNREGISTERED_ON_API_CONSOLE", "INVALID_CLIENT", "DeviceManagementRequired", "ServiceDisabled", "CaptchaRequired"}) {
            if (java.util.regex.Pattern.compile("(?m)^Error=" + code + "\\r?$").matcher(message).find()) detail += " server_error=" + code;
        }
        int count = 0;
        for (StackTraceElement frame : error.getStackTrace()) {
            String name = frame.getClassName() + "." + frame.getMethodName();
            if (name.matches("[A-Za-z0-9_.$<>]+")) detail += " frame" + count++ + "=" + name;
            if (count == 3) break;
        }
        return detail;
    }
}

package adapter.core;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AuthenticatorDescription;
import android.accounts.IAccountManagerResponse;
import android.app.ActivityThread;
import android.content.Context;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.io.File;
import java.io.FileInputStream;
import java.lang.reflect.InvocationTargetException;
import java.security.MessageDigest;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Opt-in, token-only microG provider. Separate package namespaces retain their own APK and data.
 * Launch attestation and certificate checks are mandatory in PlayServicesChannel before this binder
 * can be obtained. No operation exposes the reusable credential, arbitrary user data or token cache.
 * Only the explicitly granted account scopes are supported; other auth protocols fail closed.
 */
public final class GoogleAuthProxy {
    public static final String DESCRIPTOR = "org.westlake.IGoogleAuthProxy.v1";
    private static final int ACCOUNTS = 1, TOKEN = 2, INVALIDATE = 3, FEATURES = 4;
    private static final ExecutorService REQUESTS = Executors.newCachedThreadPool();
    private static final AtomicLong IDS = new AtomicLong();
    private static final Object TOKEN_LOCK = new Object();

    private GoogleAuthProxy() {}
    public static boolean provider() { return "microg".equals(System.getenv("WESTLAKE_GOOGLE_AUTH_PROVIDER")); }
    public static boolean client() { return System.getenv("WESTLAKE_GOOGLE_AUTH_CONNECT") != null; }

    public static String digest(byte[] bytes, String algorithm) throws Exception {
        StringBuilder out = new StringBuilder();
        for (byte b : MessageDigest.getInstance(algorithm).digest(bytes)) out.append(String.format("%02x", b & 255));
        return out.toString();
    }

    private static void event(String text) {
        System.err.println("[WL-AUTH-PROXY] time=" + System.currentTimeMillis() + " " + text);
    }

    /** Called only with the peer's APK-verified certificate and kernel peer PID. */
    public static IBinder providerBinder(String packageName, byte[] certificate, byte[] authCertificate, int pid) {
        if (!provider() || !"enforce".equals(System.getenv("WESTLAKE_CHANNEL_IDENTITY"))
                || !"1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"))) {
            throw new SecurityException("auth provider requires enforced identity and private logging");
        }
        try {
            return new Provider(packageName, certificate, authCertificate, pid);
        } catch (SecurityException e) { throw e; }
        catch (Exception e) { throw new IllegalStateException("auth provider unavailable"); }
    }

    private static final class Provider extends Binder {
        final String owner, sha256, sha1;
        final int pid;
        Provider(String owner, byte[] certificate, byte[] authCertificate, int pid) throws Exception {
            this.owner = owner; this.pid = pid;
            // Authorization pins the current APK signer. Android's legacy GET_SIGNATURES
            // identity may be an older, verified member of its signing lineage.
            sha256 = digest(certificate, "SHA-256"); sha1 = digest(authCertificate, "SHA-1");
            event("client_verified owner=" + owner + " certificate_sha256=" + sha256 + " pid=" + pid);
            GoogleAuthPolicy.require(grants(), owner, sha256, null);
        }

        private Properties grants() throws Exception {
            Properties props = new Properties();
            File file = new File(ActivityThread.currentApplication().getDataDir(), "westlake-auth-proxy/grants.properties");
            android.system.StructStat stat = android.system.Os.stat(file.getPath());
            if ((stat.st_mode & 077) != 0 || stat.st_uid != android.os.Process.myUid()) {
                throw new SecurityException("auth grants must be private and owned by the provider");
            }
            try (FileInputStream in = new FileInputStream(file)) { props.load(in); }
            return props;
        }

        private Account account(Account requested) {
            Account[] accounts = AccountManager.get(ActivityThread.currentApplication()).getAccountsByType("com.google");
            if (accounts.length != 1 || (requested != null && !accounts[0].equals(requested))) {
                throw new SecurityException("account not granted");
            }
            return accounts[0];
        }

        private Object manager(Account account, String scope) throws Exception {
            Context app = ActivityThread.currentApplication();
            Class<?> type = app.getClassLoader().loadClass("org.microg.gms.auth.AuthManager");
            Object manager = type.getConstructor(Context.class, String.class, String.class, String.class)
                    .newInstance(app, account.name, owner, scope);
            // This is the attested caller certificate, not microG's identically named self package.
            type.getMethod("setPackageSignature", String.class).invoke(manager, sha1);
            type.getMethod("setAccountType", String.class).invoke(manager, account.type);
            return manager;
        }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == INTERFACE_TRANSACTION) { reply.writeString(DESCRIPTOR); return true; }
            data.enforceInterface(DESCRIPTOR);
            if (Binder.getCallingPid() != pid) throw new SecurityException("auth capability belongs to another process");
            long request = IDS.incrementAndGet();
            try {
                Properties policy = grants(); // revocation takes effect without restarting the provider
                GoogleAuthPolicy.require(policy, owner, sha256, null);
                if (code == ACCOUNTS) {
                    Account selected = account(null);
                    reply.writeNoException(); reply.writeTypedArray(new Account[] {selected}, 0);
                    event("request=" + request + " operation=accounts owner=" + owner + " count=1");
                    return true;
                }
                if (code == FEATURES) {
                    Account selected = account(data.readTypedObject(Account.CREATOR));
                    String[] features = data.createStringArray();
                    if (features == null) features = new String[0];
                    if (features.length > 32) throw new SecurityException("too many account features");
                    for (String feature : features) {
                        if (feature == null || !feature.matches("[A-Za-z0-9_.-]{1,128}")) {
                            throw new SecurityException("invalid account feature");
                        }
                    }
                    Context app = ActivityThread.currentApplication();
                    Class<?> type = app.getClassLoader().loadClass("org.microg.gms.auth.loginservice.AccountAuthenticator");
                    Object authenticator = type.getConstructor(Context.class).newInstance(app);
                    Bundle result = (Bundle) type.getMethod("hasFeatures", android.accounts.AccountAuthenticatorResponse.class,
                            Account.class, String[].class).invoke(authenticator, null, selected, features);
                    boolean present = result.getBoolean(AccountManager.KEY_BOOLEAN_RESULT, false);
                    reply.writeNoException(); reply.writeBoolean(present);
                    event("request=" + request + " operation=features owner=" + owner
                            + " feature_count=" + features.length + " supported=" + present);
                    return true;
                }
                if (code == TOKEN) {
                    Account selected = account(data.readTypedObject(Account.CREATOR));
                    String scope = data.readString();
                    boolean force = data.readBoolean();
                    if (scope == null || scope.isEmpty()) throw new SecurityException("scope required");
                    GoogleAuthPolicy.require(policy, owner, sha256, scope);
                    Bundle result = new Bundle();
                    synchronized (TOKEN_LOCK) {
                        Object manager = manager(selected, scope);
                        Class<?> type = manager.getClass();
                        boolean cached = type.getMethod("getAuthToken").invoke(manager) != null;
                        type.getField("forceRefreshToken").setBoolean(manager, force);
                        Object response = type.getMethod("requestAuth", boolean.class).invoke(manager, true);
                        String token = (String) response.getClass().getField("auth").get(response);
                        if (token == null || token.isEmpty()) {
                            result.putInt(AccountManager.KEY_ERROR_CODE, AccountManager.ERROR_CODE_UNSUPPORTED_OPERATION);
                            result.putString(AccountManager.KEY_ERROR_MESSAGE, "consent or reauthentication required");
                        } else {
                            result.putString(AccountManager.KEY_ACCOUNT_NAME, selected.name);
                            result.putString(AccountManager.KEY_ACCOUNT_TYPE, selected.type);
                            result.putString(AccountManager.KEY_AUTHTOKEN, token);
                        }
                        result.putLong("westlake.requestId", request);
                        result.putBoolean("westlake.forceRefresh", force);
                        result.putBoolean("westlake.cachePresentBefore", cached);
                        event("request=" + request + " operation=token owner=" + owner
                                + " force_refresh=" + force + " cache_before=" + cached
                                + " token_present=" + (token != null && !token.isEmpty()));
                    }
                    reply.writeNoException(); reply.writeBundle(result); return true;
                }
                if (code == INVALIDATE) {
                    String token = data.readString();
                    boolean removed = false;
                    synchronized (TOKEN_LOCK) {
                        Account selected = account(null);
                        for (String scope : policy.getProperty("client." + owner + ".scopes", "").split(",")) {
                            if (scope.isEmpty()) continue;
                            Object manager = manager(selected, scope);
                            String cached = (String) manager.getClass().getMethod("peekAuthToken").invoke(manager);
                            if (token != null && !token.isEmpty() && token.equals(cached)) {
                                manager.getClass().getMethod("invalidateAuthToken", String.class).invoke(manager, token);
                                removed = true;
                            }
                        }
                    }
                    reply.writeNoException(); reply.writeBoolean(removed);
                    event("request=" + request + " operation=invalidate owner=" + owner + " cache_removed=" + removed);
                    return true;
                }
                throw new SecurityException("unsupported auth operation");
            } catch (SecurityException e) {
                event("request=" + request + " operation=" + code + " denied=true");
                throw e;
            } catch (Exception e) {
                Throwable cause = e instanceof InvocationTargetException ? ((InvocationTargetException) e).getCause() : e;
                event("request=" + request + " operation=" + code + " failed=" + cause.getClass().getSimpleName());
                throw new IllegalStateException("auth provider operation failed");
            }
        }
    }

    private interface Writer { void write(Parcel parcel); }
    private static Parcel call(int code, Writer writer) throws Exception {
        IBinder binder;
        try {
            binder = (IBinder) Class.forName("adapter.activity.PlayServicesChannel")
                    .getMethod("remoteAuthService").invoke(null);
        } catch (InvocationTargetException e) {
            if (e.getCause() instanceof Exception) throw (Exception) e.getCause();
            throw e;
        }
        if (binder == null) throw new RemoteException("auth provider unavailable");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR); writer.write(data);
            if (!binder.transact(code, data, reply, 0)) throw new RemoteException("unsupported auth operation");
            reply.readException(); return reply;
        } catch (Exception e) { reply.recycle(); throw e; }
        finally { data.recycle(); }
    }

    private static Account[] accounts(String type) throws Exception {
        if (type != null && !"com.google".equals(type)) return new Account[0];
        try {
            Parcel reply = call(ACCOUNTS, data -> {});
            Account[] found;
            try { found = reply.createTypedArray(Account.CREATOR); } finally { reply.recycle(); }
            rememberAccounts(found);
            return found;
        } catch (SecurityException denied) {
            // Explicit account/client revocation is not a transient provider outage.
            java.nio.file.Files.deleteIfExists(registryFile().toPath());
            throw denied;
        } catch (RemoteException unavailable) {
            Account[] known = rememberedAccounts();
            if (known == null) throw unavailable;
            event("account_registry=retained provider_unavailable=true count=" + known.length);
            return known;
        }
    }

    /** AccountManager owns account metadata even while its authenticator process is stopped. */
    private static File registryFile() {
        return new File(ActivityThread.currentApplication().getDataDir(), "westlake-auth-proxy/visible-account.properties");
    }

    private static String registryOwner() throws Exception {
        Context app = ActivityThread.currentApplication();
        android.content.pm.PackageInfo info = app.getPackageManager().getPackageInfo(app.getPackageName(),
                android.content.pm.PackageManager.GET_SIGNING_CERTIFICATES);
        android.content.pm.Signature[] signatures = info.signingInfo.getApkContentsSigners();
        if (signatures.length != 1) throw new SecurityException("account registry requires one signer");
        return app.getPackageName() + ":" + digest(signatures[0].toByteArray(), "SHA-256") + ":"
                + System.getenv("WESTLAKE_GOOGLE_AUTH_APK_SHA256") + ":" + System.getenv("WESTLAKE_GOOGLE_AUTH_CONNECT");
    }

    private static void rememberAccounts(Account[] accounts) throws Exception {
        if (accounts == null || accounts.length > 1) throw new SecurityException("invalid account registry response");
        Properties data = new Properties(); data.setProperty("owner", registryOwner());
        if (accounts.length == 1) {
            if (!"com.google".equals(accounts[0].type)) throw new SecurityException("invalid account registry type");
            data.setProperty("name", accounts[0].name);
        }
        // Only account name/owner metadata: no password, token, check-in secret or arbitrary user data.
        BootstrapTransfer.writePrivate(registryFile().toPath(), data);
    }

    private static Account[] rememberedAccounts() throws Exception {
        File file = registryFile();
        if (!file.isFile()) return null;
        android.system.StructStat stat = android.system.Os.stat(file.getPath());
        if ((stat.st_mode & 077) != 0 || stat.st_uid != android.os.Process.myUid()) return null;
        Properties data = new Properties();
        try (FileInputStream in = new FileInputStream(file)) { data.load(in); }
        if (!registryOwner().equals(data.getProperty("owner"))) return null;
        String name = data.getProperty("name");
        return name == null ? new Account[0] : new Account[] {new Account(name, "com.google")};
    }

    private static boolean hasFeatures(Account account, String[] features) throws Exception {
        Parcel reply = call(FEATURES, data -> {
            data.writeTypedObject(account, 0); data.writeStringArray(features);
        });
        try { return reply.readBoolean(); } finally { reply.recycle(); }
    }

    private static void accountFeatures(String method, Object[] args) {
        IAccountManagerResponse callback = (IAccountManagerResponse) args[0];
        REQUESTS.execute(() -> {
            try {
                Bundle result = new Bundle();
                String[] features = (String[]) args[2];
                if ("hasFeatures".equals(method)) {
                    result.putBoolean(AccountManager.KEY_BOOLEAN_RESULT, hasFeatures((Account) args[1], features));
                } else {
                    Account[] found = accounts((String) args[1]);
                    if (found.length == 1 && features != null && features.length > 0 && !hasFeatures(found[0], features)) {
                        found = new Account[0];
                    }
                    if ("getAccountsByFeatures".equals(method)) result.putParcelableArray(AccountManager.KEY_ACCOUNTS, found);
                    else if (found.length == 1) {
                        result.putString(AccountManager.KEY_ACCOUNT_NAME, found[0].name);
                        result.putString(AccountManager.KEY_ACCOUNT_TYPE, found[0].type);
                    }
                }
                callback.onResult(result);
            } catch (Exception e) {
                event("client_method=" + method + " failed=" + e.getClass().getSimpleName());
                try { callback.onError(AccountManager.ERROR_CODE_REMOTE_EXCEPTION, "account feature query failed"); }
                catch (RemoteException ignored) {}
            }
        });
    }

    /** Adapter entry point. Never falls through to the official reusable-credential store. */
    static Object answer(String method, Object[] args) {
        if (AccountManagerStub.callerPid() != android.os.Process.myPid()) {
            throw new SecurityException("remote account caller must connect to the auth provider directly");
        }
        try {
            switch (method) {
                case "getPassword":
                case "peekAuthToken":
                case "getUserData":
                    event("client_credential_read=" + method + " result=withheld"); return null;
                case "getAuthenticatorTypes":
                    return new AuthenticatorDescription[] {new AuthenticatorDescription("com.google", "com.google.android.gms", 0, 0, 0, 0, true)};
                case "getAccountsAsUser":
                case "getAccountsByTypeForPackage": return accounts((String) args[0]);
                case "getAccountsForPackage": return accounts(null);
                case "getAccountsByFeatures":
                case "getAccountByTypeAndFeatures":
                case "hasFeatures": accountFeatures(method, args); return null;
                case "getAuthToken": {
                    IAccountManagerResponse callback = (IAccountManagerResponse) args[0];
                    Account account = (Account) args[1]; String scope = (String) args[2];
                    Bundle options = args[5] == null ? new Bundle() : new Bundle((Bundle) args[5]);
                    String own = ActivityThread.currentPackageName();
                    String claimed = options.getString(AccountManager.KEY_ANDROID_PACKAGE_NAME, own);
                    if (!own.equals(claimed)) throw new SecurityException("delegated package requires an explicit contract");
                    for (String key : options.keySet()) {
                        if (!AccountManager.KEY_ANDROID_PACKAGE_NAME.equals(key) && !"westlake.forceRefresh".equals(key)) {
                            throw new SecurityException("unsupported token option");
                        }
                    }
                    REQUESTS.execute(() -> {
                        try {
                            Parcel reply = call(TOKEN, data -> {
                                data.writeTypedObject(account, 0); data.writeString(scope);
                                data.writeBoolean(options.getBoolean("westlake.forceRefresh", false));
                            });
                            Bundle result;
                            try { result = reply.readBundle(GoogleAuthProxy.class.getClassLoader()); }
                            finally { reply.recycle(); }
                            callback.onResult(result);
                        } catch (Exception e) {
                            try { callback.onError(AccountManager.ERROR_CODE_REMOTE_EXCEPTION, "auth proxy request failed"); }
                            catch (RemoteException ignored) {}
                        }
                    });
                    return null;
                }
                case "invalidateAuthToken": {
                    if (!"com.google".equals(args[0])) return null;
                    Parcel reply = call(INVALIDATE, data -> data.writeString((String) args[1]));
                    try { event("client_invalidation cache_removed=" + reply.readBoolean()); }
                    finally { reply.recycle(); }
                    return null;
                }
                case "registerAccountListener":
                case "unregisterAccountListener": return null;
                default:
                    event("client_method=" + method + " unsupported=true");
                    if (args.length > 0 && args[0] instanceof IAccountManagerResponse) {
                        ((IAccountManagerResponse) args[0]).onError(AccountManager.ERROR_CODE_UNSUPPORTED_OPERATION,
                                "unsupported proxy account operation");
                        return null;
                    }
                    throw new SecurityException("unsupported proxy account operation: " + method);
            }
        } catch (SecurityException e) {
            event("client_method=" + method + " denied=true"); throw e;
        } catch (Exception e) {
            event("client_method=" + method + " failed=" + e.getClass().getSimpleName());
            throw new IllegalStateException("auth proxy unavailable");
        }
    }
}

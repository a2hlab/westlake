package adapter.core;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AuthenticatorDescription;
import android.accounts.IAccountAuthenticator;
import android.accounts.IAccountAuthenticatorResponse;
import android.accounts.IAccountManagerResponse;
import android.app.ActivityThread;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The account manager for the shared Play services process (ADR-0001): accounts, their user data and
 * auth tokens, kept in that process's data directory, and requests passed to the Google authenticator
 * as AccountManagerService passes them. Other processes keep the plain answers in ServiceAnswers.
 *
 * Like Android's accounts database, the store is not encrypted. Nothing here logs its values. All
 * Westlake apps share one file sandbox until appspawn-x has per-app mounts, so this must not hold an
 * account that matters before then.
 */
final class LocalAccounts {
    private static final String TAG = "WL_Accounts";
    private static final String TYPE = "com.google";
    private static final String AUTHENTICATOR =
            "com.google.android.gms.auth.account.authenticator.GoogleAccountAuthenticatorService";

    private static final Object LOCK = new Object();
    private static final ExecutorService REQUESTS = Executors.newCachedThreadPool();
    private static Properties sStore;

    private LocalAccounts() {}

    private static File file() {
        File dir = new File(ActivityThread.currentApplication().getDataDir(), "westlake-accounts");
        dir.mkdirs();
        dir.setReadable(false, false);
        dir.setReadable(true, true);
        return new File(dir, "accounts.properties");
    }

    private static Properties store() {
        if (sStore != null) return sStore;
        sStore = new Properties();
        File file = file();
        if (file.isFile()) {
            try (FileInputStream in = new FileInputStream(file)) {
                sStore.load(in);
            } catch (IOException e) {
                Log.e(TAG, "cannot read the account store: " + e.getClass().getName());
            }
        }
        return sStore;
    }

    private static void save() {
        try (FileOutputStream out = new FileOutputStream(file())) {
            store().store(out, null);
        } catch (IOException e) {
            Log.e(TAG, "cannot write the account store: " + e.getClass().getName());
        }
    }

    /** Keys are "<field>/<type>/<name>[/<sub>]", so a removal can find everything of an account. */
    private static String key(String field, Account account) {
        return field + "/" + account.type + "/" + account.name;
    }

    private static boolean exists(Account account) {
        return account != null && store().containsKey(key("account", account));
    }

    private static List<Account> accounts(String type) {
        TreeSet<String> names = new TreeSet<>(store().stringPropertyNames());
        List<Account> found = new ArrayList<>();
        for (String name : names) {
            if (!name.startsWith("account/")) continue;
            String[] parts = name.split("/", 3);
            if (type == null || type.equals(parts[1])) found.add(new Account(parts[2], parts[1]));
        }
        return found;
    }

    /**
     * What AccountManagerService sends when an account is added or removed: LOGIN_ACCOUNTS_CHANGED for
     * receivers of the old broadcast, and ACTION_VISIBLE_ACCOUNTS_CHANGED to the package that registered
     * a listener (registerAccountListener). Since Android 8, AccountManager's OnAccountsUpdateListeners
     * run on the second one only; without it Google's settings screen still offered "Sign in" after
     * its own add-account flow had added the account.
     */
    private static void changed() {
        Context app = ActivityThread.currentApplication();
        if (app != null) {
            app.sendBroadcast(new Intent(AccountManager.LOGIN_ACCOUNTS_CHANGED_ACTION).setPackage(app.getPackageName()));
            app.sendBroadcast(new Intent(AccountManager.ACTION_VISIBLE_ACCOUNTS_CHANGED).setPackage(app.getPackageName()));
        }
    }

    private static boolean add(Account account, String password, Bundle extras) {
        if (account == null) return false;
        synchronized (LOCK) {
            if (exists(account)) return false;
            store().setProperty(key("account", account), "1");
            if (password != null) store().setProperty(key("password", account), password);
            if (extras != null) {
                for (String name : extras.keySet()) {
                    String value = extras.getString(name);
                    if (value != null) store().setProperty(key("data", account) + "/" + name, value);
                }
            }
            save();
        }
        Log.i(TAG, "added an account of type " + account.type);
        changed();
        return true;
    }

    private static boolean remove(Account account) {
        boolean existed;
        synchronized (LOCK) {
            existed = exists(account);
            if (existed) {
                String suffix = "/" + account.type + "/" + account.name;
                for (String name : new ArrayList<>(store().stringPropertyNames())) {
                    if (name.endsWith(suffix) || name.contains(suffix + "/")) store().remove(name);
                }
                save();
            }
        }
        if (existed) changed();
        return existed;
    }

    private static void put(String key, String value) {
        synchronized (LOCK) {
            if (value == null) store().remove(key);
            else store().setProperty(key, value);
            save();
        }
    }

    /** One IAccountManager call. LocalServiceBinders.DEFAULT for those with nothing to answer. */
    static Object answer(String method, Object[] args) {
        switch (method) {
            case "getPassword":
                return exists((Account) args[0]) ? store().getProperty(key("password", (Account) args[0])) : null;
            case "getUserData":
                return store().getProperty(key("data", (Account) args[0]) + "/" + args[1]);
            case "getAuthenticatorTypes":
                return new AuthenticatorDescription[] {
                    new AuthenticatorDescription(TYPE, ActivityThread.currentPackageName(), 0, 0, 0, 0)};
            case "getAccountsForPackage":
                return accounts(null).toArray(new Account[0]);
            case "getAccountsByTypeForPackage":
            case "getAccountsAsUser":
                return accounts((String) args[0]).toArray(new Account[0]);
            case "addAccountExplicitly":
            case "addAccountExplicitlyWithVisibility":
                return add((Account) args[0], (String) args[1], (Bundle) args[2]);
            case "removeAccountExplicitly":
                return remove((Account) args[0]);
            case "removeAccountAsUser":
                reply((IAccountManagerResponse) args[0], booleanResult(remove((Account) args[1])));
                return null;
            case "invalidateAuthToken":
                invalidate((String) args[0], (String) args[1]);
                return null;
            case "peekAuthToken":
                return store().getProperty(key("token", (Account) args[0]) + "/" + args[1]);
            case "setAuthToken":
                put(key("token", (Account) args[0]) + "/" + args[1], (String) args[2]);
                return null;
            case "setPassword":
                put(key("password", (Account) args[0]), (String) args[1]);
                return null;
            case "clearPassword":
                put(key("password", (Account) args[0]), null);
                return null;
            case "setUserData":
                put(key("data", (Account) args[0]) + "/" + args[1], (String) args[2]);
                return null;
            case "accountAuthenticated":
            case "someUserHasAccount":
                return exists((Account) args[0]);
            case "setAccountVisibility":
            case "hasAccountAccess":
                return Boolean.TRUE;
            case "getAccountVisibility":
                return AccountManager.VISIBILITY_VISIBLE;
            case "getAccountsAndVisibilityForPackage": {
                Map<Account, Integer> visible = new HashMap<>();
                for (Account account : accounts((String) args[1])) visible.put(account, AccountManager.VISIBILITY_VISIBLE);
                return visible;
            }
            case "getPackagesAndVisibilityForAccount":
                return Collections.emptyMap();
            case "getAccountsByFeatures":
                accountsResult((IAccountManagerResponse) args[0], (String) args[1]);
                return null;
            case "getAccountByTypeAndFeatures":
                firstAccountResult((IAccountManagerResponse) args[0], (String) args[1]);
                return null;
            case "hasFeatures":
                throughAuthenticator((IAccountManagerResponse) args[0], authenticator -> response ->
                        authenticator.hasFeatures(response, (Account) args[1], (String[]) args[2]));
                return null;
            case "addAccount":
            case "addAccountAsUser":
                throughAuthenticator((IAccountManagerResponse) args[0], authenticator -> response ->
                        authenticator.addAccount(response, (String) args[1], (String) args[2], (String[]) args[3],
                                withCaller((Bundle) args[5])));
                return null;
            case "getAuthToken":
                getAuthToken((IAccountManagerResponse) args[0], (Account) args[1], (String) args[2], (Bundle) args[5]);
                return null;
            case "confirmCredentialsAsUser":
                throughAuthenticator((IAccountManagerResponse) args[0], authenticator -> response ->
                        authenticator.confirmCredentials(response, (Account) args[1], (Bundle) args[2]));
                return null;
            case "updateCredentials":
                throughAuthenticator((IAccountManagerResponse) args[0], authenticator -> response ->
                        authenticator.updateCredentials(response, (Account) args[1], (String) args[2], (Bundle) args[4]));
                return null;
            default:
                return LocalServiceBinders.DEFAULT;
        }
    }

    private static void invalidate(String type, String token) {
        synchronized (LOCK) {
            for (String name : new ArrayList<>(store().stringPropertyNames())) {
                if (name.startsWith("token/" + type + "/") && token != null && token.equals(store().getProperty(name))) {
                    store().remove(name);
                }
            }
            save();
        }
    }

    private static Bundle booleanResult(boolean value) {
        Bundle result = new Bundle();
        result.putBoolean(AccountManager.KEY_BOOLEAN_RESULT, value);
        return result;
    }

    private static void reply(IAccountManagerResponse response, Bundle result) {
        try {
            if (response != null) response.onResult(result);
        } catch (RemoteException | RuntimeException e) {
            Log.w(TAG, "account result not delivered: " + e.getClass().getName());
        }
    }

    private static void accountsResult(IAccountManagerResponse response, String type) {
        Bundle result = new Bundle();
        result.putParcelableArray(AccountManager.KEY_ACCOUNTS, accounts(type).toArray(new Account[0]));
        reply(response, result);
    }

    private static void firstAccountResult(IAccountManagerResponse response, String type) {
        Bundle result = new Bundle();
        List<Account> found = accounts(type);
        if (!found.isEmpty()) {
            result.putString(AccountManager.KEY_ACCOUNT_NAME, found.get(0).name);
            result.putString(AccountManager.KEY_ACCOUNT_TYPE, found.get(0).type);
        }
        reply(response, result);
    }

    /** What AccountManagerService adds: who asked, which the authenticator reads. */
    private static Bundle withCaller(Bundle options) {
        Bundle out = options == null ? new Bundle() : new Bundle(options);
        out.putInt(AccountManager.KEY_CALLER_UID, Binder.getCallingUid());
        out.putInt(AccountManager.KEY_CALLER_PID, Binder.getCallingPid());
        return out;
    }

    private static void getAuthToken(IAccountManagerResponse caller, Account account, String tokenType, Bundle options) {
        Bundle request = withCaller(options);
        final String who = options == null ? null : options.getString(AccountManager.KEY_ANDROID_PACKAGE_NAME);
        request.putString(AccountManager.KEY_ANDROID_PACKAGE_NAME, who);
        IAccountManagerResponse caching = new IAccountManagerResponse.Stub() {
            @Override
            public void onResult(Bundle value) throws RemoteException {
                String token = value == null ? null : value.getString(AccountManager.KEY_AUTHTOKEN);
                if (token != null) put(key("token", account) + "/" + tokenType, token);
                // The outcome only, never the token.
                System.err.println("[WL-ACCOUNTS] getAuthToken " + tokenType + " for " + who + " uid="
                        + request.getInt(AccountManager.KEY_CALLER_UID) + ": "
                        + (token != null ? "token" : value == null ? "no result" : "no token, keys " + value.keySet()));
                if (caller != null) caller.onResult(value);
            }

            @Override
            public void onError(int code, String message) throws RemoteException {
                System.err.println("[WL-ACCOUNTS] getAuthToken " + tokenType + " for " + who + ": error " + code);
                if (caller != null) caller.onError(code, message);
            }
        };
        throughAuthenticator(caching, authenticator -> response ->
                authenticator.getAuthToken(response, account, tokenType, request));
    }

    private interface Call {
        void on(IAccountAuthenticatorResponse response) throws RemoteException;
    }

    private interface Request {
        Call with(IAccountAuthenticator authenticator);
    }

    /** Binds the authenticator and runs one request on it, off the main thread as the system does. */
    private static void throughAuthenticator(IAccountManagerResponse caller, Request request) {
        Context app = ActivityThread.currentApplication();
        IAccountAuthenticatorResponse response = new IAccountAuthenticatorResponse.Stub() {
            @Override
            public void onResult(Bundle value) throws RemoteException {
                if (caller != null) caller.onResult(value);
            }

            @Override
            public void onRequestContinued() {
            }

            @Override
            public void onError(int code, String message) throws RemoteException {
                Log.w(TAG, "authenticator error " + code + ": " + message);
                if (caller != null) caller.onError(code, message);
            }
        };
        Intent intent = new Intent(AccountManager.ACTION_AUTHENTICATOR_INTENT)
                .setComponent(new ComponentName(app.getPackageName(), AUTHENTICATOR));
        boolean bound = app.bindService(intent, new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder binder) {
                REQUESTS.execute(() -> {
                    try {
                        Log.i(TAG, "authenticator bound: " + binder);
                        request.with(IAccountAuthenticator.Stub.asInterface(binder)).on(response);
                    } catch (RemoteException | RuntimeException e) {
                        Log.w(TAG, "authenticator request failed: " + e.getClass().getName());
                        fail(caller);
                    }
                });
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
            }
        }, Context.BIND_AUTO_CREATE);
        if (!bound) fail(caller);
    }

    private static void fail(IAccountManagerResponse caller) {
        try {
            if (caller != null) caller.onError(AccountManager.ERROR_CODE_REMOTE_EXCEPTION, "authenticator unavailable");
        } catch (RemoteException | RuntimeException e) {
            Log.w(TAG, "account error not delivered: " + e.getClass().getName());
        }
    }
}

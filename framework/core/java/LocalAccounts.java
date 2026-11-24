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
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The account manager for the shared Play services process (ADR-0001): accounts, their user data and
 * auth tokens, kept in that process's data directory, and requests passed to the Google authenticator
 * as AccountManagerService passes them. Play services' other processes, the Play Store and every app whose
 * spawner vouched for it on its channel reach it through PlayServicesChannel; each call is answered for the
 * calling uid, with Android 15's account visibility (resolveVisibility): an app sees the accounts Play
 * services made visible to it, and Play services and the apps signed like it see them all.
 *
 * Like Android's accounts database, the store is not encrypted. Nothing here logs its values, nor an
 * account's name. All Westlake apps share one file sandbox until appspawn-x has per-app mounts, so this
 * must not hold an account that matters before then.
 */
final class LocalAccounts {
    private static final String TAG = "WL_Accounts";
    private static final String TYPE = "com.google";
    private static final String AUTHENTICATOR =
            "com.google.android.gms.auth.account.authenticator.GoogleAccountAuthenticatorService";
    /** microG's authenticator, when this process is the auth provider (GoogleAuthProxy.provider()). */
    private static final String MICROG_AUTHENTICATOR = "org.microg.gms.auth.loginservice.GoogleLoginService";

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
        synchronized (LOCK) {
            if (sStore != null) return sStore;
            Properties loaded = new Properties();
            File file = file();
            if (file.isFile()) {
                try (FileInputStream in = new FileInputStream(file)) {
                    loaded.load(in);
                } catch (IOException e) {
                    // The auth provider's store holds the account credential: an empty store saved over it would
                    // lose it, so the provider fails closed.
                    if (GoogleAuthProxy.provider()) throw new IllegalStateException("account store unreadable", e);
                    Log.e(TAG, "cannot read the account store: " + e.getClass().getName());
                }
            }
            sStore = loaded;
            return sStore;
        }
    }

    private static void save() {
        if (GoogleAuthProxy.provider()) {
            try { PrivateFiles.writePrivate(file().toPath(), store()); }
            catch (IOException e) { throw new IllegalStateException("private account write failed", e); }
            return;
        }
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

    /**
     * addAccountInternal: the account, its password and user data, then the visibility the authenticator gave
     * it (addAccountExplicitlyWithVisibility), stored without notifying; then the listeners that see it.
     */
    private static boolean add(Account account, String password, Bundle extras, Map<?, ?> visibility) {
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
        if (visibility != null) {
            for (Map.Entry<?, ?> entry : visibility.entrySet()) {
                if (entry.getKey() instanceof String && entry.getValue() instanceof Integer) {
                    setVisibility(account, (String) entry.getKey(), (Integer) entry.getValue(), false);
                }
            }
        }
        Log.i(TAG, "added an account of type " + account.type);
        changed();
        for (String listener : listenersSeeing(account)) notifyPackage(listener);
        return true;
    }

    /** removeAccountInternal: everything of the account, its visibility included; then the listeners that saw it. */
    private static boolean remove(Account account) {
        Set<String> saw = exists(account) ? listenersSeeing(account) : Collections.emptySet();
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
        if (existed) {
            changed();
            for (String listener : saw) notifyPackage(listener);
        }
        return existed;
    }

    // ---- Who calls, and which accounts it sees (AccountManagerService, Android 15) ----

    /** checkPackageSignature's answers: another app, an app signed like the authenticator, the authenticator's uid. */
    private static final int SIGNATURE_MISMATCH = 0;
    private static final int SIGNATURE_MATCH = 1;
    private static final int SIGNATURE_UID_MATCH = 2;

    /**
     * The package a call names, checked as AppOpsManager.checkPackage checks it: one of the calling uid's packages
     * (none named: the uid's first, as filterAccounts takes it). A call this process makes names a package of
     * its own uid, which is the authenticator's.
     */
    private static String callingPackage(String opPackageName) {
        int uid = AccountManagerStub.callerUid();
        Context app = ActivityThread.currentApplication();
        if (uid == android.os.Process.myUid()) return opPackageName != null ? opPackageName : app.getPackageName();
        String[] owned = app.getPackageManager().getPackagesForUid(uid);
        if (owned != null && owned.length > 0) {
            if (opPackageName == null) return owned[0];
            for (String name : owned) {
                if (name.equals(opPackageName)) return name;
            }
        }
        throw new SecurityException("Specified package " + opPackageName + " under uid " + uid + " but it is not");
    }

    /** The uid of an installed package, -1 for one this process cannot find (packageExistsForUser). */
    private static int packageUid(String packageName) {
        if (packageName == null) return -1;
        Context app = ActivityThread.currentApplication();
        if (packageName.equals(app.getPackageName())) return android.os.Process.myUid();
        try {
            return app.getPackageManager().getPackageUid(packageName, 0);
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return -1;
        }
    }

    private static boolean isSpecialKey(String packageName) {
        return AccountManager.PACKAGE_NAME_KEY_LEGACY_VISIBLE.equals(packageName)
                || AccountManager.PACKAGE_NAME_KEY_LEGACY_NOT_VISIBLE.equals(packageName);
    }

    private static boolean visible(int visibility) {
        return visibility == AccountManager.VISIBILITY_VISIBLE
                || visibility == AccountManager.VISIBILITY_USER_MANAGED_VISIBLE;
    }

    /** Stored as "visibility/<type>/<name>/<package>", so that removing the account removes it. */
    private static String visibilityKey(Account account, String packageName) {
        return key("visibility", account) + "/" + packageName;
    }

    private static int storedVisibility(Account account, String packageName) {
        String value = store().getProperty(visibilityKey(account, packageName));
        if (value == null) return AccountManager.VISIBILITY_UNDEFINED;
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            return AccountManager.VISIBILITY_UNDEFINED;
        }
    }

    /**
     * resolveAccountVisibility, for an account of the type this process's authenticator manages: the system and
     * the authenticator's own uid always see it; then what was stored for the package; then an app signed like
     * the authenticator, or one the permissions below let in, gets the stored legacy-visible default or
     * USER_MANAGED_VISIBLE, and every other app the legacy-not-visible default or USER_MANAGED_NOT_VISIBLE.
     * Westlake has no device or profile owner.
     */
    private static int resolveVisibility(Account account, String packageName) {
        int uid = packageUid(packageName);
        if (uid < 0) return AccountManager.VISIBILITY_NOT_VISIBLE;
        if (android.os.UserHandle.getAppId(uid) == android.os.Process.SYSTEM_UID) return AccountManager.VISIBILITY_VISIBLE;
        int signature = uid == android.os.Process.myUid() ? SIGNATURE_UID_MATCH
                : sameCertificate(packageName) ? SIGNATURE_MATCH : SIGNATURE_MISMATCH;
        if (signature == SIGNATURE_UID_MATCH) return AccountManager.VISIBILITY_VISIBLE;
        int stored = storedVisibility(account, packageName);
        if (stored != AccountManager.VISIBILITY_UNDEFINED) return stored;
        boolean legacyVisible = signature != SIGNATURE_MISMATCH
                || (targetsBeforeO(packageName) && granted(packageName, android.Manifest.permission.GET_ACCOUNTS))
                || (managesContacts() && granted(packageName, android.Manifest.permission.READ_CONTACTS))
                || granted(packageName, android.Manifest.permission.GET_ACCOUNTS_PRIVILEGED);
        int legacy = storedVisibility(account, legacyVisible ? AccountManager.PACKAGE_NAME_KEY_LEGACY_VISIBLE
                : AccountManager.PACKAGE_NAME_KEY_LEGACY_NOT_VISIBLE);
        if (legacy != AccountManager.VISIBILITY_UNDEFINED) return legacy;
        return legacyVisible ? AccountManager.VISIBILITY_USER_MANAGED_VISIBLE
                : AccountManager.VISIBILITY_USER_MANAGED_NOT_VISIBLE;
    }

    private static boolean sameCertificate(String packageName) {
        Set<Signature> own = ownSigners();
        return !own.isEmpty() && own.equals(signers(ActivityThread.currentApplication().getPackageManager(), packageName));
    }

    private static volatile Set<Signature> sOwnSigners;

    /** The authenticator's certificates, this process's package's: read once, as they cannot change while it runs. */
    private static Set<Signature> ownSigners() {
        Set<Signature> own = sOwnSigners;
        if (own == null || own.isEmpty()) {
            Context app = ActivityThread.currentApplication();
            own = signers(app.getPackageManager(), app.getPackageName());
            sOwnSigners = own;
        }
        return own;
    }

    private static boolean targetsBeforeO(String packageName) {
        try {
            return ActivityThread.currentApplication().getPackageManager().getApplicationInfo(packageName, 0)
                    .targetSdkVersion < android.os.Build.VERSION_CODES.O;
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return false;
        }
    }

    private static final Set<String> UNREADABLE_GRANTS =
            Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());

    /**
     * Whether another app holds a permission the visibility rules read. GET_ACCOUNTS_PRIVILEGED is a signature or
     * privileged permission of the platform, which no Westlake app has. GET_ACCOUNTS and READ_CONTACTS are runtime
     * permissions, granted to an app on OH, which this process cannot read: they count as not granted, so an app
     * sees an account through a stored visibility only, as an app on Android that was granted neither. Logged
     * once for an app that asks for one, so a round shows which apps this leaves out.
     */
    private static boolean granted(String packageName, String permission) {
        if (!android.Manifest.permission.GET_ACCOUNTS_PRIVILEGED.equals(permission)
                && requests(packageName, permission) && UNREADABLE_GRANTS.add(packageName + " " + permission)) {
            System.err.println("[WL-ACCOUNTS] visibility for " + packageName + ": it requests " + permission
                    + ", whose grant this process cannot read; counted as not granted");
        }
        return false;
    }

    private static boolean requests(String packageName, String permission) {
        try {
            PackageInfo info = ActivityThread.currentApplication().getPackageManager()
                    .getPackageInfo(packageName, PackageManager.GET_PERMISSIONS);
            return info.requestedPermissions != null && Arrays.asList(info.requestedPermissions).contains(permission);
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return false;
        }
    }

    /** accountTypeManagesContacts: the authenticator's package holds WRITE_CONTACTS. */
    private static boolean managesContacts() {
        return ActivityThread.currentApplication().checkSelfPermission(android.Manifest.permission.WRITE_CONTACTS)
                == PackageManager.PERMISSION_GRANTED;
    }

    private static final Map<String, String> SHOWN = new java.util.concurrent.ConcurrentHashMap<>();

    /**
     * filterAccounts: the accounts of a type (null: every type) a package sees, and with includeManagedNotVisible
     * those the user may still show it. Each package's count is logged when it changes, never an account's name.
     */
    private static Account[] visibleTo(String packageName, String type, boolean includeManagedNotVisible) {
        List<Account> all = accounts(type);
        List<Account> shown = new ArrayList<>();
        for (Account account : all) {
            int visibility = resolveVisibility(account, packageName);
            if (visible(visibility) || (includeManagedNotVisible
                    && visibility == AccountManager.VISIBILITY_USER_MANAGED_NOT_VISIBLE)) {
                shown.add(account);
            }
        }
        String counts = shown.size() + " of " + all.size() + (includeManagedNotVisible ? " (with user-managed)" : "");
        if (!counts.equals(SHOWN.put(packageName + "/" + type + "/" + includeManagedNotVisible, counts))) {
            System.err.println("[WL-ACCOUNTS] accounts of " + (type != null ? type : "every type") + " for "
                    + packageName + ": " + counts);
        }
        return shown.toArray(new Account[0]);
    }

    /**
     * getAccountsByTypeForPackage: the authenticator of a type asks which of its accounts a package sees or may be
     * shown (its account picker); with no type, an app asks for its own; any other answer is none.
     */
    private static Account[] accountsByTypeForPackage(String type, String packageName, String opPackageName) {
        String caller = callingPackage(opPackageName);
        if (packageUid(packageName) < 0) return new Account[0];
        if (type == null) return visibleTo(caller, null, false);
        if (!managedByCaller()) return new Account[0];
        return visibleTo(packageName, type, true);
    }

    /** getAccountVisibility: a package's visibility as resolved, or the stored legacy defaults themselves. */
    private static int visibility(Account account, String packageName) {
        if (AccountManager.PACKAGE_NAME_KEY_LEGACY_VISIBLE.equals(packageName)) {
            int stored = storedVisibility(account, packageName);
            return stored != AccountManager.VISIBILITY_UNDEFINED ? stored : AccountManager.VISIBILITY_USER_MANAGED_VISIBLE;
        }
        if (AccountManager.PACKAGE_NAME_KEY_LEGACY_NOT_VISIBLE.equals(packageName)) {
            int stored = storedVisibility(account, packageName);
            return stored != AccountManager.VISIBILITY_UNDEFINED ? stored : AccountManager.VISIBILITY_USER_MANAGED_NOT_VISIBLE;
        }
        return resolveVisibility(account, packageName);
    }

    /**
     * setAccountVisibility: stored for an installed package or a legacy default, for an account that exists. With
     * notify, a package whose answer changes between seen and not seen is told (ACTION_VISIBLE_ACCOUNTS_CHANGED):
     * a legacy default tells every app with a listener. Only apps with a listener are told here; Android also
     * wakes an app that declares a receiver, which this process cannot see. LOGIN_ACCOUNTS_CHANGED goes to this
     * app only when the stored value changed: Android sends it for every call, and Play services sets the same
     * visibility again and again as it starts.
     */
    private static boolean setVisibility(Account account, String packageName, int newVisibility, boolean notify) {
        if (account == null || packageName == null) return false;
        boolean special = isSpecialKey(packageName);
        if (!special && packageUid(packageName) < 0) {
            System.err.println("[WL-ACCOUNTS] visibility for " + packageName + " not stored: no such package here");
            return false;
        }
        Set<String> told = new TreeSet<>();
        if (notify) {
            Set<String> listeners = listeners(account.type);
            if (special) told.addAll(listeners);
            else if (listeners.contains(packageName)) told.add(packageName);
        }
        Map<String, Boolean> before = new HashMap<>();
        for (String target : told) before.put(target, visible(resolveVisibility(account, target)));
        boolean changedValue;
        synchronized (LOCK) {
            if (!exists(account)) return false;
            changedValue = storedVisibility(account, packageName) != newVisibility;
            if (changedValue) {
                store().setProperty(visibilityKey(account, packageName), Integer.toString(newVisibility));
                save();
            }
        }
        if (!changedValue) return true;
        System.err.println("[WL-ACCOUNTS] visibility of an account of type " + account.type + " for " + packageName
                + ": " + newVisibility);
        if (notify) {
            for (String target : told) {
                if (before.get(target) != visible(resolveVisibility(account, target))) notifyPackage(target);
            }
            Context app = ActivityThread.currentApplication();
            if (app != null) {
                app.sendBroadcast(new Intent(AccountManager.LOGIN_ACCOUNTS_CHANGED_ACTION).setPackage(app.getPackageName()));
            }
        }
        return true;
    }

    /** getAccountsAndVisibilityForPackage: for the authenticator of a type, each of its accounts and how a package sees it. */
    private static Map<Account, Integer> accountsAndVisibility(String packageName, String type) {
        if (type == null || !managedByCaller()) {
            throw new SecurityException("getAccountsAndVisibilityForPackage() called from unauthorized uid "
                    + AccountManagerStub.callerUid() + " with packageName=" + packageName);
        }
        Map<Account, Integer> result = new java.util.LinkedHashMap<>();
        if (packageUid(packageName) < 0) return result;
        for (Account account : accounts(type)) result.put(account, resolveVisibility(account, packageName));
        return result;
    }

    /** getPackagesAndVisibilityForAccount: what was stored for the account, legacy defaults included. */
    private static Map<String, Integer> packagesAndVisibility(Account account) {
        Map<String, Integer> result = new HashMap<>();
        if (account == null) return result;
        String prefix = key("visibility", account) + "/";
        for (String name : store().stringPropertyNames()) {
            if (!name.startsWith(prefix)) continue;
            int stored = storedVisibility(account, name.substring(prefix.length()));
            result.put(name.substring(prefix.length()), stored);
        }
        return result;
    }

    /** registerAccountListener: per account type (null for every type), the packages that listen, and how often. */
    private static final Map<String, Map<String, Integer>> LISTENERS = new HashMap<>();

    /**
     * Counts a listener in or out. Android refuses to count out one it never counted in; here that answers
     * nothing instead, because a Play services process that started again since has an empty count.
     */
    private static void listen(String[] types, String packageName, boolean register) {
        synchronized (LISTENERS) {
            for (String type : types != null ? types : new String[] {null}) {
                Map<String, Integer> packages = LISTENERS.computeIfAbsent(type, t -> new HashMap<>());
                Integer count = packages.get(packageName);
                if (register) packages.put(packageName, count == null ? 1 : count + 1);
                else if (count != null && count > 1) packages.put(packageName, count - 1);
                else packages.remove(packageName);
            }
        }
    }

    /** getRequestingPackages: the packages listening for accounts of a type, this app's own left out (changed() tells it). */
    private static Set<String> listeners(String type) {
        Set<String> found = new TreeSet<>();
        synchronized (LISTENERS) {
            for (String key : new String[] {type, null}) {
                Map<String, Integer> packages = LISTENERS.get(key);
                if (packages != null) found.addAll(packages.keySet());
            }
        }
        Context app = ActivityThread.currentApplication();
        if (app != null) found.remove(app.getPackageName());
        return found;
    }

    private static Set<String> listenersSeeing(Account account) {
        Set<String> found = new TreeSet<>();
        for (String packageName : listeners(account.type)) {
            if (visible(resolveVisibility(account, packageName))) found.add(packageName);
        }
        return found;
    }

    /**
     * notifyPackage: ACTION_VISIBLE_ACCOUNTS_CHANGED to one app, over its channel while it is connected
     * (PlayServicesChannel.deliverToLiveClient). An app that has gone is not started for it: its listener went
     * with its process, and Android wakes an app for this broadcast only if it declares a receiver for it.
     */
    private static void notifyPackage(String packageName) {
        Intent intent = new Intent(AccountManager.ACTION_VISIBLE_ACCOUNTS_CHANGED).setPackage(packageName)
                .addFlags(Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND);
        boolean delivered;
        try {
            delivered = (Boolean) Class.forName("adapter.activity.PlayServicesChannel")
                    .getMethod("deliverToLiveClient", Intent.class).invoke(null, intent);
        } catch (ReflectiveOperationException | RuntimeException e) {
            delivered = false;
        }
        System.err.println("[WL-ACCOUNTS] visible accounts changed for " + packageName + ": "
                + (delivered ? "told it" : "not connected, not told"));
    }

    private static void put(String key, String value) {
        synchronized (LOCK) {
            if (value == null) store().remove(key);
            else store().setProperty(key, value);
            save();
        }
    }

    /**
     * The calls AccountManagerService allows only to the account type's authenticator, or to an app
     * signed with the same certificate (isAccountManagedByCaller): the account's password, its user
     * data and its cached tokens hold its credentials. The Play Store, signed like Play services,
     * passes; any other app gets the SecurityException it gets on Android.
     */
    private static final Set<String> MANAGER_ONLY = new java.util.HashSet<>(Arrays.asList(
            "getPassword", "setPassword", "clearPassword", "getUserData", "setUserData", "peekAuthToken",
            "setAuthToken", "addAccountExplicitly", "addAccountExplicitlyWithVisibility",
            "removeAccountExplicitly", "renameAccount",
            // These too, once every app reaches this service: removing an account, telling it the user just
            // authenticated, and reading or setting which packages see it (removeAccountAsUser also lets the
            // system and a profile owner, which Westlake has neither of).
            "removeAccountAsUser", "accountAuthenticated", "getAccountVisibility", "setAccountVisibility",
            "getPackagesAndVisibilityForAccount"));

    /** The calls AccountManagerService allows to the system uid only, which no Westlake app has. */
    private static final Set<String> SYSTEM_ONLY = new java.util.HashSet<>(Arrays.asList(
            "getAccountsForPackage", "hasAccountAccess", "someUserHasAccount"));

    /** One IAccountManager call. LocalServiceBinders.DEFAULT for those with nothing to answer. */
    static Object answer(String method, Object[] args) {
        if (GoogleAuthProxy.client()) return GoogleAuthProxy.answer(method, args);
        if (MANAGER_ONLY.contains(method) && !managedByCaller()) {
            throw new SecurityException("uid " + AccountManagerStub.callerUid() + " cannot use " + method
                    + " on accounts of type " + TYPE);
        }
        if (SYSTEM_ONLY.contains(method)) {
            throw new SecurityException(method + " called from unauthorized uid " + AccountManagerStub.callerUid());
        }
        switch (method) {
            case "getPassword":
                return exists((Account) args[0]) ? store().getProperty(key("password", (Account) args[0])) : null;
            case "getUserData":
                return store().getProperty(key("data", (Account) args[0]) + "/" + args[1]);
            case "getAuthenticatorTypes":
                return new AuthenticatorDescription[] {
                    new AuthenticatorDescription(TYPE, ActivityThread.currentPackageName(), 0, 0, 0, 0,
                            GoogleAuthProxy.provider())};
            case "getAccountsByTypeForPackage":
                return accountsByTypeForPackage((String) args[0], (String) args[1], (String) args[2]);
            case "getAccountsAsUser":
                return visibleTo(callingPackage((String) args[2]), (String) args[0], false);
            case "addAccountExplicitly":
                return add((Account) args[0], (String) args[1], (Bundle) args[2], null);
            case "addAccountExplicitlyWithVisibility":
                return add((Account) args[0], (String) args[1], (Bundle) args[2], (Map<?, ?>) args[3]);
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
                return exists((Account) args[0]);
            case "setAccountVisibility":
                return setVisibility((Account) args[0], (String) args[1], (Integer) args[2], true);
            case "getAccountVisibility":
                return visibility((Account) args[0], (String) args[1]);
            case "getAccountsAndVisibilityForPackage":
                return accountsAndVisibility((String) args[0], (String) args[1]);
            case "getPackagesAndVisibilityForAccount":
                return packagesAndVisibility((Account) args[0]);
            case "registerAccountListener":
                listen((String[]) args[0], callingPackage((String) args[1]), true);
                return null;
            case "unregisterAccountListener":
                listen((String[]) args[0], callingPackage((String) args[1]), false);
                return null;
            case "getAccountsByFeatures":
                // Features are not asked of the authenticator yet (GetAccountsByTypeAndFeatureSession): every
                // account the caller sees is answered.
                accountsResult((IAccountManagerResponse) args[0], visibleTo(callingPackage((String) args[3]), (String) args[1], false));
                return null;
            case "getAccountByTypeAndFeatures":
                firstAccountResult((IAccountManagerResponse) args[0], callingPackage((String) args[3]), (String) args[1]);
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

    private static void accountsResult(IAccountManagerResponse response, Account[] accounts) {
        Bundle result = new Bundle();
        result.putParcelableArray(AccountManager.KEY_ACCOUNTS, accounts);
        reply(response, result);
    }

    /**
     * getAccountByTypeAndFeatures (handleGetAccountsResult): the one account the caller sees or may be shown. Where
     * Android would start its account chooser (several accounts, or one the user has not yet shown to this app),
     * Westlake has none to start, so the answer is the one for no account.
     */
    private static void firstAccountResult(IAccountManagerResponse response, String caller, String type) {
        Bundle result = new Bundle();
        Account[] found = visibleTo(caller, type, true);
        boolean chooser = found.length > 1 || (found.length == 1
                && resolveVisibility(found[0], caller) == AccountManager.VISIBILITY_USER_MANAGED_NOT_VISIBLE);
        if (chooser) {
            System.err.println("[WL-ACCOUNTS] getAccountByTypeAndFeatures for " + caller + ": Android would show its"
                    + " account chooser (" + found.length + " accounts); none here, answered no account");
        } else if (found.length == 1) {
            result.putString(AccountManager.KEY_ACCOUNT_NAME, found[0].name);
            result.putString(AccountManager.KEY_ACCOUNT_TYPE, found[0].type);
        }
        reply(response, result);
    }

    /** What AccountManagerService adds: who asked, which the authenticator reads. */
    private static Bundle withCaller(Bundle options) {
        Bundle out = options == null ? new Bundle() : new Bundle(options);
        out.putInt(AccountManager.KEY_CALLER_UID, AccountManagerStub.callerUid());
        out.putInt(AccountManager.KEY_CALLER_PID, AccountManagerStub.callerPid());
        return out;
    }

    /** isAccountManagedByCaller: this process (the authenticator's), or a caller signed by its certificate. */
    private static boolean managedByCaller() {
        int uid = AccountManagerStub.callerUid();
        if (uid == android.os.Process.myUid()) return true;
        Context app = ActivityThread.currentApplication();
        if (app == null) return false;
        PackageManager manager = app.getPackageManager();
        String[] callers = manager.getPackagesForUid(uid);
        Set<Signature> own = ownSigners();
        if (callers == null || own.isEmpty()) return false;
        for (String caller : callers) {
            if (own.equals(signers(manager, caller))) return true;
        }
        return false;
    }

    private static Set<Signature> signers(PackageManager manager, String packageName) {
        try {
            PackageInfo info = manager.getPackageInfo(packageName, PackageManager.GET_SIGNING_CERTIFICATES);
            if (info.signingInfo == null) return Collections.emptySet();
            return new java.util.HashSet<>(Arrays.asList(info.signingInfo.getApkContentsSigners()));
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            return Collections.emptySet();
        }
    }

    private static void getAuthToken(IAccountManagerResponse caller, Account account, String tokenType, Bundle options) {
        final String who = options == null ? null : options.getString(AccountManager.KEY_ANDROID_PACKAGE_NAME);
        // AccountManagerService: the package a request names must be one of the calling uid's ("attempting to
        // illegally masquerade"), since the authenticator answers for that package. Calls this process makes
        // itself are its own.
        if (AccountManagerStub.callerUid() != android.os.Process.myUid()) {
            if (who == null) {
                throw new SecurityException("Uid " + AccountManagerStub.callerUid()
                        + " is attempting to illegally masquerade as package null!");
            }
            callingPackage(who);
        }
        Bundle request = withCaller(options);
        request.putString(AccountManager.KEY_ANDROID_PACKAGE_NAME, who);
        IAccountManagerResponse outcome = new IAccountManagerResponse.Stub() {
            @Override
            public void onResult(Bundle value) throws RemoteException {
                String token = value == null ? null : value.getString(AccountManager.KEY_AUTHTOKEN);
                // Not kept with the account: Google's authenticator declares customTokens (as microG's does), and
                // AccountManagerService keeps such tokens out of the account's token table, only per caller
                // package and signature, so a token issued to one app never answers another's peekAuthToken.
                // The outcome only, never the token.
                System.err.println("[WL-ACCOUNTS] getAuthToken " + tokenType + " for " + who + " uid="
                        + request.getInt(AccountManager.KEY_CALLER_UID) + ": "
                        + (token != null ? "token" : value == null ? "no result"
                                : "no token, keys " + new TreeSet<>(value.keySet())));   // the names, never the values
                if (caller != null) caller.onResult(value);
            }

            @Override
            public void onError(int code, String message) throws RemoteException {
                System.err.println("[WL-ACCOUNTS] getAuthToken " + tokenType + " for " + who + ": error " + code);
                if (caller != null) caller.onError(code, message);
            }
        };
        throughAuthenticator(outcome, authenticator -> response ->
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
                Log.w(TAG, "authenticator error " + code);
                if (caller != null) caller.onError(code, message);
            }
        };
        Intent intent = new Intent(AccountManager.ACTION_AUTHENTICATOR_INTENT)
                .setComponent(new ComponentName(app.getPackageName(), (GoogleAuthProxy.provider() ? MICROG_AUTHENTICATOR : AUTHENTICATOR)));
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

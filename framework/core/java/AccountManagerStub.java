package adapter.core;

import android.accounts.Account;
import android.accounts.AuthenticatorDescription;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.content.IntentSender;
import android.os.Bundle;
import android.os.UserHandle;
import java.util.Map;

/**
 * The account service as a real IAccountManager.Stub, answered by LocalAccounts: generated from AOSP's
 * IAccountManager.aidl by tools/gen_account_manager_stub.py, do not edit by hand.
 *
 * A Stub serves callers in other processes as well as this one. The Play Store host's account
 * manager is this service in the Play services host, reached through SocketBinder, where
 * Binder.getCallingUid is the Play Store's own uid; the in-process proxy it replaces answered only
 * calls made in this process.
 */
final class AccountManagerStub extends IAccountManager.Stub {
    private static final java.util.Set<String> SEEN =
            java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());

    /** The uid and pid of the process whose transaction this thread is serving, or null in a direct call. */
    private static final ThreadLocal<int[]> REMOTE = new ThreadLocal<>();

    /**
     * Who is calling, as AccountManagerService sees it: the other process during a transaction that came
     * through this Stub, and this process for a direct call. A direct call made on a thread that is
     * serving some app's request (Play services' code on a SocketBinder thread) is still this process's
     * own call, as it would be a call from Play services into system_server on Android, whatever
     * Binder.getCallingUid says on that thread.
     */
    static int callerUid() {
        int[] remote = REMOTE.get();
        return remote != null ? remote[0] : android.os.Process.myUid();
    }

    static int callerPid() {
        int[] remote = REMOTE.get();
        return remote != null ? remote[1] : android.os.Process.myPid();
    }

    @Override
    public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags)
            throws android.os.RemoteException {
        int[] outer = REMOTE.get();
        REMOTE.set(new int[] {android.os.Binder.getCallingUid(), android.os.Binder.getCallingPid()});
        try {
            return super.onTransact(code, data, reply, flags);
        } finally {
            REMOTE.set(outer);
        }
    }

    /** One call: logged the first time, as LocalServiceBinders' proxies are; DEFAULT answers as they do. */
    private static Object call(String method, Class<?> returns, Object[] args) {
        if (SEEN.add(method)) System.err.println("[WESTLAKE-LOCAL-SERVICE] account." + method);
        Object value = LocalAccounts.answer(method, args);
        return value == LocalServiceBinders.DEFAULT ? LocalServiceBinders.defaultValue(returns) : value;
    }

    @Override
    public String getPassword(Account account) {
        return (String) call("getPassword", String.class, new Object[] {account});
    }

    @Override
    public String getUserData(Account account, String key) {
        return (String) call("getUserData", String.class, new Object[] {account, key});
    }

    @Override
    public AuthenticatorDescription[] getAuthenticatorTypes(int userId) {
        return (AuthenticatorDescription[]) call("getAuthenticatorTypes", AuthenticatorDescription[].class, new Object[] {userId});
    }

    @Override
    public Account[] getAccountsForPackage(String packageName, int uid, String opPackageName) {
        return (Account[]) call("getAccountsForPackage", Account[].class, new Object[] {packageName, uid, opPackageName});
    }

    @Override
    public Account[] getAccountsByTypeForPackage(String type, String packageName, String opPackageName) {
        return (Account[]) call("getAccountsByTypeForPackage", Account[].class, new Object[] {type, packageName, opPackageName});
    }

    @Override
    public Account[] getAccountsAsUser(String accountType, int userId, String opPackageName) {
        return (Account[]) call("getAccountsAsUser", Account[].class, new Object[] {accountType, userId, opPackageName});
    }

    @Override
    public void hasFeatures(IAccountManagerResponse response, Account account, String[] features, int userId, String opPackageName) {
        call("hasFeatures", void.class, new Object[] {response, account, features, userId, opPackageName});
    }

    @Override
    public void getAccountByTypeAndFeatures(IAccountManagerResponse response, String accountType, String[] features, String opPackageName) {
        call("getAccountByTypeAndFeatures", void.class, new Object[] {response, accountType, features, opPackageName});
    }

    @Override
    public void getAccountsByFeatures(IAccountManagerResponse response, String accountType, String[] features, String opPackageName) {
        call("getAccountsByFeatures", void.class, new Object[] {response, accountType, features, opPackageName});
    }

    @Override
    public boolean addAccountExplicitly(Account account, String password, Bundle extras, String opPackageName) {
        return (Boolean) call("addAccountExplicitly", boolean.class, new Object[] {account, password, extras, opPackageName});
    }

    @Override
    public void removeAccountAsUser(IAccountManagerResponse response, Account account, boolean expectActivityLaunch, int userId) {
        call("removeAccountAsUser", void.class, new Object[] {response, account, expectActivityLaunch, userId});
    }

    @Override
    public boolean removeAccountExplicitly(Account account) {
        return (Boolean) call("removeAccountExplicitly", boolean.class, new Object[] {account});
    }

    @Override
    public void copyAccountToUser(IAccountManagerResponse response, Account account, int userFrom, int userTo) {
        call("copyAccountToUser", void.class, new Object[] {response, account, userFrom, userTo});
    }

    @Override
    public void invalidateAuthToken(String accountType, String authToken) {
        call("invalidateAuthToken", void.class, new Object[] {accountType, authToken});
    }

    @Override
    public String peekAuthToken(Account account, String authTokenType) {
        return (String) call("peekAuthToken", String.class, new Object[] {account, authTokenType});
    }

    @Override
    public void setAuthToken(Account account, String authTokenType, String authToken) {
        call("setAuthToken", void.class, new Object[] {account, authTokenType, authToken});
    }

    @Override
    public void setPassword(Account account, String password) {
        call("setPassword", void.class, new Object[] {account, password});
    }

    @Override
    public void clearPassword(Account account) {
        call("clearPassword", void.class, new Object[] {account});
    }

    @Override
    public void setUserData(Account account, String key, String value) {
        call("setUserData", void.class, new Object[] {account, key, value});
    }

    @Override
    public void updateAppPermission(Account account, String authTokenType, int uid, boolean value) {
        call("updateAppPermission", void.class, new Object[] {account, authTokenType, uid, value});
    }

    @Override
    public void getAuthToken(IAccountManagerResponse response, Account account, String authTokenType, boolean notifyOnAuthFailure, boolean expectActivityLaunch, Bundle options) {
        call("getAuthToken", void.class, new Object[] {response, account, authTokenType, notifyOnAuthFailure, expectActivityLaunch, options});
    }

    @Override
    public void addAccount(IAccountManagerResponse response, String accountType, String authTokenType, String[] requiredFeatures, boolean expectActivityLaunch, Bundle options) {
        call("addAccount", void.class, new Object[] {response, accountType, authTokenType, requiredFeatures, expectActivityLaunch, options});
    }

    @Override
    public void addAccountAsUser(IAccountManagerResponse response, String accountType, String authTokenType, String[] requiredFeatures, boolean expectActivityLaunch, Bundle options, int userId) {
        call("addAccountAsUser", void.class, new Object[] {response, accountType, authTokenType, requiredFeatures, expectActivityLaunch, options, userId});
    }

    @Override
    public void updateCredentials(IAccountManagerResponse response, Account account, String authTokenType, boolean expectActivityLaunch, Bundle options) {
        call("updateCredentials", void.class, new Object[] {response, account, authTokenType, expectActivityLaunch, options});
    }

    @Override
    public void editProperties(IAccountManagerResponse response, String accountType, boolean expectActivityLaunch) {
        call("editProperties", void.class, new Object[] {response, accountType, expectActivityLaunch});
    }

    @Override
    public void confirmCredentialsAsUser(IAccountManagerResponse response, Account account, Bundle options, boolean expectActivityLaunch, int userId) {
        call("confirmCredentialsAsUser", void.class, new Object[] {response, account, options, expectActivityLaunch, userId});
    }

    @Override
    public boolean accountAuthenticated(Account account) {
        return (Boolean) call("accountAuthenticated", boolean.class, new Object[] {account});
    }

    @Override
    public void getAuthTokenLabel(IAccountManagerResponse response, String accountType, String authTokenType) {
        call("getAuthTokenLabel", void.class, new Object[] {response, accountType, authTokenType});
    }

    @Override
    public void addSharedAccountsFromParentUser(int parentUserId, int userId, String opPackageName) {
        call("addSharedAccountsFromParentUser", void.class, new Object[] {parentUserId, userId, opPackageName});
    }

    @Override
    public void renameAccount(IAccountManagerResponse response, Account accountToRename, String newName) {
        call("renameAccount", void.class, new Object[] {response, accountToRename, newName});
    }

    @Override
    public String getPreviousName(Account account) {
        return (String) call("getPreviousName", String.class, new Object[] {account});
    }

    @Override
    public void startAddAccountSession(IAccountManagerResponse response, String accountType, String authTokenType, String[] requiredFeatures, boolean expectActivityLaunch, Bundle options) {
        call("startAddAccountSession", void.class, new Object[] {response, accountType, authTokenType, requiredFeatures, expectActivityLaunch, options});
    }

    @Override
    public void startUpdateCredentialsSession(IAccountManagerResponse response, Account account, String authTokenType, boolean expectActivityLaunch, Bundle options) {
        call("startUpdateCredentialsSession", void.class, new Object[] {response, account, authTokenType, expectActivityLaunch, options});
    }

    @Override
    public void finishSessionAsUser(IAccountManagerResponse response, Bundle sessionBundle, boolean expectActivityLaunch, Bundle appInfo, int userId) {
        call("finishSessionAsUser", void.class, new Object[] {response, sessionBundle, expectActivityLaunch, appInfo, userId});
    }

    @Override
    public boolean someUserHasAccount(Account account) {
        return (Boolean) call("someUserHasAccount", boolean.class, new Object[] {account});
    }

    @Override
    public void isCredentialsUpdateSuggested(IAccountManagerResponse response, Account account, String statusToken) {
        call("isCredentialsUpdateSuggested", void.class, new Object[] {response, account, statusToken});
    }

    @Override
    public Map getPackagesAndVisibilityForAccount(Account account) {
        return (Map) call("getPackagesAndVisibilityForAccount", Map.class, new Object[] {account});
    }

    @Override
    public boolean addAccountExplicitlyWithVisibility(Account account, String password, Bundle extras, Map visibility, String opPackageName) {
        return (Boolean) call("addAccountExplicitlyWithVisibility", boolean.class, new Object[] {account, password, extras, visibility, opPackageName});
    }

    @Override
    public boolean setAccountVisibility(Account a, String packageName, int newVisibility) {
        return (Boolean) call("setAccountVisibility", boolean.class, new Object[] {a, packageName, newVisibility});
    }

    @Override
    public int getAccountVisibility(Account a, String packageName) {
        return (Integer) call("getAccountVisibility", int.class, new Object[] {a, packageName});
    }

    @Override
    public Map getAccountsAndVisibilityForPackage(String packageName, String accountType) {
        return (Map) call("getAccountsAndVisibilityForPackage", Map.class, new Object[] {packageName, accountType});
    }

    @Override
    public void registerAccountListener(String[] accountTypes, String opPackageName) {
        call("registerAccountListener", void.class, new Object[] {accountTypes, opPackageName});
    }

    @Override
    public void unregisterAccountListener(String[] accountTypes, String opPackageName) {
        call("unregisterAccountListener", void.class, new Object[] {accountTypes, opPackageName});
    }

    @Override
    public boolean hasAccountAccess(Account account, String packageName, UserHandle userHandle) {
        return (Boolean) call("hasAccountAccess", boolean.class, new Object[] {account, packageName, userHandle});
    }

    @Override
    public IntentSender createRequestAccountAccessIntentSenderAsUser(Account account, String packageName, UserHandle userHandle) {
        return (IntentSender) call("createRequestAccountAccessIntentSenderAsUser", IntentSender.class, new Object[] {account, packageName, userHandle});
    }

    @Override
    public void onAccountAccessed(String token) {
        call("onAccountAccessed", void.class, new Object[] {token});
    }
}

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AccountManagerFuture;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.content.ContextWrapper;
import android.os.Bundle;
import android.os.Parcelable;

import adapter.core.LocalServiceBinders;

/** Exercises the account manager in an ordinary app process without an account provider. */
public final class EmptyAccountLookupProbe {
    private static final String TYPE = "org.westlake.test.account";
    private static final String PKG = "org.westlake.test.client";

    private static final class Response extends IAccountManagerResponse.Stub {
        int calls;
        int errorCode;
        Bundle value;

        @Override public void onResult(Bundle result) {
            calls++;
            value = result;
        }

        @Override public void onError(int code, String message) {
            calls++;
            errorCode = code;
        }
    }

    public static String query() throws Exception {
        IAccountManager service = IAccountManager.Stub.asInterface(LocalServiceBinders.get("account"));
        Account account = new Account("fixture", TYPE);
        ContextWrapper context = new ContextWrapper(null) {
            @Override public String getOpPackageName() { return PKG; }
            @Override public int getUserId() { return 0; }
        };
        AccountManager manager = new AccountManager(context, service, null);
        try {
            AccountManagerFuture<Boolean> result = manager.hasFeatures(account, new String[0], null, null);
            if (!result.isDone()) throw new AssertionError("invisible account feature future left pending");
            throw new AssertionError("feature query should have refused the account type at submission");
        } catch (SecurityException expected) {}
        Response featureResponse = new Response();
        for (String[] features : new String[][] {{}, {"org.westlake.test.feature"}}) {
            try {
                service.hasFeatures(featureResponse, account, features, 0, PKG);
                throw new AssertionError("feature query for an invisible type returned normally");
            } catch (SecurityException expected) {}
        }
        try {
            service.hasFeatures(featureResponse, null, new String[0], 0, PKG);
            throw new AssertionError("null feature-query account accepted");
        } catch (IllegalArgumentException expected) {}
        try {
            service.hasFeatures(null, account, new String[0], 0, PKG);
            throw new AssertionError("null feature-query response accepted");
        } catch (IllegalArgumentException expected) {}
        try {
            service.hasFeatures(featureResponse, account, null, 0, PKG);
            throw new AssertionError("null feature array accepted");
        } catch (IllegalArgumentException expected) {}
        if (featureResponse.calls != 0) throw new AssertionError("refused feature query got a callback");
        for (String[] features : new String[][] {null, {"org.westlake.test.feature"}}) {
            Response many = new Response();
            service.getAccountsByFeatures(many, TYPE, features, PKG);
            if (many.calls != 1 || many.value == null) throw new AssertionError("missing list callback");
            Parcelable[] accounts = many.value.getParcelableArray(AccountManager.KEY_ACCOUNTS);
            if (accounts == null || accounts.length != 0 || many.value.size() != 1) {
                throw new AssertionError("expected an empty account list");
            }

            Response one = new Response();
            service.getAccountByTypeAndFeatures(one, TYPE, features, PKG);
            if (one.calls != 1 || one.value == null || !one.value.isEmpty()) {
                throw new AssertionError("expected an empty account selection");
            }
        }
        Response invalid = new Response();
        try {
            service.getAccountsByFeatures(invalid, null, null, PKG);
            throw new AssertionError("null account type accepted");
        } catch (IllegalArgumentException expected) {}
        if (invalid.calls != 0) throw new AssertionError("invalid request got a callback");

        Response enrollment = new Response();
        service.addAccount(enrollment, TYPE, null, null, true, new Bundle());
        if (enrollment.calls != 1 || enrollment.errorCode != AccountManager.ERROR_CODE_REMOTE_EXCEPTION
                || enrollment.value != null) {
            throw new AssertionError("missing authenticator must deliver exactly one error");
        }
        return "Invisible account feature queries were refused synchronously.\n"
                + "Empty account lookup and selection callbacks passed.\n"
                + "Enrollment without an authenticator returned one error and no login intent.\n";
    }
}

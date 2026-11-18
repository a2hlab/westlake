import adapter.core.LocalServiceBinders;
import android.accounts.AccountManager;
import android.accounts.IAccountManager;
import android.accounts.IAccountManagerResponse;
import android.os.Bundle;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Runs on the board with no account provider configured for the calling app. */
public final class UnavailableAccountAuthenticatorProbe {
    private static final class Response extends IAccountManagerResponse.Stub {
        private final CountDownLatch done = new CountDownLatch(1);
        private int errors;
        private int code;
        private boolean result;

        @Override public void onError(int error, String message) {
            errors++;
            code = error;
            done.countDown();
        }

        @Override public void onResult(Bundle bundle) {
            result = true;
            done.countDown();
        }

        void check(String method) throws InterruptedException {
            if (!done.await(2, TimeUnit.SECONDS)) {
                throw new AssertionError(method + " left the caller waiting");
            }
            if (result || errors != 1 || code != AccountManager.ERROR_CODE_REMOTE_EXCEPTION) {
                throw new AssertionError(method + " returned an unexpected result");
            }
        }
    }

    public static String query() throws Exception {
        IAccountManager accounts = IAccountManager.Stub.asInterface(LocalServiceBinders.get("account"));
        Response first = new Response();
        accounts.addAccount(first, "com.google", null, null, true, new Bundle());
        first.check("addAccount");
        Response second = new Response();
        accounts.addAccountAsUser(second, "com.google", null, null, true, new Bundle(), 0);
        second.check("addAccountAsUser");
        return "Both requests received the unavailable-authenticator error.\n";
    }
}

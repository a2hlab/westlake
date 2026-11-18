package adapter.core;

import android.accounts.AccountManager;
import android.content.Context;
import android.os.Process;

/** Board regression check for an ordinary, explicitly granted app with one Google test account. */
public final class PackageAccountVisibilityProbe {
    public static String check(Context app) {
        AccountManager manager = AccountManager.get(app);
        String own = app.getPackageName();
        int direct = manager.getAccountsByType("com.google").length;
        int self = manager.getAccountsByTypeForPackage(null, own).length;
        int missing = manager.getAccountsByTypeForPackage("com.google", "org.westlake.absent.accounttest").length;
        int otherType = manager.getAccountsByTypeForPackage("org.westlake.absent.type", own).length;
        boolean systemCallDenied = false;
        try {
            manager.getAccountsForPackage(own, Process.myUid());
        } catch (SecurityException expected) {
            systemCallDenied = true;
        }
        if (direct != 1 || self != 1 || missing != 0 || otherType != 0 || !systemCallDenied) {
            throw new AssertionError("Unexpected account visibility results");
        }
        return "direct=1 self=1 absent=0 otherType=0 systemCallDenied=true";
    }
}

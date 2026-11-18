import android.content.IIntentSender;
import android.content.Intent;
import android.os.Binder;
import android.os.Parcel;
import java.lang.reflect.Method;

/** Inert peer refusals for the board check of PendingIntent diagnostic redaction. */
public final class PendingActivityDenialProbe {
    private static void refused(Method prepare, String message) throws Exception {
        Binder peer = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                reply.writeException(new SecurityException(message));
                return true;
            }
        };
        Object result = prepare.invoke(null, IIntentSender.Stub.asInterface(peer), null, 0, 0);
        if (result != null) throw new AssertionError("A refused sender returned an Intent");
    }

    public static String query() throws Exception {
        Method prepare = Class.forName("adapter.activity.LocalIntentSenders").getDeclaredMethod(
                "prepareActivity", IIntentSender.class, Intent.class, int.class, int.class);
        prepare.setAccessible(true);
        refused(prepare, "stored activity Intent crossed an unverified or foreign hop");
        refused(prepare, "activity sender preparation requires a verified process of the same app");
        refused(prepare, "private-test-diagnostic-marker");
        return "All three refused senders returned no Intent.\n";
    }
}

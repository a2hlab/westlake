import android.content.ContentResolver;
import android.content.IContentService;
import android.database.IContentObserver;
import android.net.Uri;
import android.os.UserHandle;

import java.lang.reflect.Method;
import java.util.Arrays;

/** Headless ART test using the same content service as an app. All URIs are local test data. */
public final class LocalContentObserverProbe {
    private static final Uri INBOX = Uri.parse("content://org.westlake.syncprobe.data/conversations");
    private static final Uri ROW = Uri.withAppendedPath(INBOX, "7");
    private static final Uri OTHER = Uri.withAppendedPath(INBOX, "8");
    private static final String PACKAGE = "org.westlake.syncprobe";

    private static final class Observer extends IContentObserver.Stub {
        int calls;
        boolean self;
        int flags;
        int user;
        Uri[] changed;

        @Override public void onChange(boolean own, Uri uri, int userId) {
            onChangeEtc(own, new Uri[] {uri}, 0, userId);
        }
        @Override public void onChangeEtc(boolean own, Uri[] uris, int changeFlags, int userId) {
            calls++;
            self = own;
            changed = uris.clone();
            flags = changeFlags;
            user = userId;
        }
    }

    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static void notify(IContentService service, Observer origin, boolean self, int flags,
            int user, Uri... uris) throws Exception {
        service.notifyChange(uris, origin, self, flags, user, 35, PACKAGE);
    }

    public static String query() throws Exception {
        Method install = Class.forName("com.android.internal.os.AppSpawnXInit")
                .getDeclaredMethod("installContentServiceAdapter");
        install.setAccessible(true);
        install.invoke(null);
        IContentService service = ContentResolver.getContentService();
        int user = UserHandle.myUserId();
        String mode = System.getenv("OBSERVER_TEST_MODE");
        Observer exact = new Observer();
        service.registerContentObserver(ROW, false, exact, user, 35);
        notify(service, null, false, 0, user, ROW);
        if ("off".equals(mode)) {
            check(exact.calls == 0, "non-opted-in process received a notification");
            return "Ordinary process retained its disabled observer registry.\n";
        }
        check(exact.calls == 1, "registered observer received " + exact.calls + " callbacks");
        notify(service, exact, false, 0, user, ROW);
        check(exact.calls == 1, "unexpected self notification");
        notify(service, exact, true, ContentResolver.NOTIFY_UPDATE, user, ROW);
        check(exact.calls == 2 && exact.self && exact.flags == ContentResolver.NOTIFY_UPDATE
                && exact.user == user, "self notification metadata");
        if ("legacy".equals(mode)) {
            service.unregisterContentObserver(exact);
            notify(service, null, false, 0, user, ROW);
            check(exact.calls == 2, "unregister");
            return "Existing Play host registration, self flags and unregister passed.\n";
        }

        Observer parent = new Observer();
        service.registerContentObserver(INBOX, true, parent, UserHandle.USER_CURRENT, 35);
        Uri filtered = ROW.buildUpon().appendQueryParameter("filter", "local").build();
        notify(service, null, false, ContentResolver.NOTIFY_UPDATE, user, filtered, OTHER);
        check(parent.calls == 1 && Arrays.equals(parent.changed, new Uri[] {filtered, OTHER}),
                "batch or descendant notification");
        check(exact.calls == 3 && Arrays.equals(exact.changed, new Uri[] {filtered}), "path with query");
        notify(service, null, false, ContentResolver.NOTIFY_SKIP_NOTIFY_FOR_DESCENDANTS, user, INBOX);
        check(parent.calls == 1 && exact.calls == 4, "skip descendants on ancestor change");
        notify(service, null, false, ContentResolver.NOTIFY_SKIP_NOTIFY_FOR_DESCENDANTS, user, ROW);
        check(parent.calls == 2 && exact.calls == 5, "specific change after ancestor change");
        notify(service, null, false, 0, user, Uri.parse(INBOX.toString() + "-archive/7"),
                Uri.parse("content://org.westlake.other/conversations/7"));
        check(parent.calls == 2 && exact.calls == 5, "wrong path segment or authority");
        if ("sync".equals(mode)) {
            try {
                service.registerContentObserver(ROW, false, new Observer(), user + 1, 35);
                throw new AssertionError("foreign user registration");
            } catch (SecurityException expected) {}
            try {
                notify(service, null, false, 0, UserHandle.USER_ALL, ROW);
                throw new AssertionError("cross-user notification");
            } catch (SecurityException expected) {}
        }
        service.unregisterContentObserver(exact);
        service.unregisterContentObserver(parent);
        notify(service, null, false, 0, user, ROW);
        check(parent.calls == 2 && exact.calls == 5, "unregister");
        return "Content observer delivery, batch, flags, path matching and unregister passed.\n";
    }

    public static void main(String[] args) throws Exception {
        System.out.print(query());
    }
}

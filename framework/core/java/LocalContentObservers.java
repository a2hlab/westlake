package adapter.core;

import android.database.IContentObserver;
import android.net.Uri;
import android.os.RemoteException;
import android.util.Log;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * ContentService's observers, within one process. The shared Play services process needs them: it
 * caches Gservices values until told they changed, so an Android ID read as 0 before check-in stayed 0.
 * Other processes keep the plain answers until this has had a regression pass, since delivering
 * changes alters what every app that observes a provider does.
 */
public final class LocalContentObservers {
    private static final String TAG = "WL_Observers";

    private static final class Registration {
        final Uri uri;
        final boolean descendants;
        final IContentObserver observer;

        Registration(Uri uri, boolean descendants, IContentObserver observer) {
            this.uri = uri;
            this.descendants = descendants;
            this.observer = observer;
        }
    }

    private static final List<Registration> sRegistrations = new CopyOnWriteArrayList<>();

    private LocalContentObservers() {}

    /** Whether this process uses these rather than the plain answers. */
    public static boolean active() {
        return System.getenv("WESTLAKE_PLAY_SERVICES_SERVE") != null;
    }

    public static void register(Uri uri, boolean descendants, IContentObserver observer) {
        if (uri != null && observer != null) sRegistrations.add(new Registration(uri, descendants, observer));
    }

    public static void unregister(IContentObserver observer) {
        if (observer == null) return;
        sRegistrations.removeIf(r -> r.observer.asBinder() == observer.asBinder());
    }

    /**
     * As ObserverNode.collectObservers: an observer hears of a change to its own URI or below it if it
     * asked for descendants, and of a change at any URI above it.
     */
    public static void notifyChange(Uri[] uris, IContentObserver origin, boolean originWantsSelf, int flags, int user) {
        if (uris == null) return;
        for (Uri uri : uris) {
            for (Registration registration : sRegistrations) {
                if (!matches(registration, uri)) continue;
                boolean own = origin != null && registration.observer.asBinder() == origin.asBinder();
                if (own && !originWantsSelf) continue;
                try {
                    registration.observer.onChangeEtc(own, new Uri[] {uri}, flags, user);
                } catch (RemoteException | RuntimeException e) {
                    Log.w(TAG, "observer failed: " + e.getClass().getName());
                }
            }
        }
    }

    private static boolean matches(Registration registration, Uri changed) {
        String observed = registration.uri.toString();
        String uri = changed.toString();
        return uri.equals(observed) || observed.startsWith(uri + "/")
                || (registration.descendants && uri.startsWith(observed + "/"));
    }
}

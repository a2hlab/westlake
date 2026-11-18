package adapter.core;

import android.content.ContentResolver;
import android.database.IContentObserver;
import android.net.Uri;
import android.os.RemoteException;
import android.os.UserHandle;
import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;

/** Content provider observers for changes originating in the same process. */
public final class LocalContentObservers {
    private static final String TAG = "WL_Observers";
    private static final boolean PLAY_HOST = System.getenv("WESTLAKE_PLAY_SERVICES_SERVE") != null;

    private static final class Registration {
        final Uri uri;
        final boolean descendants;
        final IContentObserver observer;
        final int user;

        Registration(Uri uri, boolean descendants, IContentObserver observer, int user) {
            this.uri = uri;
            this.descendants = descendants;
            this.observer = observer;
            this.user = user;
        }

        boolean matches(Uri changed, int flags) {
            if (changed == null || uri.getAuthority() == null || !uri.getAuthority().equals(changed.getAuthority())) {
                return false;
            }
            List<String> observed = uri.getPathSegments();
            List<String> path = changed.getPathSegments();
            for (int i = 0; i < Math.min(observed.size(), path.size()); i++) {
                if (!observed.get(i).equals(path.get(i))) return false;
            }
            if (observed.size() < path.size()) return descendants;
            return !descendants || (flags & ContentResolver.NOTIFY_SKIP_NOTIFY_FOR_DESCENDANTS) == 0;
        }
    }

    private static final List<Registration> sRegistrations = new CopyOnWriteArrayList<>();

    private LocalContentObservers() {}

    public static boolean active() {
        return PLAY_HOST || "1".equals(System.getenv("WESTLAKE_LOCAL_SYNC"));
    }

    private static int resolveUser(int requested) {
        int current = UserHandle.myUserId();
        if (requested == current || requested == UserHandle.USER_CURRENT || requested == UserHandle.USER_CURRENT_OR_SELF) {
            return current;
        }
        // Play hosts also have local observers for multiple users. App-owned sync is single-user.
        if (PLAY_HOST && (requested >= 0 || requested == UserHandle.USER_ALL)) return requested;
        throw new SecurityException("content observers belong to the current user");
    }

    public static void register(Uri uri, boolean descendants, IContentObserver observer, int user) {
        int target = resolveUser(user);
        if (uri != null && observer != null) sRegistrations.add(new Registration(uri, descendants, observer, target));
    }

    public static void unregister(IContentObserver observer) {
        if (observer == null) return;
        sRegistrations.removeIf(r -> r.observer.asBinder() == observer.asBinder());
    }

    public static void notifyChange(Uri[] uris, IContentObserver origin, boolean originWantsSelf, int flags, int user) {
        int target = resolveUser(user);
        if (uris == null) return;
        Map<IContentObserver, List<Uri>> deliveries = new LinkedHashMap<>();
        for (Uri uri : uris) {
            for (Registration r : sRegistrations) {
                if (r.user != target && r.user != UserHandle.USER_ALL && target != UserHandle.USER_ALL) continue;
                if (!originWantsSelf && origin != null && r.observer.asBinder() == origin.asBinder()) continue;
                if (r.matches(uri, flags)) deliveries.computeIfAbsent(r.observer, k -> new ArrayList<>()).add(uri);
            }
        }
        for (Map.Entry<IContentObserver, List<Uri>> entry : deliveries.entrySet()) {
            IContentObserver observer = entry.getKey();
            boolean own = origin != null && observer.asBinder() == origin.asBinder();
            try {
                observer.onChangeEtc(own, entry.getValue().toArray(new Uri[0]), flags, target);
            } catch (RemoteException | RuntimeException e) {
                Log.w(TAG, "observer failed: " + e.getClass().getName());
            }
        }
    }
}

package adapter.compat;

import android.media.session.ISessionManager;
import android.os.IBinder;
import android.os.ServiceManager;

/** Exercises local service lookup and binder identity after installation. */
public final class WlMediaSessionHostTest {
    public static void main(String[] args) {
        String result = WlMediaSession.install();
        if (!result.contains("queryLocalInterface=OK")) {
            throw new AssertionError("Install failed: " + result);
        }
        IBinder binder = ServiceManager.sCache.get("media_session");
        if (binder == null) throw new AssertionError("ServiceManager lost binder");
        ISessionManager manager = (ISessionManager) binder.queryLocalInterface(
                "android.media.session.ISessionManager");
        if (manager == null || manager.asBinder() != binder ||
                binder.queryLocalInterface("android.media.session.WrongService") != null ||
                !"android.media.session.ISessionManager".equals(
                        binder.getInterfaceDescriptor()) || manager.getSessionsCount() != 0) {
            throw new AssertionError("Local binder contract failed");
        }
        System.out.println("MEDIA_SESSION_LOCAL_BINDER_HOST_PASS identity descriptor typed_default");
    }
}

package adapter.packagemanager;

import android.os.Binder;
import android.os.Bundle;
import android.os.IInterface;
import android.os.IUserManager;
import android.os.ServiceManager;
import android.os.UserHandle;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.Map;

/** Current-user lifecycle queries backed by the OpenHarmony account service. */
public final class OHUserManager {
    private static native int[] nativeState();

    private OHUserManager() {}

    public static void install() throws ReflectiveOperationException {
        System.load(System.getenv("WESTLAKE_ACCOUNT_HELPER_PATH"));
        final int androidUser = UserHandle.myUserId();
        Binder binder = new Binder();
        IUserManager service = (IUserManager) Proxy.newProxyInstance(IUserManager.class.getClassLoader(),
                new Class<?>[] {IUserManager.class}, (proxy, method, arguments) -> {
                    String name = method.getName();
                    if (name.equals("asBinder")) return binder;
                    if (name.equals("toString")) return "OHUserManager(user=" + androidUser + ")";
                    if (name.equals("hashCode")) return System.identityHashCode(proxy);
                    if (name.equals("equals")) return proxy == arguments[0];
                    if (name.equals("isUserUnlocked") || name.equals("isUserUnlockingOrUnlocked")
                            || name.equals("isUserRunning")) {
                        if (((Integer) arguments[0]) != androidUser) {
                            throw new SecurityException("OH account query is not for the current process user");
                        }
                        int[] state = nativeState();
                        // OH sets verified after successfully unlocking the account's
                        // storage key. Query it live; no Android cache is used here.
                        return state[1] != 0 && (name.equals("isUserRunning") || state[2] != 0);
                    }
                    // Restrictions and profile type have a truthful local answer: this process's
                    // user is the device's only, unrestricted, full user, and no device-policy
                    // app has set restrictions for any package. WebView reads them while it
                    // starts (CombinedPolicyProvider), inside a JNI callback, where a throw aborts
                    // Chromium.
                    switch (name) {
                        case "getApplicationRestrictions":
                        case "getApplicationRestrictionsForUser":
                        case "getUserRestrictions":
                        case "getUserRestrictionSources":
                            return method.getReturnType() == Bundle.class ? new Bundle() : null;
                        case "hasUserRestriction":
                        case "hasBaseUserRestriction":
                        case "hasUserRestrictionOnAnyUser":
                        case "isRestricted":
                        case "isManagedProfile":
                        case "isProfile":
                        case "isCloneProfile":
                        case "isMediaSharedWithParent":
                        case "isDemoUser":
                        case "isQuietModeEnabled":
                            return Boolean.FALSE;
                        default:
                            break;
                    }
                    throw new UnsupportedOperationException("OH user service does not implement " + name);
                });
        binder.attachInterface((IInterface) service, "android.os.IUserManager");
        Field cache = ServiceManager.class.getDeclaredField("sCache");
        cache.setAccessible(true);
        @SuppressWarnings("unchecked")
        Map<String, android.os.IBinder> services = (Map<String, android.os.IBinder>) cache.get(null);
        services.put("user", binder);
        int[] state = nativeState();
        System.err.println("[SOURCE-USER] androidUser=" + androidUser + " ohAccount=" + state[0]
                + " active=" + state[1] + " storageVerified=" + state[2]);
    }
}

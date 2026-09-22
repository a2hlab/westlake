package adapter.core;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * ILocationManager at the OH boundary.
 *
 * SystemServiceRegistry builds LocationManager from
 * ServiceManager.getServiceOrThrow(Context.LOCATION_SERVICE); with no binder
 * published the fetcher throws ServiceNotFoundException and getSystemService
 * returns null. Apps rarely null-check it: McDonald's calls
 * LocationUtil.isLocationEnabled() from AnalyticsHelper on the SplashActivity
 * onCreate callback and dies on the null reference before any UI is shown.
 *
 * Shape follows ConnectivityManagerAdapter: a reflective Proxy over the AIDL
 * interface attached to a local Binder, so Stub.asInterface resolves it through
 * queryLocalInterface without a transaction. That keeps this to one file rather
 * than 62 hand-written stubs, and new AIDL methods cannot break the build.
 *
 * Scope: this reports location as present but switched off -- providers exist,
 * none enabled, no fixes. That is a truthful answer for a board where OH
 * location is not wired through yet, and it takes apps down their
 * "user has location disabled" path rather than their crash path. Real routing
 * to OH's location service is a separate change; this is the compatibility
 * floor, not a substitute for it.
 */
public final class LocationManagerAdapter {
    private static final String TAG = "WESTLAKE-LOCATION";
    private static final String DESCRIPTOR = "android.location.ILocationManager";
    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());
    private static IBinder sBinder;

    private LocationManagerAdapter() {}

    public static synchronized IBinder getBinder() {
        if (sBinder != null) return sBinder;
        try {
            Class<?> iface = Class.forName(DESCRIPTOR);
            Object proxy = Proxy.newProxyInstance(LocationManagerAdapter.class.getClassLoader(),
                    new Class<?>[] { iface }, new Router());
            Binder binder = new Binder();
            binder.attachInterface((IInterface) proxy, DESCRIPTOR);
            sBinder = binder;
            System.err.println("[" + TAG + "] location service bound (providers present, disabled)");
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] location service unavailable: " + t);
        }
        return sBinder;
    }

    private static final class Router implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("asBinder".equals(name)) return getBinder();
            if (sSeen.add(name)) {
                System.err.println("[" + TAG + "] call " + name);
            }
            switch (name) {
                // Location exists as a concept, but nothing is turned on.
                case "isProviderEnabledForUser":
                case "isLocationEnabled":
                case "isLocationEnabledForUser":
                case "isProviderPackage":
                case "isAdasGnssLocationEnabledForUser":
                    return Boolean.FALSE;
                // Never hand back null where the framework iterates the result.
                case "getAllProviders":
                case "getProviders":
                case "getProviderPackages":
                    return new ArrayList<String>();
                case "getBestProvider":
                    return null;
                default:
                    return defaultValue(method.getReturnType());
            }
        }
    }

    /** Zero/false/null by return type, so unlisted AIDL methods stay harmless. */
    private static Object defaultValue(Class<?> type) {
        if (type == null || !type.isPrimitive()) return null;
        if (type == boolean.class) return Boolean.FALSE;
        if (type == byte.class) return (byte) 0;
        if (type == short.class) return (short) 0;
        if (type == char.class) return (char) 0;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0f;
        if (type == double.class) return 0d;
        return null;  // void
    }
}

package adapter.core;

import android.app.ActivityThread;
import android.app.Application;
import android.app.PendingIntent;
import android.content.Intent;
import android.location.Criteria;
import android.location.GnssCapabilities;
import android.location.ILocationCallback;
import android.location.ILocationListener;
import android.location.Location;
import android.location.LocationManager;
import android.location.LocationRequest;
import android.location.provider.ProviderProperties;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.ICancellationSignal;
import android.os.IInterface;
import android.os.Looper;
import android.os.RemoteException;
import android.os.SystemClock;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * ILocationManager over OH's location service (westlake ADR-0009 decision 1): the service a HAP asks for the
 * same thing, through its NDK (location_natives.c in libwl_missing_natives).
 *
 * SystemServiceRegistry builds LocationManager from ServiceManager.getServiceOrThrow(Context.LOCATION_SERVICE);
 * with no binder published getSystemService returns null, and apps rarely check it (McDonald's died on it). The
 * binder is a reflective Proxy over the AIDL interface attached to a local Binder, as ConnectivityManagerAdapter
 * is, so new AIDL methods cannot break the build.
 *
 * What it answers:
 * - Location is on when OH's location switch is (OH_Location_IsLocatingEnabled). The providers are Android's four:
 *   gps, network, fused and passive. OH chooses the source itself.
 * - Requests (registerLocationListener, registerLocationPendingIntent, getCurrentLocation) become OH requests:
 *   the interval from the LocationRequest, the power scene from its quality. Their fixes reach the listener,
 *   PendingIntent or callback as Android's location manager delivers them, with the request's minimum distance,
 *   minimum interval and maximum update count applied. Unregistering stops the OH request. getCurrentLocation
 *   answers its first fix, or null at its duration (30 seconds by default), and can be cancelled.
 * - getLastLocation answers the last fix this process received, or null.
 * - For a foreground location request, the APK must declare a location permission and its HAP must hold
 *   the corresponding OH grant. Android fine location maps to ohos.permission.LOCATION and coarse location
 *   maps to ohos.permission.APPROXIMATELY_LOCATION. The checks read the current grant.
 *   With neither permission, or with location switched off, listeners receive no fixes, getCurrentLocation
 *   returns null and there is no last known location. The host's permission applies to the HAP;
 *   independent user grants for APKs sharing that HAP need separate enforcement.
 * - Without OH's location library the compatibility floor stays: providers absent, location off, no fixes.
 * - Not bridged: geocoding (isGeocodeAvailable false), GNSS status, NMEA, measurements and batching (accepted, no
 *   callbacks, as on a device without that data). Coordinates are never logged.
 */
public final class LocationManagerAdapter {
    private static final String TAG = "WESTLAKE-LOCATION";
    private static final String DESCRIPTOR = "android.location.ILocationManager";
    private static final Set<String> sSeen = Collections.synchronizedSet(new HashSet<String>());
    private static IBinder sBinder;

    /** OH's result codes (Location_ResultCode). */
    private static final int OH_PERMISSION_DENIED = 201;
    private static final int OH_SERVICE_UNAVAILABLE = 3301000;
    private static final int OH_SWITCH_OFF = 3301100;
    /** OH's power scenes (Location_PowerConsumptionScene). */
    private static final int OH_HIGH_POWER = 0x0601;
    private static final int OH_LOW_POWER = 0x0602;
    private static final int OH_NO_POWER = 0x0603;
    /** LocationManager.getCurrentLocation's default timeout. */
    private static final long CURRENT_LOCATION_TIMEOUT_MS = 30_000;

    private static final List<String> PROVIDERS = Collections.unmodifiableList(Arrays.asList(
            LocationManager.PASSIVE_PROVIDER, LocationManager.NETWORK_PROVIDER, LocationManager.FUSED_PROVIDER,
            LocationManager.GPS_PROVIDER));

    private static final AtomicInteger NEXT_ID = new AtomicInteger(1);
    private static final Map<Integer, Request> REQUESTS = new ConcurrentHashMap<>();
    /** Requests by what the app registered: a listener's binder, or a PendingIntent. */
    private static final Map<Object, Request> BY_KEY = new ConcurrentHashMap<>();
    private static volatile Location sLast;
    private static volatile boolean sNoNatives;

    private static native int nativeEnabled();
    private static native long nativeStart(int requestId, int intervalSeconds, int powerScene);
    private static native int nativeStop(long handle);

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
            System.err.println("[" + TAG + "] location service bound (OH location service, switch "
                    + describe(state()) + ")");
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] location service unavailable: " + t);
        }
        return sBinder;
    }

    /** OH's switch: 1 on, 0 off, negative when OH could not be asked (-1: no location library here). */
    private static int state() {
        if (sNoNatives) return -1;
        try {
            return nativeEnabled();
        } catch (UnsatisfiedLinkError e) {
            sNoNatives = true;
            return -1;
        }
    }

    private static String describe(int state) {
        return state == 1 ? "on" : state == 0 ? "off" : state == -1 ? "unknown: no OH location library"
                : "unknown: OH answered " + (-state);
    }

    private static boolean available() {
        return state() >= 0;
    }

    private static boolean locationOn() {
        return state() == 1;
    }

    /** Whether OH lets the host application, whose identity every Westlake process carries, locate. */
    private static boolean hostMayLocate() {
        int uid = android.os.Process.myUid();
        try {
            return adapter.packagemanager.PermissionManagerAdapter.checkProcessPermission(
                    android.Manifest.permission.ACCESS_COARSE_LOCATION, -1, uid) == 0
                    || adapter.packagemanager.PermissionManagerAdapter.checkProcessPermission(
                    android.Manifest.permission.ACCESS_FINE_LOCATION, -1, uid) == 0;
        } catch (Throwable t) {   // no permission helper in this launch: nothing says OH granted it
            return false;
        }
    }

    /** One app request, backed by one OH request while it runs. */
    private static final class Request {
        final int id = NEXT_ID.getAndIncrement();
        final String provider;
        final LocationRequest request;
        final Object key;
        ILocationListener listener;
        PendingIntent pendingIntent;
        ILocationCallback callback;
        // Zero until StartLocating returns its handle. A negative value closes the request.
        final AtomicLong handle = new AtomicLong();
        int delivered;
        Location lastDelivered;
        long lastDeliveredAt;

        Request(String provider, LocationRequest request, Object key) {
            this.provider = provider != null ? provider : LocationManager.FUSED_PROVIDER;
            this.request = request;
            this.key = key;
        }
    }

    /**
     * OH's power scene for a request: GNSS (high power) for the gps provider, as Android's gps provider is GNSS, and
     * for a high-accuracy request; passive for the passive provider or interval; otherwise OH's low-power scene,
     * network positioning. On a device without a network location service (this board: OH's NetworkAbility finds
     * none) OH files a low-power request as passive and it gets fixes only while someone else locates.
     *
     * WESTLAKE_LOCATION_GNSS=0 keeps every request off GNSS (low power instead), for a device where starting GNSS
     * does harm. On the test board OH's GNSS start fails, and about two minutes later its WiFi driver fails every
     * scan and WiFi stays down until a reboot (seen twice; the radio is likely shared). A HAP asking for GNSS does
     * the same there; the switch keeps Westlake's apps from doing it.
     */
    private static int powerScene(String provider, LocationRequest request) {
        if (LocationManager.PASSIVE_PROVIDER.equals(provider)) return OH_NO_POWER;
        if (request != null && request.getIntervalMillis() == LocationRequest.PASSIVE_INTERVAL) return OH_NO_POWER;
        if (!GNSS) return OH_LOW_POWER;
        if (LocationManager.GPS_PROVIDER.equals(provider)) return OH_HIGH_POWER;
        return request != null && request.getQuality() == LocationRequest.QUALITY_HIGH_ACCURACY
                ? OH_HIGH_POWER : OH_LOW_POWER;
    }

    private static final boolean GNSS = !"0".equals(System.getenv("WESTLAKE_LOCATION_GNSS"));

    private static int intervalSeconds(LocationRequest request) {
        long millis = request != null ? request.getIntervalMillis() : 1000;
        if (millis == LocationRequest.PASSIVE_INTERVAL) return 60;
        return (int) Math.max(1, Math.min(Integer.MAX_VALUE, millis / 1000));
    }

    /**
     * Starts an OH request for r, unless OH cannot give it fixes now; true when it runs. The app's request stays
     * registered either way, as Android keeps a request made while location is off.
     */
    private static boolean start(Request r, String how) {
        REQUESTS.put(r.id, r);
        if (r.key != null) stop(BY_KEY.put(r.key, r), "replaced");
        if (!locationOn()) {
            System.err.println("[" + TAG + "] " + how + " " + r.id + " for " + r.provider
                    + ": registered, no fixes while location is " + describe(state()));
            return false;
        }
        if (!hostMayLocate()) {
            System.err.println("[" + TAG + "] " + how + " " + r.id + " for " + r.provider
                    + ": registered, no fixes: OH has not granted the host application location");
            return false;
        }
        long handle;
        try {
            handle = nativeStart(r.id, intervalSeconds(r.request), powerScene(r.provider, r.request));
        } catch (UnsatisfiedLinkError e) {
            sNoNatives = true;
            handle = -1;
        }
        if (handle <= 0) {
            int code = (int) -handle;
            System.err.println("[" + TAG + "] " + how + " " + r.id + " for " + r.provider + ": OH did not start it ("
                    + (code == OH_PERMISSION_DENIED ? "permission denied" : code == OH_SWITCH_OFF ? "switch off"
                    : code == OH_SERVICE_UNAVAILABLE ? "service unavailable" : "code " + code) + "); no fixes");
            return false;
        }
        if (!attachNativeHandle(r, handle)) return false;
        System.err.println("[" + TAG + "] " + how + " " + r.id + " for " + r.provider + ": OH request started, every "
                + intervalSeconds(r.request) + " s, power scene 0x" + Integer.toHexString(powerScene(r.provider, r.request)));
        return true;
    }

    private static boolean attachNativeHandle(Request r, long handle) {
        if (r.handle.compareAndSet(0, handle)) return true;
        // A single fix or an unregister can finish this request inside StartLocating.
        stopNative(r, handle, "finished during start");
        return false;
    }

    private static boolean stop(Request r, String why) {
        if (r == null) return false;
        long handle = r.handle.getAndSet(-1);
        if (handle < 0) return false;
        REQUESTS.remove(r.id, r);
        if (r.key != null) BY_KEY.remove(r.key, r);
        if (handle > 0) stopNative(r, handle, why);
        return true;
    }

    private static void stopNative(Request r, long handle, String why) {
        int rc;
        try {
            rc = nativeStop(handle);
        } catch (UnsatisfiedLinkError e) {
            rc = -1;
        }
        System.err.println("[" + TAG + "] request " + r.id + " stopped (" + why + "), OH " + rc + ", "
                + r.delivered + " fix(es) delivered");
    }

    /** A fix from OH (location_natives.c), on the thread OH calls back on. */
    @SuppressWarnings("unused")
    static void onOhLocation(int id, double latitude, double longitude, double altitude, double accuracy,
            double speed, double bearing, long timeMillis, long elapsedNanos, double altitudeAccuracy,
            double speedAccuracy, double bearingAccuracy, int sourceType) {
        Request r = REQUESTS.get(id);
        if (r == null) return;
        Location location = new Location(r.provider);
        location.setLatitude(latitude);
        location.setLongitude(longitude);
        location.setAltitude(altitude);
        location.setAccuracy((float) accuracy);
        location.setSpeed((float) speed);
        location.setBearing((float) bearing);
        location.setTime(timeMillis > 0 ? timeMillis : System.currentTimeMillis());
        location.setElapsedRealtimeNanos(elapsedNanos > 0 ? elapsedNanos : SystemClock.elapsedRealtimeNanos());
        if (altitudeAccuracy > 0) location.setVerticalAccuracyMeters((float) altitudeAccuracy);
        if (speedAccuracy > 0) location.setSpeedAccuracyMetersPerSecond((float) speedAccuracy);
        if (bearingAccuracy > 0) location.setBearingAccuracyDegrees((float) bearingAccuracy);
        sLast = location;
        deliver(r, location, sourceType);
    }

    private static void deliver(Request r, Location location, int sourceType) {
        LocationRequest request = r.request;
        if (request != null && r.lastDelivered != null) {
            long since = SystemClock.elapsedRealtime() - r.lastDeliveredAt;
            if (since < request.getMinUpdateIntervalMillis()) return;
            if (location.distanceTo(r.lastDelivered) < request.getMinUpdateDistanceMeters()) return;
        }
        r.lastDelivered = location;
        r.lastDeliveredAt = SystemClock.elapsedRealtime();
        r.delivered++;
        if (r.delivered == 1) {
            System.err.println("[" + TAG + "] request " + r.id + ": first fix from OH (source " + sourceType
                    + ", accuracy " + Math.round(location.getAccuracy()) + " m)");
        }
        try {
            if (r.callback != null) {
                if (stop(r, "answered")) r.callback.onLocation(new Location(location));
                return;
            }
            if (r.listener != null) {
                r.listener.onLocationChanged(Collections.singletonList(new Location(location)), null);
            } else if (r.pendingIntent != null) {
                Application app = ActivityThread.currentApplication();
                r.pendingIntent.send(app, 0, new Intent().putExtra(LocationManager.KEY_LOCATION_CHANGED,
                        new Location(location)));
            }
        } catch (RemoteException | PendingIntent.CanceledException | RuntimeException e) {
            stop(r, "its receiver is gone: " + e);
            return;
        }
        if (request != null && request.getMaxUpdates() != Integer.MAX_VALUE && r.delivered >= request.getMaxUpdates()) {
            stop(r, "its " + request.getMaxUpdates() + " update(s) delivered");
        }
    }

    /** getCurrentLocation's cancellation: stops the request it made. */
    private static final class Cancel extends ICancellationSignal.Stub {
        private final Request mRequest;

        Cancel(Request request) {
            mRequest = request;
        }

        @Override
        public void cancel() {
            stop(mRequest, "cancelled");
        }
    }

    private static Object currentLocation(String provider, LocationRequest request, ILocationCallback callback) {
        final Request r = new Request(provider, request, null);
        r.callback = callback;
        boolean running = start(r, "current location");
        long timeout = request != null && request.getDurationMillis() != Long.MAX_VALUE
                ? request.getDurationMillis() : CURRENT_LOCATION_TIMEOUT_MS;
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            if (!stop(r, running ? "no fix in " + timeout + " ms" : "unavailable")) return;
            try {
                callback.onLocation(null);
            } catch (RemoteException | RuntimeException ignored) {
            }
        }, running ? timeout : 0);
        return new Cancel(r);
    }

    private static void registerListener(String provider, LocationRequest request, ILocationListener listener) {
        IBinder key = listener.asBinder();
        Request r = new Request(provider, request, key);
        r.listener = listener;
        start(r, "listener");
    }

    private static void registerPendingIntent(String provider, LocationRequest request, PendingIntent pendingIntent) {
        Request r = new Request(provider, request, pendingIntent);
        r.pendingIntent = pendingIntent;
        start(r, "PendingIntent");
    }

    private static ProviderProperties properties(String provider) {
        if (!PROVIDERS.contains(provider) || LocationManager.PASSIVE_PROVIDER.equals(provider)) return null;
        ProviderProperties.Builder builder = new ProviderProperties.Builder();
        if (LocationManager.GPS_PROVIDER.equals(provider)) {
            builder.setHasSatelliteRequirement(true).setHasAltitudeSupport(true).setHasSpeedSupport(true)
                    .setHasBearingSupport(true).setPowerUsage(ProviderProperties.POWER_USAGE_HIGH)
                    .setAccuracy(ProviderProperties.ACCURACY_FINE);
        } else if (LocationManager.NETWORK_PROVIDER.equals(provider)) {
            builder.setHasNetworkRequirement(true).setPowerUsage(ProviderProperties.POWER_USAGE_LOW)
                    .setAccuracy(ProviderProperties.ACCURACY_COARSE);
        } else {
            builder.setHasAltitudeSupport(true).setHasSpeedSupport(true).setHasBearingSupport(true)
                    .setPowerUsage(ProviderProperties.POWER_USAGE_LOW).setAccuracy(ProviderProperties.ACCURACY_FINE);
        }
        return builder.build();
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
                case "isLocationEnabledForUser":
                    return locationOn();
                case "isProviderEnabledForUser":
                    return locationOn() && PROVIDERS.contains((String) args[0]);
                case "hasProvider":
                    return available() && PROVIDERS.contains((String) args[0]);
                case "getAllProviders":
                    return available() ? new ArrayList<>(PROVIDERS) : new ArrayList<String>();
                case "getProviders": {
                    boolean enabledOnly = (Boolean) args[1];
                    boolean none = !available() || (enabledOnly && !locationOn());
                    return none ? new ArrayList<String>() : new ArrayList<>(PROVIDERS);
                }
                case "getBestProvider": {
                    if (!available() || ((Boolean) args[1] && !locationOn())) return null;
                    Criteria criteria = (Criteria) args[0];
                    return criteria != null && criteria.getAccuracy() == Criteria.ACCURACY_FINE
                            ? LocationManager.GPS_PROVIDER : LocationManager.FUSED_PROVIDER;
                }
                case "getProviderProperties":
                    return properties((String) args[0]);
                case "getLastLocation": {
                    Location last = sLast;
                    if (last == null || !locationOn() || !hostMayLocate()) return null;
                    Location copy = new Location(last);
                    copy.setProvider((String) args[0]);
                    return copy;
                }
                case "getCurrentLocation":
                    return currentLocation((String) args[0], (LocationRequest) args[1], (ILocationCallback) args[2]);
                case "registerLocationListener":
                    registerListener((String) args[0], (LocationRequest) args[1], (ILocationListener) args[2]);
                    return null;
                case "unregisterLocationListener":
                    stop(BY_KEY.get(((ILocationListener) args[0]).asBinder()), "unregistered");
                    return null;
                case "registerLocationPendingIntent":
                    registerPendingIntent((String) args[0], (LocationRequest) args[1], (PendingIntent) args[2]);
                    return null;
                case "unregisterLocationPendingIntent":
                    stop(BY_KEY.get(args[0]), "unregistered");
                    return null;
                case "requestListenerFlush":
                    // Nothing is batched here: every fix is delivered as it comes, so the flush is complete.
                    try {
                        ((ILocationListener) args[1]).onFlushComplete((Integer) args[2]);
                    } catch (RemoteException ignored) {
                    }
                    return null;
                case "requestPendingIntentFlush":
                    try {
                        ((PendingIntent) args[1]).send(ActivityThread.currentApplication(), 0,
                                new Intent().putExtra(LocationManager.KEY_FLUSH_COMPLETE, (Integer) args[2]));
                    } catch (PendingIntent.CanceledException | RuntimeException ignored) {
                    }
                    return null;
                case "getGnssCapabilities":
                    return new GnssCapabilities.Builder().build();
                case "isGeocodeAvailable":
                case "isProviderPackage":
                case "isAdasGnssLocationEnabledForUser":
                case "isExtraLocationControllerPackageEnabled":
                    return Boolean.FALSE;
                case "getProviderPackages":
                    return new ArrayList<String>();
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

import android.location.Criteria;
import android.location.ILocationCallback;
import android.location.ILocationListener;
import android.location.ILocationManager;
import android.location.Location;
import android.location.LocationManager;
import android.location.LocationRequest;
import android.os.IRemoteCallback;
import adapter.core.LocationManagerAdapter;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;

/** Runs in a fresh headless ART process with the in-process location_request_fixture backend. */
public final class LocationRequestLifecycleProbe {
    private static native boolean isFixtureBound();
    private static native void emit();
    private static native int stopCount();
    private static native void switchOff();

    private static Method method(Class<?> owner, String name, Class<?>... args) throws Exception {
        Method m = owner.getDeclaredMethod(name, args);
        m.setAccessible(true);
        return m;
    }

    private static Field field(Class<?> owner, String name) throws Exception {
        Field f = owner.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static void checkGpsPolicy(ILocationManager service, boolean gnssEnabled) throws Exception {
        String gps = LocationManager.GPS_PROVIDER;
        String fused = LocationManager.FUSED_PROVIDER;
        String network = LocationManager.NETWORK_PROVIDER;
        check(service.hasProvider(gps) && service.getAllProviders().contains(gps)
                && service.getProviders(null, false).contains(gps), "GPS missing from installed providers");
        check(service.isProviderEnabledForUser(gps, 0) == gnssEnabled, "GPS enabled state differs from policy");
        List<String> enabled = service.getProviders(null, true);
        check(enabled.contains(gps) == gnssEnabled && enabled.contains(network) && enabled.contains(fused),
                "enabled provider list differs from policy");
        Criteria fine = new Criteria();
        fine.setAccuracy(Criteria.ACCURACY_FINE);
        check((gnssEnabled ? gps : fused).equals(service.getBestProvider(fine, true)), "best enabled provider");
        check(gps.equals(service.getBestProvider(fine, false)), "best installed provider");
    }

    private static void checkProviderScenes(Class<?> adapter, boolean gnssEnabled) throws Exception {
        Method scene = method(adapter, "powerScene", String.class, LocationRequest.class);
        LocationRequest high = new LocationRequest.Builder(1000)
                .setQuality(LocationRequest.QUALITY_HIGH_ACCURACY).build();
        check((Integer) scene.invoke(null, "network", high) == 0x0602,
                "explicit network request selected satellite power");
        check((Integer) scene.invoke(null, "fused", high) == (gnssEnabled ? 0x0601 : 0x0602),
                "high accuracy fused power");
        check((Integer) scene.invoke(null, "passive", high) == 0x0603, "passive power");
    }

    private static void sample(Method receive, int id, long when, int source) throws Exception {
        // Synthetic NDK callback. Elapsed time identifies samples without using any real position.
        receive.invoke(null, id, 0d, 0d, 0d, 10d, 0d, 0d, when, when, 0d, 0d, 0d, source);
    }

    private static void checkFixSources(Class<?> adapter, Constructor<?> constructor, Field id,
            Map<Integer, Object> requests, boolean gnssEnabled) throws Exception {
        Method receive = method(adapter, "onOhLocation", int.class, double.class, double.class,
                double.class, double.class, double.class, double.class, long.class, long.class,
                double.class, double.class, double.class, int.class);
        Object network = constructor.newInstance("network", null, null);
        Object gps = constructor.newInstance("gps", null, null);
        int netId = id.getInt(network);
        int gpsId = id.getInt(gps);
        requests.put(netId, network);
        if (gnssEnabled) requests.put(gpsId, gps);
        try {
            sample(receive, netId, 10, 2);
            sample(receive, netId, 20, 1);
            check(field(network.getClass(), "delivered").getInt(network) == 1,
                    "satellite sample delivered as network");
            sample(receive, netId, 15, 3);
            check(field(network.getClass(), "delivered").getInt(network) == 2,
                    "network missed indoor positioning");
            if (gnssEnabled) {
                sample(receive, gpsId, 30, 2);
                sample(receive, gpsId, 40, 3);
                sample(receive, gpsId, 45, 0);
                check(field(gps.getClass(), "delivered").getInt(gps) == 0,
                        "GPS accepted a non-satellite fix");
            }
            Method last = method(adapter, "lastLocation", String.class);
            check(last.invoke(null, "gps") == null, "GPS borrowed another provider's fix");
            if (gnssEnabled) {
                sample(receive, gpsId, 50, 1);
                sample(receive, gpsId, 60, 4);
                check(field(gps.getClass(), "delivered").getInt(gps) == 2, "missing GNSS or RTK fix");
                check(((Location) last.invoke(null, "gps")).getElapsedRealtimeNanos() == 60,
                        "missing cached satellite fix");
            }
            sample(receive, netId, 5, 2);
            Location stored = (Location) last.invoke(null, "network");
            check(stored.getElapsedRealtimeNanos() == 15, "network cache lost its newer fix");
            stored.setProvider("changed by client");
            check("network".equals(((Location) last.invoke(null, "network")).getProvider()),
                    "client mutated cached provider");
            Location passive = (Location) last.invoke(null, "passive");
            check((gnssEnabled ? "gps" : "network").equals(passive.getProvider())
                    && passive.getElapsedRealtimeNanos() == (gnssEnabled ? 60 : 15),
                    "passive cache lost origin or latest time");
            check(last.invoke(null, "fused") == null, "fused borrowed another provider's fix");
            Object fused = constructor.newInstance("fused", null, null);
            int fusedId = id.getInt(fused);
            requests.put(fusedId, fused);
            try {
                sample(receive, fusedId, 70, 2);
                sample(receive, fusedId, 80, 1);
                check(field(fused.getClass(), "delivered").getInt(fused) == 2,
                        "fused rejected a network or satellite result");
                check(((Location) last.invoke(null, "network")).getElapsedRealtimeNanos() == 15,
                        "fused update replaced a network fix");
            } finally {
                requests.remove(fusedId);
            }
        } finally {
            requests.remove(netId);
            requests.remove(gpsId);
        }
        ILocationManager service = ILocationManager.Stub.asInterface(LocationManagerAdapter.getBinder());
        try {
            service.getCurrentLocation("not-installed", null, new ILocationCallback.Stub() {
                @Override public void onLocation(Location unused) {
                    throw new AssertionError("unknown provider received a callback");
                }
            }, "test.location", null, "probe");
            throw new AssertionError("unknown location provider accepted");
        } catch (IllegalArgumentException expected) {
            // The adapter rejects the request before allocating a native subscription or cache entry.
        }
    }

    private static void checkListenerReplacement(Map<Integer, Object> requests, Map<?, ?> keys) throws Exception {
        switchOff();
        ILocationManager service = ILocationManager.Stub.asInterface(LocationManagerAdapter.getBinder());
        check(service.getLastLocation("network", null, "test.location", null) == null,
                "system location off returned a cached fix");
        check(service.getProviders(null, true).isEmpty() && service.getBestProvider(null, true) == null,
                "enabled providers while system location is off");
        ILocationListener listener = new ILocationListener.Stub() {
            @Override public void onLocationChanged(List<Location> locations, IRemoteCallback done) {
                throw new AssertionError("listener received a location while off");
            }
            @Override public void onFlushComplete(int requestCode) {}
            @Override public void onProviderEnabledChanged(String provider, boolean enabled) {}
        };
        service.registerLocationListener("network", null, listener, "test.location", null, "probe");
        Object original = keys.get(listener.asBinder());
        check(original != null && requests.size() == 1, "listener was not registered");
        service.registerLocationListener("network", null, listener, "test.location", null, "probe");
        check(requests.size() == 1 && keys.size() == 1 && !requests.containsValue(original),
                "replacement left the old subscription registered");
        service.unregisterLocationListener(listener);
        check(requests.isEmpty() && keys.isEmpty() && stopCount() == 2, "listener unregister cleanup");
    }

    @SuppressWarnings("unchecked")
    public static String query(String bridgeLibrary, String fixtureLibrary) throws Exception {
        System.load(fixtureLibrary);
        check(isFixtureBound(), "test location provider not loaded");
        System.load(bridgeLibrary);
        checkGpsPolicy(ILocationManager.Stub.asInterface(LocationManagerAdapter.getBinder()),
                !"0".equals(System.getenv("WESTLAKE_LOCATION_GNSS")));
        Class<?> adapter = Class.forName("adapter.core.LocationManagerAdapter");
        Class<?> request = Class.forName("adapter.core.LocationManagerAdapter$Request");
        Constructor<?> constructor = request.getDeclaredConstructor(String.class, LocationRequest.class, Object.class);
        constructor.setAccessible(true);
        Field id = field(request, "id");
        Map<Integer, Object> requests = (Map<Integer, Object>) field(adapter, "REQUESTS").get(null);
        boolean gnssEnabled = !"0".equals(System.getenv("WESTLAKE_LOCATION_GNSS"));
        checkProviderScenes(adapter, gnssEnabled);
        checkFixSources(adapter, constructor, id, requests, gnssEnabled);
        Method start = method(adapter, "nativeStart", int.class, int.class, int.class);
        Method stop = method(adapter, "stop", request, String.class);
        int[] answers = {0};
        ILocationCallback callback = new ILocationCallback.Stub() {
            @Override public void onLocation(Location location) {
                check(location != null, "missing synthetic fix");
                if (++answers[0] == 1) emit();
            }
        };

        Object early = constructor.newInstance("network", null, null);
        field(request, "callback").set(early, callback);
        requests.put(id.getInt(early), early);
        // The fake provider calls back inside start, before Java can take ownership of the handle.
        long earlyHandle = (Long) start.invoke(null, id.getInt(early), 1, 0x0602);
        check(earlyHandle > 0, "test provider failed to start");
        check(answers[0] == 1, "one-shot callback delivered " + answers[0] + " times");
        Method attach = method(adapter, "attachNativeHandle", request, long.class);
        check(!(Boolean) attach.invoke(null, early, earlyHandle), "already answered request stayed active");
        emit();
        stop.invoke(null, early, "duplicate stop");
        check(stopCount() == 1 && answers[0] == 1 && requests.isEmpty(), "early completion cleanup");

        // The second provider request waits for explicit delivery so cancellation can win.
        Object cancelled = constructor.newInstance("network", null, null);
        field(request, "callback").set(cancelled, callback);
        requests.put(id.getInt(cancelled), cancelled);
        long handle = (Long) start.invoke(null, id.getInt(cancelled), 1, 0x0602);
        check(handle > 0 && (Boolean) attach.invoke(null, cancelled, handle), "registration failed");
        stop.invoke(null, cancelled, "cancelled");
        emit();
        stop.invoke(null, cancelled, "duplicate stop");
        check(stopCount() == 2 && answers[0] == 1 && requests.isEmpty(), "late callback after cancellation");
        checkListenerReplacement(requests, (Map<?, ?>) field(adapter, "BY_KEY").get(null));
        return "One answer, two native stops, zero pending requests.\n";
    }

    public static void main(String[] args) throws Exception {
        System.out.print(query(args[0], args[1]));
    }
}

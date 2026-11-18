import android.location.ILocationCallback;
import android.location.ILocationListener;
import android.location.ILocationManager;
import android.location.Location;
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

    private static void checkListenerReplacement(Map<Integer, Object> requests, Map<?, ?> keys) throws Exception {
        switchOff();
        ILocationManager service = ILocationManager.Stub.asInterface(LocationManagerAdapter.getBinder());
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
        Class<?> adapter = Class.forName("adapter.core.LocationManagerAdapter");
        Class<?> request = Class.forName("adapter.core.LocationManagerAdapter$Request");
        Constructor<?> constructor = request.getDeclaredConstructor(String.class, LocationRequest.class, Object.class);
        constructor.setAccessible(true);
        Field id = field(request, "id");
        Map<Integer, Object> requests = (Map<Integer, Object>) field(adapter, "REQUESTS").get(null);
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

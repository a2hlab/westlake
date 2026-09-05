package android.bluetooth;

import android.app.SystemServiceRegistry;
import android.content.Context;

/**
 * Bluetooth mainline framework entry point.
 *
 * WESTLAKE §681: preserve AOSP's Context service registration even though
 * this board has no Android bluetooth daemon.  The wrapper itself remains
 * useful to framework clients; its adapter is null when the platform service
 * is unavailable, matching Android's no-Bluetooth-device behavior.
 */
public final class BluetoothFrameworkInitializer {
    private BluetoothFrameworkInitializer() {}

    public static void registerServiceWrappers() {
        SystemServiceRegistry.registerContextAwareService(
                Context.BLUETOOTH_SERVICE,
                BluetoothManager.class,
                context -> new BluetoothManager(context));
    }

    public static void setBluetoothServiceManager(android.os.BluetoothServiceManager a0) { /* no-op */ }
    public static void setBinderCallsStatsInitializer(java.util.function.Consumer<android.content.Context> a0) { /* no-op */ }
}

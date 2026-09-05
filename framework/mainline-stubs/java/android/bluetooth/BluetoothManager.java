package android.bluetooth;

import android.content.Context;

/**
 * Android Bluetooth system-service wrapper.
 *
 * The implementation follows AOSP's public behavior: construction is tied to
 * an application Context and getAdapter() returns the platform default
 * adapter.  On this OH target there is no Android Bluetooth service, so the
 * default adapter is null rather than a fabricated connected device.
 */
public final class BluetoothManager {
    private final BluetoothAdapter mAdapter;

    public BluetoothManager(Context context) {
        context = context.getApplicationContext();
        if (context == null) {
            throw new IllegalArgumentException(
                    "context not associated with any application (using a mock context?)");
        }
        mAdapter = BluetoothAdapter.getDefaultAdapter();
    }

    public BluetoothAdapter getAdapter() {
        return mAdapter;
    }
}

package android.bluetooth;

/**
 * Bluetooth adapter API surface for a target without an Android Bluetooth
 * service.  These return values are the standard disabled/disconnected state;
 * getDefaultAdapter() returns null, as Android does on devices without an
 * adapter.
 */
public final class BluetoothAdapter {
    public static final int STATE_OFF = 10;
    public static final int STATE_ON = 12;
    public static final int SCAN_MODE_NONE = 20;
    public static final int STATE_DISCONNECTED = 0;

    private BluetoothAdapter() {}

    public static synchronized BluetoothAdapter getDefaultAdapter() {
        return null;
    }

    public boolean isEnabled() {
        return false;
    }

    public int getProfileConnectionState(int profile) {
        return STATE_DISCONNECTED;
    }

    public boolean enable() {
        return false;
    }

    public boolean disable() {
        return false;
    }

    public String getAddress() {
        return null;
    }

    public String getName() {
        return null;
    }

    public int getScanMode() {
        return SCAN_MODE_NONE;
    }

    public boolean isDiscovering() {
        return false;
    }
}

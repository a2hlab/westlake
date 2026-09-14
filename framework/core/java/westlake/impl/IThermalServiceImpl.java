package westlake.impl;

import android.os.Binder;
import android.os.CoolingDevice;
import android.os.IThermalEventListener;
import android.os.IThermalService;
import android.os.IThermalStatusListener;
import android.os.RemoteException;
import android.os.Temperature;

import java.util.Set;
import java.util.concurrent.CopyOnWriteArraySet;

/**
 * Android thermal-service boundary for an OH device without a wired thermal HAL adapter.
 *
 * The absence of sensor data is represented with Android's normal values: empty sensor arrays,
 * THROTTLING_NONE, and NaN headroom. Listener registration is still a real contract; clients such
 * as Chromium treat a false registration result as a framework failure. Keep listener identity so
 * unregister has the same success semantics as Android's RemoteCallbackList-backed service.
 */
public final class IThermalServiceImpl extends Binder implements IThermalService {
    private final Set<IThermalEventListener> eventListeners = new CopyOnWriteArraySet<>();
    private final Set<IThermalStatusListener> statusListeners = new CopyOnWriteArraySet<>();

    @Override
    public Binder asBinder() {
        return this;
    }

    @Override
    public Temperature[] getCurrentTemperatures() {
        return new Temperature[0];
    }

    @Override
    public Temperature[] getCurrentTemperaturesWithType(int type) {
        return new Temperature[0];
    }

    @Override
    public CoolingDevice[] getCurrentCoolingDevices() {
        return new CoolingDevice[0];
    }

    @Override
    public CoolingDevice[] getCurrentCoolingDevicesWithType(int type) {
        return new CoolingDevice[0];
    }

    @Override
    public int getCurrentThermalStatus() {
        return Temperature.THROTTLING_NONE;
    }

    @Override
    public float getThermalHeadroom(int forecastSeconds) {
        return Float.NaN;
    }

    @Override
    public boolean registerThermalEventListener(IThermalEventListener listener) {
        return listener != null && eventListeners.add(listener);
    }

    @Override
    public boolean registerThermalEventListenerWithType(
            IThermalEventListener listener, int type) {
        return registerThermalEventListener(listener);
    }

    @Override
    public boolean unregisterThermalEventListener(IThermalEventListener listener) {
        return listener != null && eventListeners.remove(listener);
    }

    @Override
    public boolean registerThermalStatusListener(IThermalStatusListener listener) {
        if (listener == null || !statusListeners.add(listener)) return false;
        try {
            listener.onStatusChange(Temperature.THROTTLING_NONE);
            return true;
        } catch (RemoteException e) {
            statusListeners.remove(listener);
            return false;
        }
    }

    @Override
    public boolean unregisterThermalStatusListener(IThermalStatusListener listener) {
        return listener != null && statusListeners.remove(listener);
    }

    @Override
    public android.os.IInterface queryLocalInterface(String descriptor) {
        return this;
    }

    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public float[] getThermalHeadroomThresholds() throws android.os.RemoteException {
        float[] thresholds = new float[Temperature.THROTTLING_SHUTDOWN + 1];
        java.util.Arrays.fill(thresholds, Float.NaN);
        return thresholds;
    }
}

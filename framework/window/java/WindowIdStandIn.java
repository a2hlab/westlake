package adapter.window;

import android.os.IBinder;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.util.Log;
import android.view.IWindowFocusObserver;
import android.view.IWindowId;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A window's id, as WMS gives one to each window it has added (WindowState.mWindowId, through
 * Session.getWindowId): one binder from the window's add to its remove, so the WindowIds of two
 * views in one window are equal (WindowId.equals compares the binders). isFocused answers with the
 * window focus last latched on the window here, which is how WMS's current focus reaches the app,
 * and registered focus observers hear each change, as WindowState reports them
 * (reportFocusChangedSerialized).
 *
 * getWindowId used to answer null for every window. androidx Transition compares the WindowIds of
 * the views it animates, and threw NullPointerException in an app past its licence dialog.
 */
final class WindowIdStandIn extends IWindowId.Stub {
    private static final String TAG = "OH_WSAdapter";
    private static final Map<IBinder, WindowIdStandIn> sIds = new HashMap<>();

    private final IBinder mWindow;
    private final RemoteCallbackList<IWindowFocusObserver> mObservers = new RemoteCallbackList<>();

    private WindowIdStandIn(IBinder window) {
        mWindow = window;
    }

    /** The added window's id, made when first asked for. */
    static IWindowId of(IBinder window) {
        synchronized (sIds) {
            WindowIdStandIn id = sIds.get(window);
            if (id == null) {
                id = new WindowIdStandIn(window);
                sIds.put(window, id);
                Log.d(TAG, "[STAND-IN] getWindowId - the window's id, held until the window is removed");
            }
            return id;
        }
    }

    /** The window is removed: its id goes, and its observers are dropped, as with its WindowState. */
    static void removed(IBinder window) {
        WindowIdStandIn id;
        synchronized (sIds) {
            id = sIds.remove(window);
        }
        if (id != null) id.mObservers.kill();
    }

    /** Window focus was latched on the ViewRootImpl: the observers of its window's id hear it. */
    static void focusChanged(Object viewRootImpl, boolean focused) {
        List<WindowIdStandIn> observed = new ArrayList<>();
        synchronized (sIds) {
            for (WindowIdStandIn id : sIds.values()) {
                if (id.mObservers.getRegisteredCallbackCount() > 0) observed.add(id);
            }
        }
        for (WindowIdStandIn id : observed) {
            Object root;
            try {
                root = WindowCallbackBridge.viewRootOf(id.mWindow);
            } catch (ReflectiveOperationException e) {
                continue;
            }
            if (root == viewRootImpl) id.report(focused);
        }
    }

    private void report(boolean focused) {
        synchronized (mObservers) {
            int count = mObservers.beginBroadcast();
            try {
                for (int i = 0; i < count; i++) {
                    IWindowFocusObserver observer = mObservers.getBroadcastItem(i);
                    try {
                        if (focused) {
                            observer.focusGained(asBinder());
                        } else {
                            observer.focusLost(asBinder());
                        }
                    } catch (RemoteException e) {
                        // The observer's process is gone; the list drops it.
                    }
                }
            } finally {
                mObservers.finishBroadcast();
            }
        }
    }

    @Override
    public void registerFocusObserver(IWindowFocusObserver observer) {
        if (observer != null) mObservers.register(observer);
    }

    @Override
    public void unregisterFocusObserver(IWindowFocusObserver observer) {
        if (observer != null) mObservers.unregister(observer);
    }

    @Override
    public boolean isFocused() {
        return WindowCallbackBridge.latchedFocus(mWindow);
    }
}

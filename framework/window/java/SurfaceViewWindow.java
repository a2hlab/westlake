package adapter.window;

import android.view.ViewRootImpl;

import java.lang.ref.WeakReference;

/**
 * The window a SurfaceView's own OH session is created for ({@link WindowSessionAdapter#attachSurfaceView}).
 * Android's SurfaceView is a layer of its activity's window, not a window: it takes neither focus nor
 * input. Its OH window is a window, and OH moves focus to it once it shows above the activity's. Its
 * session callbacks reach the window callback bridge with this object, which names the window the
 * SurfaceView belongs to, so the bridge can count that focus as the window's.
 */
final class SurfaceViewWindow extends android.os.Binder {
    private final WeakReference<ViewRootImpl> mOwner;

    SurfaceViewWindow(ViewRootImpl owner) {
        mOwner = new WeakReference<>(owner);
    }

    ViewRootImpl owner() {
        return mOwner.get();
    }
}

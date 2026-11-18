package adapter.window;

import android.graphics.Rect;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.IWindow;
import android.view.InsetsSource;
import android.view.InsetsState;
import android.view.View;
import android.view.ViewRootImpl;
import android.view.WindowInsets;
import android.view.WindowManagerGlobal;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.WeakHashMap;

/** OpenHarmony system insets in Android display coordinates, scoped to their window. */
public final class OhInsetsBridge {
    private static final WeakHashMap<IBinder, InsetsState> STATES = new WeakHashMap<>();
    private static final WeakHashMap<IBinder, Rect> OCCUPIED = new WeakHashMap<>();
    private static final WeakHashMap<IBinder, Boolean> RETIRED = new WeakHashMap<>();
    private static final int STATUS_BAR = InsetsSource.createId(null, 0, WindowInsets.Type.statusBars());
    private static volatile boolean statusBarQueryAvailable = true;
    private static native int[] nativeStatusBarFrame(int session);

    private OhInsetsBridge() {}

    // SceneBoard returns the avoid area in this window's local coordinates.
    public static boolean updateStatusBar(IWindow window, int session, int left, int top) {
        if (!statusBarQueryAvailable || session <= 0 || window == null) return false;
        int[] bounds;
        try {
            bounds = nativeStatusBarFrame(session);
        } catch (UnsatisfiedLinkError olderBridge) {
            statusBarQueryAvailable = false;
            return false;
        }
        if (bounds == null) return false;
        int[] size = WindowSessionAdapter.getDefaultDisplaySize();
        Rect display = new Rect(0, 0, size[0], size[1]);
        Rect bar = new Rect(bounds[0], bounds[1], bounds[2], bounds[3]);
        bar.offset(left, top);
        if (!bar.intersect(display)) bar.setEmpty();
        IBinder key = window.asBinder();
        synchronized (STATES) {
            if (RETIRED.containsKey(key)) return false;
            InsetsState state = STATES.get(key);
            if (state == null) {
                state = new InsetsState();
                STATES.put(key, state);
            }
            InsetsSource prior = state.peekSource(STATUS_BAR);
            if (prior != null && prior.getFrame().equals(bar)
                    && state.getDisplayFrame().equals(display)) return false;
            state.setDisplayFrame(display);
            state.getOrCreateSource(STATUS_BAR, WindowInsets.Type.statusBars())
                    .setFrame(bar).setVisible(!bar.isEmpty());
            return true;
        }
    }

    public static void systemAreaChanged(Object target, int type) {
        if (type != 0 || !(target instanceof IWindow)) return;
        IWindow window = (IWindow) target;
        new Handler(Looper.getMainLooper()).post(() -> {
            View view = WindowManagerGlobal.getInstance().getWindowView(window.asBinder());
            if (view == null || view.getViewRootImpl() == null) return;
            int session = InputEventBridge.getInstance().getSessionId(window.asBinder());
            int[] origin = new int[2];
            view.getLocationOnScreen(origin);
            if (updateStatusBar(window, session, origin[0], origin[1])) {
                InsetsState state = new InsetsState(view.getViewRootImpl().getInsetsController().getState(), true);
                synchronized (STATES) { copyStatusBar(window.asBinder(), state); }
                view.getViewRootImpl().getInsetsController().onStateChanged(state);
                view.requestApplyInsets();
            }
        });
    }

    private static void copyStatusBar(IBinder key, InsetsState destination) {
        InsetsState saved = STATES.get(key);
        InsetsSource bar = saved == null ? null : saved.peekSource(STATUS_BAR);
        if (bar != null) {
            destination.setDisplayFrame(saved.getDisplayFrame());
            destination.addSource(new InsetsSource(bar));
        }
    }

    public static void copyForWindow(IWindow window, InsetsState destination) {
        if (destination == null) return;
        synchronized (STATES) {
            InsetsState state = window == null ? null : STATES.get(window.asBinder());
            destination.set(state == null ? new InsetsState() : state, true);
        }
    }

    public static void remove(IWindow window) {
        if (window != null) synchronized (STATES) {
            STATES.remove(window.asBinder()); OCCUPIED.remove(window.asBinder());
            RETIRED.put(window.asBinder(), Boolean.TRUE);
        }
    }

    /** The legacy native callback supplies docked keyboard height in pixels. */
    public static void occupied(final Object target, final int height) {
        apply(target, height, null);
    }
    /** Physical OH rectangle; floating keyboards remain visible without resize. */
    public static void occupiedRect(Object target, int x, int y, int width, int height) {
        if (width < 0 || height < 0 || (long)x + width > Integer.MAX_VALUE
                || (long)y + height > Integer.MAX_VALUE) return;
        apply(target, height, new Rect(x,y,x+width,y+height));
    }
    public static Rect insetFrame(Rect display, Rect occupied) {
        Rect clipped = new Rect(occupied);
        if (!clipped.intersect(display)) return new Rect();
        // AOSP treats any nonempty IME intersection as bottom insets. Floating
        // panels must therefore have an empty source frame, while visibility
        // and the actual occupied rectangle remain independently represented.
        return clipped.bottom == display.bottom && clipped.left <= display.left
                && clipped.right >= display.right ? clipped : new Rect();
    }
    private static void apply(final Object target, final int height, final Rect occupied) {
        if (!(target instanceof IWindow) || height < 0) return;
        final IWindow window = (IWindow) target;
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override public void run() {
                try {
                    synchronized (STATES) { if (RETIRED.containsKey(window.asBinder())) return; }
                    Class<?> globalClass = Class.forName("android.view.WindowManagerGlobal");
                    Object global = globalClass.getMethod("getInstance").invoke(null);
                    Field rootsField = globalClass.getDeclaredField("mRoots");
                    rootsField.setAccessible(true);
                    ArrayList<?> roots = (ArrayList<?>) rootsField.get(global);
                    Field windowField = ViewRootImpl.class.getDeclaredField("mWindow");
                    windowField.setAccessible(true);
                    // The keyboard serves the window that has input focus (Android's IME
                    // target), whichever window's session reported the occupied area.
                    ViewRootImpl reported = null, focused = null;
                    for (Object item : roots.toArray()) {
                        ViewRootImpl root = (ViewRootImpl) item;
                        IWindow candidate = (IWindow) windowField.get(root);
                        if (candidate.asBinder().equals(window.asBinder())) reported = root;
                        if (root.getView() != null && root.getView().hasWindowFocus()) focused = root;
                    }
                    ViewRootImpl imeTarget = focused != null ? focused : reported;
                    if (imeTarget != null) {
                        ViewRootImpl root = imeTarget;
                        IWindow targetWindow = (IWindow) windowField.get(root);
                        View view = root.getView();
                        if (view == null) return;
                        // The physical display, not view.getDisplay(): for a dialog window that
                        // reports the window's own size here, and the keyboard never intersects it.
                        int[] size = WindowSessionAdapter.getDefaultDisplaySize();
                        int h = Math.min(height, size[1]);
                        Rect display = new Rect(0, 0, size[0], size[1]);
                        Rect area = occupied == null ? new Rect(0, size[1] - h, size[0], size[1])
                                : new Rect(occupied);
                        Rect frame = insetFrame(display, area);
                        InsetsState state = new InsetsState(root.getInsetsController().getState(), true);
                        state.setDisplayFrame(display);
                        state.getOrCreateSource(InsetsSource.ID_IME, WindowInsets.Type.ime())
                                .setFrame(frame).setVisible(!area.isEmpty());
                        synchronized (STATES) {
                            copyStatusBar(targetWindow.asBinder(), state);
                            STATES.put(targetWindow.asBinder(), new InsetsState(state, true));
                            OCCUPIED.put(targetWindow.asBinder(), area);
                        }
                        // On the UI thread; retain all non-IME source controls.
                        // Relayout uses the same stored state, not an empty one.
                        root.getInsetsController().onStateChanged(state);
                        root.requestFitSystemWindows();
                        root.requestLayout();
                        System.err.println("[OH_INSETS] occupied=" + area + " frame=" + frame + " display=" + display);
                        return;
                    }
                } catch (ReflectiveOperationException | RuntimeException failure) {
                    System.err.println("[OH_INSETS] update failed: " + failure);
                }
            }
        });
    }
}

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
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.WeakHashMap;

/** Server-owned IME insets, scoped to the window receiving the OH callback. */
public final class OhInsetsBridge {
    private static final WeakHashMap<IBinder, InsetsState> STATES = new WeakHashMap<>();
    private static final WeakHashMap<IBinder, Rect> OCCUPIED = new WeakHashMap<>();
    private static final WeakHashMap<IBinder, Boolean> RETIRED = new WeakHashMap<>();
    private OhInsetsBridge() {}

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

    // OH avoid area types (wm_common.h AvoidAreaType) that are Android system bars.
    private static final int OH_AVOID_SYSTEM = 0;
    private static final int OH_AVOID_NAVIGATION_INDICATOR = 4;

    /**
     * OH's system bar areas for a window it manages (ADR-0008): SceneBoard reports the status bar as the
     * top of TYPE_SYSTEM and a navigation bar or gesture indicator at the bottom of TYPE_SYSTEM or of
     * TYPE_NAVIGATION_INDICATOR. They become Android's status bar and navigation bar insets sources, as
     * the window manager's system bars are on Android; an empty area hides the source.
     */
    public static void systemBars(final Object target, final int type, final Rect top, final Rect bottom) {
        if (!(target instanceof IWindow)) return;
        if (type != OH_AVOID_SYSTEM && type != OH_AVOID_NAVIGATION_INDICATOR) return;
        final IWindow window = (IWindow) target;
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override public void run() {
                try {
                    synchronized (STATES) { if (RETIRED.containsKey(window.asBinder())) return; }
                    ViewRootImpl root = rootOf(window);
                    if (root == null || root.getView() == null) return;
                    int[] size = WindowSessionAdapter.getDefaultDisplaySize();
                    Rect display = new Rect(0, 0, size[0], size[1]);
                    InsetsState state = new InsetsState(root.getInsetsController().getState(), true);
                    state.setDisplayFrame(display);
                    if (type == OH_AVOID_SYSTEM) {
                        source(state, WindowInsets.Type.statusBars(), top);
                    }
                    if (type == OH_AVOID_NAVIGATION_INDICATOR || !bottom.isEmpty()) {
                        source(state, WindowInsets.Type.navigationBars(), bottom);
                    }
                    synchronized (STATES) { STATES.put(window.asBinder(), new InsetsState(state, true)); }
                    root.getInsetsController().onStateChanged(state);
                    root.requestFitSystemWindows();
                    root.requestLayout();
                    System.err.println("[OH_INSETS] system bars type=" + type + " top=" + top + " bottom=" + bottom);
                } catch (ReflectiveOperationException | RuntimeException failure) {
                    System.err.println("[OH_INSETS] system bars failed: " + failure);
                }
            }
        });
    }

    private static void source(InsetsState state, int insetsType, Rect frame) {
        state.getOrCreateSource(InsetsSource.createId(null, 0, insetsType), insetsType)
                .setFrame(frame).setVisible(!frame.isEmpty());
    }

    private static ViewRootImpl rootOf(IWindow window) throws ReflectiveOperationException {
        Class<?> globalClass = Class.forName("android.view.WindowManagerGlobal");
        Object global = globalClass.getMethod("getInstance").invoke(null);
        Field rootsField = globalClass.getDeclaredField("mRoots");
        rootsField.setAccessible(true);
        Field windowField = ViewRootImpl.class.getDeclaredField("mWindow");
        windowField.setAccessible(true);
        for (Object item : ((ArrayList<?>) rootsField.get(global)).toArray()) {
            ViewRootImpl root = (ViewRootImpl) item;
            if (((IWindow) windowField.get(root)).asBinder().equals(window.asBinder())) return root;
        }
        return null;
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

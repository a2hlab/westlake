package adapter.window;

import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.WindowManagerGlobal;

import java.lang.reflect.Field;
import java.util.List;

/**
 * The layout geometry of the app's windows, for inset and placement bugs.
 *
 * Unlike the view dump ("v", in the native input bridge) and {@link AccessibilityTreeDump}, it
 * writes no text, content description or other view content, so it can be used while
 * WESTLAKE_GOOGLE_PRIVATE_LOGS keeps the view dump off. Per window: the root's window flags, system
 * UI visibility, insets, whether the window's decor fits system windows and whether a content
 * insets applier is installed. Per view: class, resource id name, position on screen, size,
 * margins, padding, visibility and its fitsSystemWindows flags.
 *
 * The request comes as a tap at or beyond {@link #REQUEST_AT} on both axes, which no display has:
 * the tap channel's "i X Y" passes it to OHTouchInjector, which hands it here instead of
 * dispatching it. The dump runs on the main thread, as layout does, and writes "GEOM" lines.
 */
final class ViewGeometryDump {
    /** A tap at or beyond this on both axes asks for the dump (beyond the accessibility dump's). */
    static final float REQUEST_AT = 2_000_000f;
    private static final int MAX_VIEWS = 600;
    private static final int MAX_DEPTH = 28;
    private static final int OPTIONAL_FITS_SYSTEM_WINDOWS = 0x00000800;  // View.mViewFlags

    private ViewGeometryDump() {}

    static boolean isRequest(MotionEvent event) {
        return event.getX() >= REQUEST_AT && event.getY() >= REQUEST_AT;
    }

    /** Runs the dump on the main thread; the request's DOWN asks, its UP is dropped by the caller. */
    static void post() {
        Handler.createAsync(Looper.getMainLooper()).post(ViewGeometryDump::dump);
    }

    static void dump() {
        StringBuilder out = new StringBuilder();
        try {
            List<View> roots = WindowManagerGlobal.getInstance().getWindowViews();
            int[] budget = {0};
            for (int i = 0; i < roots.size(); i++) {
                View root = roots.get(i);
                if (root == null) continue;
                out.append("GEOM ==== root[").append(i).append("] ").append(root.getClass().getSimpleName())
                        .append(" attached=").append(root.isAttachedToWindow() ? 1 : 0)
                        .append(" vis=").append(visibility(root))
                        .append(" focus=").append(root.hasWindowFocus() ? 1 : 0)
                        .append(" sysui=0x").append(Integer.toHexString(root.getWindowSystemUiVisibility()))
                        .append(" decorFits=").append(decorFits(root))
                        .append(" contentApplier=").append(contentApplier(root));
                if (root.getLayoutParams() instanceof WindowManager.LayoutParams) {
                    WindowManager.LayoutParams lp = (WindowManager.LayoutParams) root.getLayoutParams();
                    out.append(" type=").append(lp.type)
                            .append(" flags=0x").append(Integer.toHexString(lp.flags))
                            .append(" fitTypes=0x").append(Integer.toHexString(lp.getFitInsetsTypes()))
                            .append(" cutout=").append(lp.layoutInDisplayCutoutMode)
                            .append(" title=").append(String.valueOf(lp.getTitle()).replace(' ', '_'));
                }
                out.append(" ====\n");
                WindowInsets insets = root.getRootWindowInsets();
                if (insets != null) {
                    out.append("GEOM insets system=[").append(insets.getSystemWindowInsetLeft()).append(',')
                            .append(insets.getSystemWindowInsetTop()).append(',')
                            .append(insets.getSystemWindowInsetRight()).append(',')
                            .append(insets.getSystemWindowInsetBottom())
                            .append("] stable=[").append(insets.getStableInsetLeft()).append(',')
                            .append(insets.getStableInsetTop()).append(',')
                            .append(insets.getStableInsetRight()).append(',')
                            .append(insets.getStableInsetBottom())
                            .append("] statusBars=").append(insets.getInsets(WindowInsets.Type.statusBars()))
                            .append(" navigationBars=").append(insets.getInsets(WindowInsets.Type.navigationBars()))
                            .append('\n');
                }
                walk(root, 0, out, budget);
            }
        } catch (Throwable throwable) {
            out.append("GEOM dump failed: ").append(throwable.getClass().getName()).append('\n');
        }
        out.append("GEOM ==== end ====\n");
        System.err.print(out);
        System.err.flush();
    }

    private static void walk(View view, int depth, StringBuilder out, int[] budget) {
        if (view == null || depth > MAX_DEPTH || budget[0]++ >= MAX_VIEWS) return;
        int[] at = new int[2];
        view.getLocationOnScreen(at);
        out.append("GEOM ");
        for (int i = 0; i < Math.min(depth, 20); i++) out.append(' ');
        out.append(view.getClass().getSimpleName().isEmpty() ? view.getClass().getName()
                        : view.getClass().getSimpleName())
                .append(" id=").append(idName(view))
                .append(" xy=").append(at[0]).append(',').append(at[1])
                .append(" wh=").append(view.getWidth()).append('x').append(view.getHeight())
                .append(" pad=").append(view.getPaddingLeft()).append(',').append(view.getPaddingTop())
                .append(',').append(view.getPaddingRight()).append(',').append(view.getPaddingBottom());
        if (view.getLayoutParams() instanceof ViewGroup.MarginLayoutParams) {
            ViewGroup.MarginLayoutParams m = (ViewGroup.MarginLayoutParams) view.getLayoutParams();
            if ((m.leftMargin | m.topMargin | m.rightMargin | m.bottomMargin) != 0) {
                out.append(" margin=").append(m.leftMargin).append(',').append(m.topMargin).append(',')
                        .append(m.rightMargin).append(',').append(m.bottomMargin);
            }
        }
        out.append(" vis=").append(visibility(view))
                .append(" fits=").append(view.getFitsSystemWindows() ? 1 : 0)
                .append(optionalFits(view) ? " optionalFits=1" : "");
        if (view instanceof ViewGroup) out.append(" children=").append(((ViewGroup) view).getChildCount());
        out.append('\n');
        if (view.getVisibility() != View.VISIBLE || !(view instanceof ViewGroup)) return;
        ViewGroup group = (ViewGroup) view;
        for (int c = 0; c < group.getChildCount(); c++) walk(group.getChildAt(c), depth + 1, out, budget);
    }

    private static String idName(View view) {
        int id = view.getId();
        if (id == View.NO_ID) return "-";
        try {
            return view.getResources().getResourceEntryName(id);
        } catch (RuntimeException unnamed) {
            return "0x" + Integer.toHexString(id);
        }
    }

    private static char visibility(View view) {
        int v = view.getVisibility();
        return v == View.VISIBLE ? 'V' : v == View.INVISIBLE ? 'I' : 'G';
    }

    private static boolean optionalFits(View view) {
        try {
            Field flags = View.class.getDeclaredField("mViewFlags");
            flags.setAccessible(true);
            return (flags.getInt(view) & OPTIONAL_FITS_SYSTEM_WINDOWS) != 0;
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return false;
        }
    }

    /** Whether the window's decor fits system windows (PhoneWindow), "?" when unknown. */
    private static String decorFits(View root) {
        try {
            Field window = root.getClass().getDeclaredField("mWindow");
            window.setAccessible(true);
            Object phoneWindow = window.get(root);
            if (phoneWindow == null) return "?";
            Field fits = phoneWindow.getClass().getDeclaredField("mDecorFitsSystemWindows");
            fits.setAccessible(true);
            return fits.getBoolean(phoneWindow) ? "1" : "0";
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return "?";
        }
    }

    /** Whether the view root installed the decor's content insets applier, "?" when unknown. */
    private static String contentApplier(View root) {
        try {
            Field attachInfo = View.class.getDeclaredField("mAttachInfo");
            attachInfo.setAccessible(true);
            Object info = attachInfo.get(root);
            if (info == null) return "?";
            Field listener = info.getClass().getDeclaredField("mContentOnApplyWindowInsetsListener");
            listener.setAccessible(true);
            return listener.get(info) != null ? "1" : "0";
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return "?";
        }
    }
}

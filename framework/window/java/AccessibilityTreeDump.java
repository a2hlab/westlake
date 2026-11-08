package adapter.window;

import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManagerGlobal;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.webkit.WebView;

import java.util.List;

/**
 * The accessibility nodes of the app's windows, written as the view dump's VT lines.
 *
 * The tap channel's view dump ("v", in the native input bridge) walks Android views only. A screen
 * drawn with Jetpack Compose is one AndroidComposeView there with no clickable child: its buttons
 * are semantics nodes, which reach accessibility through the view's AccessibilityNodeProvider, and
 * that is how uiautomator reads them on Android. The Level 2 explorer saw 9 views and nothing to tap
 * on Dropbox's welcome screen, whose "Sign up" and "Log in" are such nodes.
 *
 * The native dump cannot be extended (its library cannot be rebuilt today), so the request comes as
 * a tap: one at or beyond {@link #REQUEST_AT} on both axes, which no display has. The tap channel
 * passes it to OHTouchInjector, which hands it here instead of dispatching it. The dump runs on the
 * main thread, as accessibility queries do on Android, and writes one block per window that has a
 * node provider ("VT ==== root[a&lt;i&gt;] ====", then the window, then its nodes in the window's
 * coordinates, as the native dump gives them), and "VT ==== a11y end ====" last.
 */
final class AccessibilityTreeDump {
    /** A tap at or beyond this on both axes asks for the dump. */
    static final float REQUEST_AT = 1_000_000f;
    private static final int MAX_NODES = 600;
    private static final int MAX_DEPTH = 40;

    private AccessibilityTreeDump() {}

    static boolean isRequest(MotionEvent event) {
        return event.getX() >= REQUEST_AT && event.getY() >= REQUEST_AT;
    }

    /** Runs the dump on the main thread; the request's DOWN asks, its UP is dropped by the caller. */
    static void post() {
        Handler.createAsync(Looper.getMainLooper()).post(AccessibilityTreeDump::dump);
    }

    static void dump() {
        StringBuilder out = new StringBuilder();
        try {
            List<View> roots = WindowManagerGlobal.getInstance().getWindowViews();
            int[] budget = {0};
            for (int i = 0; i < roots.size(); i++) {
                View root = roots.get(i);
                if (root == null || !root.isAttachedToWindow() || root.getVisibility() != View.VISIBLE) continue;
                int[] origin = new int[2];
                root.getLocationOnScreen(origin);
                StringBuilder nodes = new StringBuilder();
                if (walk(root, origin, nodes, budget) == 0) continue;
                out.append("VT ==== root[a").append(i).append("] ====\n");
                line(out, 0, root.getClass().getSimpleName(), "-", 0, 0, root.getWidth(), root.getHeight(),
                        false, true, null);
                out.append(nodes);
            }
        } catch (Throwable throwable) {
            out.append("VT a11y dump failed: ").append(throwable).append('\n');
        }
        out.append("VT ==== a11y end ====\n");
        System.err.print(out);
        System.err.flush();
    }

    /** Nodes written for view's subtree: those of the first node provider on each path down. */
    private static int walk(View view, int[] origin, StringBuilder out, int[] budget) {
        if (view == null || view.getVisibility() != View.VISIBLE) return 0;
        // A WebView's provider turns on the renderer's accessibility tree, a cost the page would not
        // otherwise pay; its content is not this dump's to read.
        if (!(view instanceof WebView)) {
            AccessibilityNodeProvider provider = view.getAccessibilityNodeProvider();
            if (provider != null) {
                return nodes(provider, AccessibilityNodeProvider.HOST_VIEW_ID, origin, out, 1, budget);
            }
        }
        int written = 0;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int c = 0; c < group.getChildCount(); c++) {
                written += walk(group.getChildAt(c), origin, out, budget);
            }
        }
        return written;
    }

    private static int nodes(AccessibilityNodeProvider provider, int id, int[] origin, StringBuilder out,
                             int depth, int[] budget) {
        if (depth > MAX_DEPTH || budget[0] >= MAX_NODES) return 0;
        AccessibilityNodeInfo info;
        try {
            info = provider.createAccessibilityNodeInfo(id);
        } catch (Throwable throwable) {
            return 0;
        }
        if (info == null) return 0;
        budget[0]++;
        int written = 0;
        Rect bounds = new Rect();
        info.getBoundsInScreen(bounds);
        if (info.isVisibleToUser() && bounds.width() > 0 && bounds.height() > 0) {
            CharSequence text = info.getText() != null ? info.getText() : info.getContentDescription();
            CharSequence cls = info.getClassName();
            String name = cls == null ? "Node" : cls.toString().substring(cls.toString().lastIndexOf('.') + 1);
            String vid = info.getViewIdResourceName();
            if (vid != null) vid = vid.substring(vid.lastIndexOf('/') + 1);
            line(out, depth, name.isEmpty() ? "Node" : name, vid == null || vid.isEmpty() ? "-" : vid,
                    bounds.left - origin[0], bounds.top - origin[1], bounds.width(), bounds.height(),
                    info.isClickable(), info.isEnabled(), text);
            written++;
        }
        for (int c = 0; c < info.getChildCount(); c++) {
            written += nodes(provider, AccessibilityNodeInfo.getVirtualDescendantId(info.getChildId(c)),
                    origin, out, depth + 1, budget);
        }
        return written;
    }

    /** One VT line, in the native dump's format (oh_input_bridge.cpp wl_dump_view). */
    private static void line(StringBuilder out, int depth, String cls, String vid, int x, int y, int w, int h,
                             boolean clickable, boolean enabled, CharSequence text) {
        out.append("VT ");
        for (int i = 0; i < Math.min(depth, 20); i++) out.append(' ');
        out.append(cls.replace(' ', '_')).append(" id=").append(vid.replace(' ', '_'))
                .append(" rect=[").append(x).append(',').append(y).append(' ').append(w).append('x').append(h)
                .append("] c=").append(clickable ? 1 : 0).append(" en=").append(enabled ? 1 : 0)
                .append(" f=0 ftm=0 itm=1 ");
        if (text != null && text.length() > 0) {
            String s = text.toString().replace('\n', ' ').replace('"', '\'');
            out.append('"').append(s.length() > 120 ? s.substring(0, 120) : s).append('"');
        }
        out.append('\n');
    }
}

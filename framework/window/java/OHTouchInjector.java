package adapter.window;

import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.Editable;
import android.text.Selection;
import android.widget.TextView;

/**
 * Posts OH-boundary touch injection onto the Android view hierarchy's owning thread.
 *
 * The native input bridge resolves this boot-class-path helper before considering any
 * app-specific fallback.  Keeping dispatch on View.post() preserves Android's threading contract
 * for RecyclerView gesture state, popup dismissal, focus changes, and click listeners.
 */
public final class OHTouchInjector {
    // Native diagnostic contract: oh_input_bridge.cpp reads these fields after dispatch.
    public static int invokeCount = 0;
    public static int runCount = 0;
    public static int lastHandled = -1;
    public static String lastEx = "none";
    public static int textCommitCount = 0;
    public static String lastTextCommit = "none";

    // Legacy pacing is opt-in only. Android View handles an UP while PREPRESSED;
    // delaying every release adds latency and can reorder UP after the next DOWN.
    private static final boolean LEGACY_TAP_PACING =
            "1".equals(System.getenv("WL_TOUCH_LEGACY_TAP_PACING"));
    private static volatile long lastDownDispatchUptime;
    private static final boolean FORCE_PERFORM_CLICK =
            "1".equals(System.getenv("WL_TOUCH_FORCE_CLICK"));

    private OHTouchInjector() {}

    public static void dispatchTouchOnMain(View view, MotionEvent event) {
        invokeCount++;
        if (view == null || event == null) {
            lastEx = "null arg";
            return;
        }
        Task task = new Task(view, event);
        // WESTLAKE §698: surface-only windows can be laid out and rendered while the
        // DecorView's attach-info run queue is never drained.  View.post() then returns true but
        // the task never runs (Toutiao: invokeCount=18, runCount=0).  Post to Android's actual
        // main Looper explicitly; this is still the stock UI-thread dispatch contract and avoids
        // executing click/lifecycle code on the OH input worker.  Retain View.post() as the
        // fallback for the unlikely pre-main-looper case.
        try {
            Looper mainLooper = Looper.getMainLooper();
            // WESTLAKE §786: Android's real InputEventReceiver is driven by a Looper fd
            // callback, so Choreographer's synchronous-message barrier does not starve input.
            // The OH boundary substitutes a posted Runnable; make that message asynchronous to
            // preserve the same barrier-independent input property.  A normal Handler was
            // measured at invokeCount=2/runCount=0 for 35 seconds while rendering continued.
            if (mainLooper != null && Handler.createAsync(mainLooper).post(task)) {
                return;
            }
        } catch (Throwable throwable) {
            lastEx = "main-post:" + String.valueOf(throwable);
        }
        if (!view.post(task)) {
            task.run();
        }
    }

    /**
     * Commits text into the currently focused TextView on Android's main thread.
     *
     * The OH board has no attached IME and its synthetic virtual keyboard has no
     * resolvable KeyCharacterMap, so per-key KeyEvents carry unicode 0.  This is
     * the boundary equivalent of InputConnection.commitText(): it targets only
     * the focused editable view and preserves the normal UI-thread contract.
     */
    public static void commitFocusedTextOnMain(View root, String text) {
        if (root == null || text == null) {
            lastTextCommit = "null arg";
            return;
        }
        TextTask task = new TextTask(root, text);
        try {
            Looper mainLooper = Looper.getMainLooper();
            if (mainLooper != null && Handler.createAsync(mainLooper).post(task)) {
                return;
            }
        } catch (Throwable throwable) {
            lastTextCommit = "main-post:" + String.valueOf(throwable);
        }
        if (!root.post(task)) {
            task.run();
        }
    }

    static final class TextTask implements Runnable {
        private final View root;
        private final String text;

        TextTask(View root, String text) {
            this.root = root;
            this.text = text;
        }

        @Override
        public void run() {
            try {
                View focused = root.findFocus();
                if (!(focused instanceof TextView)) {
                    lastTextCommit = "focused="
                            + (focused == null ? "null" : focused.getClass().getName());
                    return;
                }
                TextView target = (TextView) focused;
                Editable editable = target.getEditableText();
                String committed;
                if (editable != null) {
                    int start = Selection.getSelectionStart(editable);
                    int end = Selection.getSelectionEnd(editable);
                    if (start < 0 || end < 0) {
                        start = end = editable.length();
                    } else if (start > end) {
                        int swap = start;
                        start = end;
                        end = swap;
                    }
                    editable.replace(start, end, text);
                    Selection.setSelection(editable, start + text.length());
                    committed = editable.toString();
                } else {
                    CharSequence old = target.getText();
                    committed = (old == null ? "" : old.toString()) + text;
                    target.setText(committed);
                }
                textCommitCount++;
                lastTextCommit = "ok:" + target.getClass().getName() + ":" + committed;
            } catch (Throwable throwable) {
                lastTextCommit = String.valueOf(throwable);
            }
        }
    }

    static final class Task implements Runnable {
        private final View view;
        private final MotionEvent event;

        Task(View view, MotionEvent event) {
            this.view = view;
            this.event = event;
        }

        @Override
        public void run() {
            final int action = event.getActionMasked();
            if (LEGACY_TAP_PACING && action == MotionEvent.ACTION_UP) {
                final long downUptime = lastDownDispatchUptime;
                if (downUptime != 0) {
                final long minimum = ViewConfiguration.getTapTimeout() + 20L;
                final long remaining = minimum - (SystemClock.uptimeMillis() - downUptime);
                if (remaining > 0) {
                    // Keep the paced UP asynchronous too.  Re-posting through View converted it
                    // back to a synchronous Handler message: measured invokeCount=6/runCount=2,
                    // with DOWN delivered but every UP stranded behind the same barrier.
                    Looper mainLooper = Looper.getMainLooper();
                    if (mainLooper != null
                            && Handler.createAsync(mainLooper).postDelayed(this, remaining)) {
                        return;
                    }
                    if (view.postDelayed(this, remaining)) {
                        return;
                    }
                }
            }
            }
            runCount++;
            try {
                lastHandled = view.dispatchTouchEvent(event) ? 1 : 0;
                lastEx = "none";
                if (action == MotionEvent.ACTION_DOWN) {
                    lastDownDispatchUptime = SystemClock.uptimeMillis();
                } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    lastDownDispatchUptime = 0;
                }
                if (action == MotionEvent.ACTION_UP && FORCE_PERFORM_CLICK) {
                    forceClickAt(view, event.getX(), event.getY());
                }
            } catch (Throwable throwable) {
                lastHandled = -1;
                lastEx = String.valueOf(throwable);
            }
        }
    }

    private static boolean forceClickAt(View view, float x, float y) {
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = group.getChildCount() - 1; index >= 0; --index) {
                View child = group.getChildAt(index);
                if (child == null || child.getVisibility() != View.VISIBLE) continue;
                float childX = x + group.getScrollX() - child.getX();
                float childY = y + group.getScrollY() - child.getY();
                if (childX >= 0 && childY >= 0
                        && childX < child.getWidth() && childY < child.getHeight()
                        && forceClickAt(child, childX, childY)) {
                    return true;
                }
            }
        }
        if (view.isClickable()) {
            boolean clicked = view.performClick();
            lastEx = "force-click:" + view.getClass().getName() + ":" + clicked;
            return clicked;
        }
        return false;
    }
}

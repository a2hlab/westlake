package adapter.window;

import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.Selection;
import android.view.View;
import android.widget.TextView;

/**
 * Generic text-commit helper loaded by the OH input bridge.
 *
 * This lives in a small standalone dex during bring-up so changing input-boundary
 * behavior does not require regenerating the platform boot image.  The bridge passes
 * only the focused Android window root and text; all view mutation stays on Android's
 * main looper and targets the currently focused editable widget.
 */
public final class OHTextInjector {
    public static int commitCount;
    public static String lastCommit = "none";

    private OHTextInjector() {}

    public static void commitFocusedTextOnMain(View root, String text) {
        if (root == null || text == null) {
            lastCommit = "null arg";
            return;
        }
        Task task = new Task(root, text);
        try {
            Looper mainLooper = Looper.getMainLooper();
            if (mainLooper != null && Handler.createAsync(mainLooper).post(task)) {
                return;
            }
        } catch (Throwable throwable) {
            lastCommit = "main-post:" + String.valueOf(throwable);
        }
        if (!root.post(task)) {
            task.run();
        }
    }

    private static final class Task implements Runnable {
        private final View root;
        private final String text;

        Task(View root, String text) {
            this.root = root;
            this.text = text;
        }

        @Override
        public void run() {
            try {
                View focused = root.findFocus();
                if (!(focused instanceof TextView)) {
                    lastCommit = "focused="
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
                commitCount++;
                lastCommit = "ok:" + target.getClass().getName() + ":" + committed;
            } catch (Throwable throwable) {
                lastCommit = String.valueOf(throwable);
            }
        }
    }
}

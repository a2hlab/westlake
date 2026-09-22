/*
 * OhImeBridge.java
 *
 * Android IMM <-> OHOS inputMethod bridge (Java half).
 *
 * Companion to liboh_adapter_bridge.so's input_method_bridge.cpp. The native
 * side Attaches an OHOS OnTextChangedListener to InputMethodController and
 * shows/hides the soft keyboard; this class:
 *   - exposes show()/hide() that call the native entry points
 *     (nativeShowKeyboard/nativeHideKeyboard, registered by the bridge on THIS
 *     class via RegisterNatives), and
 *   - exposes nativeOn* statics the native listener calls back to, each of
 *     which performs one InputConnection edit on the Android UI thread against
 *     the currently-focused EditText (commitText / deleteSurroundingText /
 *     ENTER key). commitText mutates the live Editable so the text appears.
 *
 * This is a standalone (non-BCP-coupled) class so it compiles against the
 * public android.jar; ViewRootImpl / WindowManagerGlobal (hidden) are reached
 * via reflection. adapter.window.InputMethodManagerAdapter (the registered
 * "input_method" stub) just forwards showSoftInput/hideSoftInput/
 * startInputOrWindowGainedFocus here.
 */
package adapter.window;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.SurroundingText;
import android.icu.text.BreakIterator;

import java.lang.reflect.Field;
import java.util.ArrayList;

public final class OhImeBridge {

    private static final String TAG = "OH_IMEBridge";

    private OhImeBridge() {}

    // ---- native entry points (registered by liboh_adapter_bridge.so) ----
    private static native boolean nativeShowKeyboard();
    private static native boolean nativeHideKeyboard();
    private static native boolean nativeShowKeyboardCapi(long generation, int windowId);
    private static native boolean nativeHideKeyboardCapi();
    private static native void nativeNotifySelectionCapi(String text, int start, int end);

    // Loaded only on first input in the forked app. Do not initialize OHOS IPC
    // clients in the persistent ART parent. The two legacy native declarations
    // above remain for compatibility with older bridge registration tables.
    private static final class NativeLoader {
        static {
            String path = System.getenv("WESTLAKE_IME_HELPER_PATH");
            if (path == null || path.isEmpty()) System.loadLibrary("oh_ime_helper_capi");
            else System.load(path);
        }
        static void ensureLoaded() {}
    }

    private static volatile int sInputType = 1;
    private static volatile int sImeOptions;
    private static volatile String sBefore = "";
    private static volatile String sAfter = "";
    private static volatile int sCursor;
    private static volatile int sSelectionEnd;
    private static volatile Session sSession;
    private static View sCreatingView; // UI-thread only
    private static long sNextGeneration;

    private static final class Session {
        final View view;
        final InputConnection connection;
        final long generation;
        Session(View view, InputConnection connection) {
            this.view = view;
            this.connection = connection;
            this.generation = ++sNextGeneration;
        }
        boolean current() {
            return sSession == this && view.isShown() && view.hasWindowFocus() && view.isFocused();
        }
    }

    public static void setEditorInfo(EditorInfo info) {
        if (info != null) {
            sInputType = info.inputType;
            sImeOptions = info.imeOptions;
            sCursor = Math.max(0, info.initialSelStart);
        }
    }

    public static int nativeGetInputType() { return sInputType; }
    public static int nativeGetImeOptions() { return sImeOptions; }
    public static int nativeGetCursor() { return sCursor; }
    public static int nativeGetSelectionEnd() { return sSelectionEnd; }
    public static String nativeGetBefore() { return sBefore; }
    public static String nativeGetAfter() { return sAfter; }
    public static long nativeGetGeneration() { Session s = sSession; return s == null ? 0 : s.generation; }
    public static int visibleHeight() {
        Session s = sSession;
        android.view.WindowInsets insets = s == null ? null : s.view.getRootWindowInsets();
        return insets == null ? 0 : insets.getInsets(android.view.WindowInsets.Type.ime()).bottom;
    }
    public static boolean requestVisibility(final android.os.IBinder token, final boolean visible,
            final android.os.ResultReceiver receiver) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            return uiHandler().post(new Runnable() { @Override public void run() {
                requestVisibility(token, visible, receiver);
            }});
        }
        Session s = sSession;
        View view = s != null && s.current() ? s.view : null;
        if (view == null) { focusedInputConnection(); view=sCreatingView; }
        if (token == null || view == null || !token.equals(view.getWindowToken())) return false;
        boolean wasVisible = visibleHeight() > 0;
        boolean result = visible ? show() : hide();
        // Report only after the native operation, not when it was queued.
        if (receiver != null) receiver.send(result ? (visible ? (wasVisible ? 0 : 2) : (wasVisible ? 3 : 1))
                : (wasVisible ? 0 : 1), null);
        return result;
    }
    public static void refresh(final long generation) {
        uiHandler().post(new Runnable() { @Override public void run() {
            Session session = sSession;
            if (session != null && session.generation == generation && session.current())
                updateSnapshot(session.connection);
        }});
    }

    /**
     * The keyboard's area over the edited window, reported by the native helper once the panel is
     * shown (an empty rectangle once it is gone). Android resizes or pans the focused window for
     * the keyboard from IME insets, so it is published as that window's IME insets source.
     */
    public static void nativeOnKeyboardArea(int windowId, int x, int y, int width, int height) {
        try {
            for (View root : android.view.WindowManagerGlobal.getInstance().getWindowViews()) {
                if (root == null || root.getWindowToken() == null) continue;
                if (InputEventBridge.getInstance().getSessionId(root.getWindowToken()) != windowId) continue;
                OhInsetsBridge.occupiedRect(android.view.IWindow.Stub.asInterface(root.getWindowToken()),
                        x, y, width, height);
                return;
            }
        } catch (Throwable t) {
            System.err.println("[OH_IME] keyboard area not applied: " + t);
        }
    }

    /** Resolve the focused Android window's own OH session, never the last-created window. */
    private static int windowId(View view) throws ReflectiveOperationException {
        Object root = View.class.getMethod("getViewRootImpl").invoke(view);
        if (root == null) return 0;
        Field sessionField = root.getClass().getDeclaredField("mWindowSession");
        Field windowField = root.getClass().getDeclaredField("mWindow");
        sessionField.setAccessible(true); windowField.setAccessible(true);
        Object windowSession = sessionField.get(root);
        Field sessions = windowSession.getClass().getDeclaredField("mSessionMap");
        sessions.setAccessible(true);
        android.view.IWindow window = (android.view.IWindow) windowField.get(root);
        int[] data = (int[]) ((java.util.Map<?, ?>) sessions.get(windowSession)).get(window.asBinder());
        return data == null || data.length == 0 ? 0 : data[0];
    }

    /** Summon the OHOS soft keyboard. Called from the IMM adapter. */
    public static boolean show() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            // Acceptance of the request, not a premature native attach. Editor
            // discovery, snapshot and OH attach must execute in this order.
            return uiHandler().post(new Runnable() { @Override public void run() { show(); } });
        }
        try {
            NativeLoader.ensureLoaded();
            Session previous = sSession;
            InputConnection connection;
            if (previous != null && previous.current()) {
                connection = previous.connection;
            } else {
                sSession = null; // Discard queued edits before detaching.
                if (previous != null) {
                    previous.connection.finishComposingText();
                    if (!nativeHideKeyboardCapi()) return false;
                }
                connection = focusedInputConnection();
                if (connection == null || sCreatingView == null) return false;
                sSession = new Session(sCreatingView, connection);
            }
            updateSnapshot(connection);
            int id = windowId(sSession.view);
            boolean r = id > 0 && nativeShowKeyboardCapi(sSession.generation, id);
            if (!r) sSession = null;
            else updateSnapshot(connection);
            System.err.println("[" + TAG + "] show -> nativeShowKeyboard=" + r);
            return r;
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] show failed: " + t);
            return false;
        }
    }

    /** Hide the OHOS soft keyboard. Called from the IMM adapter. */
    public static boolean hide() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            return uiHandler().post(new Runnable() { @Override public void run() { hide(); } });
        }
        try {
            NativeLoader.ensureLoaded();
            Session previous = sSession;
            sSession = null;
            if (previous != null) previous.connection.finishComposingText();
            boolean r = nativeHideKeyboardCapi();
            System.err.println("[" + TAG + "] hide -> nativeHideKeyboard=" + r);
            return r;
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] hide failed: " + t);
            return false;
        }
    }

    // ------------------------------------------------------------------
    // Text routing: OHOS keyboard events -> focused Android InputConnection
    // ------------------------------------------------------------------
    static final int OP_INSERT = 0;
    static final int OP_DEL_BEFORE = 1;
    static final int OP_DEL_AFTER = 2;
    static final int OP_ENTER = 3;

    private static volatile Handler sUiHandler;

    private static Handler uiHandler() {
        if (sUiHandler == null) {
            synchronized (OhImeBridge.class) {
                if (sUiHandler == null) {
                    sUiHandler = new Handler(Looper.getMainLooper());
                }
            }
        }
        return sUiHandler;
    }

    private static Field sRootsField;     // WindowManagerGlobal.mRoots
    private static Field sViewField;      // ViewRootImpl.mView

    /** Reflectively locate the InputConnection of the focused editable View. */
    static InputConnection focusedInputConnection() {
        sCreatingView = null;
        try {
            Class<?> wmgCls = Class.forName("android.view.WindowManagerGlobal");
            Object wmg = wmgCls.getMethod("getInstance").invoke(null);
            if (sRootsField == null) {
                sRootsField = wmgCls.getDeclaredField("mRoots");
                sRootsField.setAccessible(true);
            }
            Object rootsObj = sRootsField.get(wmg);
            if (!(rootsObj instanceof ArrayList)) return null;
            ArrayList<?> roots = (ArrayList<?>) rootsObj;
            Class<?> vriCls = Class.forName("android.view.ViewRootImpl");
            if (sViewField == null) {
                sViewField = vriCls.getDeclaredField("mView");
                sViewField.setAccessible(true);
            }
            View focusView = null;
            for (int i = roots.size() - 1; i >= 0; i--) {
                Object vri = roots.get(i);
                if (vri == null) continue;
                Object vObj = sViewField.get(vri);
                if (!(vObj instanceof View)) continue;
                View v = (View) vObj;
                if (!v.isShown()) continue;
                View ff = v.findFocus();
                // 2026-06-26 INPUT-ROUTING DIAG: log every root's focus state to
                // see why typed text doesn't reach a 2nd-Activity's search field.
                System.err.println("[" + TAG + "] IC-scan root#" + i
                        + " rootView=" + v.getClass().getName()
                        + " hasWindowFocus=" + v.hasWindowFocus()
                        + " findFocus=" + (ff == null ? "null" : ff.getClass().getName())
                        + " isTextEditor=" + (ff != null && ff.onCheckIsTextEditor()));
                if (ff != null && ff.onCheckIsTextEditor()) {
                    if (v.hasWindowFocus()) { focusView = ff; break; }
                }
            }
            if (focusView == null) {
                System.err.println("[" + TAG + "] focusedInputConnection: no editable focus");
                return null;
            }
            EditorInfo ei = new EditorInfo();
            InputConnection ic = focusView.onCreateInputConnection(ei);
            if (ic != null) sCreatingView = focusView;
            setEditorInfo(ei);
            // 2026-06-26 INPUT-ROUTING DIAG: which view + IC actually won.
            System.err.println("[" + TAG + "] IC-chosen view=" + focusView.getClass().getName()
                    + " ic=" + (ic == null ? "null" : ic.getClass().getName()));
            if (ic == null) {
                System.err.println("[" + TAG + "] focusedInputConnection: view returned null IC ("
                        + focusView.getClass().getName() + ")");
            }
            return ic;
        } catch (Throwable t) {
            System.err.println("[" + TAG + "] focusedInputConnection: " + t);
            return null;
        }
    }

    private static void runOp(final int op, final int arg, final String text) {
        runOp(op, arg, text, -1);
    }

    private static void runOp(final int op, final int arg, final String text, final int rangeEnd) {
        final Session session = sSession;
        runOp(session, op, arg, text, rangeEnd);
    }

    /** Native callback carries the generation captured from its own editor proxy. */
    public static void nativeOnOperation(long generation, int op, int arg, String text, int end) {
        Session session = sSession;
        if (session == null || session.generation != generation) {
            System.err.println("[" + TAG + "] native stale generation discarded=" + generation);
            return;
        }
        if (op == 7) {
            switch (arg) {
                case 0: arg = android.R.id.selectAll; break;
                case 3: arg = android.R.id.cut; break;
                case 4: arg = android.R.id.copy; break;
                case 5: arg = android.R.id.paste; break;
                default: return;
            }
        } else if (op == 8) {
            switch (arg) {
                case 1: arg = KeyEvent.KEYCODE_DPAD_UP; break;
                case 2: arg = KeyEvent.KEYCODE_DPAD_DOWN; break;
                case 3: arg = KeyEvent.KEYCODE_DPAD_LEFT; break;
                case 4: arg = KeyEvent.KEYCODE_DPAD_RIGHT; break;
                default: return;
            }
        }
        runOp(session, op, arg, text, end);
    }

    private static void runOp(final Session session, final int op, final int arg,
            final String text, final int rangeEnd) {
        if (session == null) return;
        uiHandler().post(new Runnable() {
            @Override public void run() {
                try {
                    if (!session.current()) {
                        System.err.println("[" + TAG + "] stale edit discarded generation=" + session.generation);
                        return;
                    }
                    InputConnection ic = session.connection;
                    ic.beginBatchEdit();
                    try {
                    if (op == OP_INSERT) {
                        ic.commitText(text, 1);
                    } else if (op == OP_DEL_BEFORE) {
                        CharSequence selected = ic.getSelectedText(0);
                        if (selected != null && selected.length() > 0) ic.commitText("", 1);
                        else if (arg > 0) deleteCodePoints(ic, arg, true);
                    } else if (op == OP_DEL_AFTER) {
                        CharSequence selected = ic.getSelectedText(0);
                        if (selected != null && selected.length() > 0) ic.commitText("", 1);
                        else if (arg > 0) deleteCodePoints(ic, arg, false);
                    } else if (op == OP_ENTER) {
                        int action = sImeOptions & EditorInfo.IME_MASK_ACTION;
                        if (action != EditorInfo.IME_ACTION_NONE
                                && action != EditorInfo.IME_ACTION_UNSPECIFIED
                                && ic.performEditorAction(action)) return;
                        long now = SystemClock.uptimeMillis();
                        ic.sendKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_DOWN,
                                KeyEvent.KEYCODE_ENTER, 0));
                        ic.sendKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_UP,
                                KeyEvent.KEYCODE_ENTER, 0));
                    } else if (op == 4) {
                        if (arg >= 0 && rangeEnd >= arg) ic.setComposingRegion(arg, rangeEnd);
                        ic.setComposingText(text, 1);
                    } else if (op == 5) {
                        ic.finishComposingText();
                    } else if (op == 6) {
                        int end = text == null ? rangeEnd : Integer.parseInt(text);
                        if (arg >= 0 && end >= 0) ic.setSelection(arg, end);
                    } else if (op == 7) {
                        ic.performContextMenuAction(arg);
                    } else if (op == 8) {
                        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, arg));
                        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, arg));
                    }
                    } finally {
                        ic.endBatchEdit();
                        updateSnapshot(ic);
                    }
                    System.err.println("[" + TAG + "] runOp(" + op + ") done");
                } catch (Throwable t) {
                    System.err.println("[" + TAG + "] runOp(" + op + ") failed: " + t);
                }
            }
        });
    }

    // Use Unicode extended grapheme boundaries for user-visible characters,
    // including combining marks, ZWJ sequences, skin tones and regional flags.
    // Bound surrounding reads and refuse an incomplete boundary at the cap.
    private static void deleteCodePoints(InputConnection ic, int count, boolean before) {
        count = Math.min(count, 4096);
        final int cap = 65536;
        CharSequence chars = before ? ic.getTextBeforeCursor(cap, 0)
                : ic.getTextAfterCursor(cap, 0);
        if (chars == null) return;
        String value = chars.toString();
        BreakIterator boundaries = BreakIterator.getCharacterInstance(java.util.Locale.ROOT);
        boundaries.setText(value);
        int offset = before ? boundaries.last() : boundaries.first();
        for (int i=0; i<count; i++) {
            int next = before ? boundaries.previous() : boundaries.next();
            if (next == BreakIterator.DONE) break;
            offset = next;
        }
        if (value.length() == cap && (before ? offset == 0 : offset == cap)) return;
        int units = before ? value.length() - offset : offset;
        ic.deleteSurroundingText(before ? units : 0, before ? 0 : units);
    }

    /** Called from native: commit text into the focused field. */
    public static void nativeOnInsertText(String text) { runOp(OP_INSERT, 0, text); }

    /** Called from native: backspace (delete `n` chars before cursor). */
    public static void nativeOnDeleteBefore(int n) { runOp(OP_DEL_BEFORE, n, null); }

    /** Called from native: forward-delete (delete `n` chars after cursor). */
    public static void nativeOnDeleteAfter(int n) { runOp(OP_DEL_AFTER, n, null); }

    /** Called from native: enter / IME action key. */
    public static void nativeOnEnterAction(int enterKeyType) { runOp(OP_ENTER, enterKeyType, null); }

    public static void nativeOnPreview(String text, int start, int end) {
        // Updating the composing region and its text is one UI-thread edit.
        // Changing selection first would interleave with other editor events.
        runOp(4, start, text, end);
    }
    public static void nativeOnFinishPreview() { runOp(5, 0, null); }
    public static void nativeOnSelection(int start, int end) {
        runOp(6, start, Integer.toString(end));
    }
    public static void nativeOnExtend(int action) {
        int id;
        switch (action) {
            case 0: id = android.R.id.selectAll; break;
            case 3: id = android.R.id.cut; break;
            case 4: id = android.R.id.copy; break;
            case 5: id = android.R.id.paste; break;
            default: return;
        }
        runOp(7, id, null);
    }
    public static void nativeOnMove(int direction) {
        int code;
        switch (direction) {
            case 1: code = KeyEvent.KEYCODE_DPAD_UP; break;
            case 2: code = KeyEvent.KEYCODE_DPAD_DOWN; break;
            case 3: code = KeyEvent.KEYCODE_DPAD_LEFT; break;
            case 4: code = KeyEvent.KEYCODE_DPAD_RIGHT; break;
            default: return;
        }
        runOp(8, code, null);
    }

    private static void updateSnapshot(InputConnection ic) {
        if (ic == null) return;
        try {
            CharSequence before = ic.getTextBeforeCursor(8192, 0);
            CharSequence after = ic.getTextAfterCursor(8192, 0);
            sBefore = before == null ? "" : before.toString();
            sAfter = after == null ? "" : after.toString();
            ExtractedText extracted = ic.getExtractedText(new ExtractedTextRequest(), 0);
            SurroundingText surrounding = extracted == null ? ic.getSurroundingText(8192, 8192, 0) : null;
            if (extracted != null) {
                sCursor = Math.max(0, extracted.startOffset + extracted.selectionStart);
                sSelectionEnd = Math.max(0, extracted.startOffset + extracted.selectionEnd);
            } else if (surrounding != null && surrounding.getOffset() >= 0) {
                sCursor = surrounding.getOffset() + surrounding.getSelectionStart();
                sSelectionEnd = surrounding.getOffset() + surrounding.getSelectionEnd();
            } // Otherwise retain EditorInfo/IMM's absolute selection, not a window length.
            // The OH C API accepts the entire document, at most 8K UTF-16
            // units. Never claim a truncated window is the whole document.
            if (extracted != null && extracted.startOffset == 0 && extracted.text != null
                    && extracted.text.length() <= 262144 && extracted.partialStartOffset < 0
                    && sCursor <= extracted.text.length() && sSelectionEnd <= extracted.text.length()) {
                if (extracted.text.length() <= 8192)
                    nativeNotifySelectionCapi(extracted.text.toString(), sCursor, sSelectionEnd);
                else OhImeMetadata.selection(extracted.text.toString(), sCursor, sSelectionEnd);
            }
        } catch (RuntimeException ignored) {
            sBefore = "";
            sAfter = "";
        }
    }
}

package adapter.window;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.inputmethod.*;
import com.android.internal.inputmethod.IInputMethodSession;
import com.android.internal.inputmethod.IRemoteInputConnection;

/** Actual Android session endpoint for the OH text-editor connection. */
final class OhInputMethodSession extends IInputMethodSession.Stub {
    private volatile long generation;
    OhInputMethodSession(long generation) { this.generation = generation; }
    private boolean current() { return generation != 0 && generation == OhImeBridge.nativeGetGeneration(); }
    public void finishInput() { if (current()) OhImeBridge.hide(); generation = 0; }
    public void finishSession() { finishInput(); }
    public void removeImeSurface() { if (current()) OhImeBridge.hide(); }
    public void updateSelection(int oldStart,int oldEnd,int start,int end,int candidatesStart,int candidatesEnd) {
        if (current()) OhImeBridge.refresh(generation);
    }
    public void updateExtractedText(int token, ExtractedText text) { if (current()) OhImeBridge.refresh(generation); }
    public void updateCursor(Rect cursor) { if (current()) OhImeBridge.refresh(generation); }
    public void updateCursorAnchorInfo(CursorAnchorInfo info) { if (current()) OhImeBridge.refresh(generation); }
    public void invalidateInput(EditorInfo info, IRemoteInputConnection connection, int sessionId) {
        if (current()) { OhImeBridge.setEditorInfo(info); OhImeBridge.refresh(generation); }
    }
    public void viewClicked(boolean changed) { if (current()) OhImeBridge.refresh(generation); }
    public void displayCompletions(CompletionInfo[] completions) {
        // No negotiated C API for app-supplied completions. Do not pretend to
        // insert text or modify the keyboard's own candidate list.
    }
    public void appPrivateCommand(String action, Bundle data) {
        // Private commands are not portable without a negotiated protocol.
    }
}

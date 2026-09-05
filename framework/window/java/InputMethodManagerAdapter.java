/*
 * InputMethodManagerAdapter.java
 *
 * Adapter implementation of IInputMethodManager for the OH environment.
 *
 * Why this exists:
 *   AOSP InputMethodManager.createRealInstance() calls
 *     ServiceManager.getService("input_method")
 *   and throws IllegalStateException("IInputMethodManager is not available")
 *   if it returns null.  Editor (TextView.append → setText path) calls
 *   InputMethodManager.forContext() during view inflation, so without a
 *   non-null binder ANY app that inflates an editable / focusable text widget
 *   (which means basically every app) crashes before reaching onResume.
 *
 *   OH owns the actual keyboard and language plug-ins. This facade exposes
 *   their metadata and a real Android text session; it does not impersonate
 *   an installed Android InputMethodService APK. Unsupported privileged
 *   configuration must go through the OH keyboard settings/controller.
 *
 * Authoritative spec: doc/window_manager_ipc_adapter_design.html §5
 *   (added 2026-04-30 alongside this file).
 */
package adapter.window;

import android.os.IBinder;
import android.os.RemoteException;
import android.os.ResultReceiver;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ImeTracker;
import android.view.inputmethod.InputMethodInfo;
import android.view.inputmethod.InputMethodSubtype;
import android.window.ImeOnBackInvokedDispatcher;

import com.android.internal.inputmethod.IImeTracker;
import com.android.internal.inputmethod.IInputMethodClient;
import com.android.internal.inputmethod.IRemoteAccessibilityInputConnection;
import com.android.internal.inputmethod.IRemoteInputConnection;
import com.android.internal.inputmethod.InputBindResult;
import com.android.internal.view.IInputMethodManager;

import java.util.Collections;
import java.util.List;

public final class InputMethodManagerAdapter extends IInputMethodManager.Stub {

    private static final String TAG = "OH_IMMAdapter";

    private static volatile InputMethodManagerAdapter sInstance;

    public static InputMethodManagerAdapter getInstance() {
        if (sInstance == null) {
            synchronized (InputMethodManagerAdapter.class) {
                if (sInstance == null) {
                    sInstance = new InputMethodManagerAdapter();
                }
            }
        }
        return sInstance;
    }

    public InputMethodManagerAdapter() {
        System.err.println("[" + TAG + "] instantiated (OH metadata and bound editor sessions)");
    }

    private static void traceCall(String name) {
        // Quiet by default; flip if needed for debug.
        // System.err.println("[" + TAG + "] " + name);
    }

    // ========================================================================
    // Client lifecycle
    // ========================================================================

    @Override
    public void addClient(IInputMethodClient client, IRemoteInputConnection inputmethod,
            int untrustedDisplayId) throws RemoteException {
        traceCall("addClient(displayId=" + untrustedDisplayId + ")");
        // No IMMS to register with.  IMM client retains the reference to its
        // local-side IInputMethodClient mClient; we just need this call not to
        // throw so the constructor path completes.
    }

    // ========================================================================
    // Current OH user's installed/enabled keyboards and subtypes.
    // ========================================================================

    @Override
    public InputMethodInfo getCurrentInputMethodInfoAsUser(int userId) throws RemoteException {
        enforceUser(userId);
        return OhImeMetadata.current();
    }

    @Override
    public List<InputMethodInfo> getInputMethodList(int userId, int directBootAwareness)
            throws RemoteException {
        enforceUser(userId);
        return OhImeMetadata.list(false);
    }

    @Override
    public List<InputMethodInfo> getEnabledInputMethodList(int userId) throws RemoteException {
        enforceUser(userId);
        return OhImeMetadata.list(true);
    }

    @Override
    public List<InputMethodSubtype> getEnabledInputMethodSubtypeList(String imiId,
            boolean allowsImplicitlyEnabledSubtypes, int userId) throws RemoteException {
        enforceUser(userId);
        InputMethodInfo info = OhImeMetadata.current();
        String selected = imiId == null || imiId.isEmpty() ? (info == null ? null : info.getId()) : imiId;
        info = null;
        for (InputMethodInfo item : OhImeMetadata.list(true)) if (item.getId().equals(selected)) { info=item; break; }
        if (info == null) return Collections.emptyList();
        java.util.ArrayList<InputMethodSubtype> result = new java.util.ArrayList<>();
        for (int i=0;i<info.getSubtypeCount();i++) result.add(info.getSubtypeAt(i));
        return result;
    }

    @Override
    public InputMethodSubtype getLastInputMethodSubtype(int userId) throws RemoteException {
        enforceUser(userId);
        return null;
    }

    @Override
    public InputMethodSubtype getCurrentInputMethodSubtype(int userId) throws RemoteException {
        enforceUser(userId);
        InputMethodInfo info = OhImeMetadata.current();
        return info == null || info.getSubtypeCount() == 0 ? null : info.getSubtypeAt(0);
    }

    // ========================================================================
    private static void enforceUser(int userId) {
        if (userId != android.os.UserHandle.myUserId())
            throw new SecurityException("Cross-user keyboard queries are not authorized");
    }

    // Soft-input show / hide.
    // ========================================================================

    @Override
    public boolean showSoftInput(IInputMethodClient client, IBinder windowToken,
            ImeTracker.Token statsToken, int flags, int lastClickToolType,
            ResultReceiver resultReceiver, int reason) throws RemoteException {
        traceCall("showSoftInput");
        return OhImeBridge.requestVisibility(windowToken, true, resultReceiver);
    }

    @Override
    public boolean hideSoftInput(IInputMethodClient client, IBinder windowToken,
            ImeTracker.Token statsToken, int flags,
            ResultReceiver resultReceiver, int reason) throws RemoteException {
        traceCall("hideSoftInput");
        return OhImeBridge.requestVisibility(windowToken, false, resultReceiver);
    }

    // ========================================================================
    // Window/focus binding with an actual IInputMethodSession endpoint.
    // ========================================================================

    @Override
    public InputBindResult startInputOrWindowGainedFocus(
            int startInputReason, IInputMethodClient client, IBinder windowToken,
            int startInputFlags, int softInputMode, int windowFlags,
            EditorInfo editorInfo, IRemoteInputConnection inputConnection,
            IRemoteAccessibilityInputConnection remoteAccessibilityInputConnection,
            int unverifiedTargetSdkVersion, int userId,
            ImeOnBackInvokedDispatcher imeDispatcher) throws RemoteException {
        traceCall("startInputOrWindowGainedFocus(reason=" + startInputReason + ")");
        enforceUser(userId);
        if (editorInfo != null && editorInfo.inputType != EditorInfo.TYPE_NULL) {
            InputMethodInfo method = OhImeMetadata.current();
            if (method == null) return InputBindResult.NO_IME;
            OhImeBridge.setEditorInfo(editorInfo);
            if (!OhImeBridge.show()) return InputBindResult.IME_NOT_CONNECTED;
            long generation = OhImeBridge.nativeGetGeneration();
            if (generation == 0) return InputBindResult.IME_NOT_CONNECTED;
            System.err.println("[OH_IMMAdapter] bound id=" + method.getId() + " generation=" + generation);
            return new InputBindResult(0, new OhInputMethodSession(generation), null,
                    null, method.getId(), (int) (generation & 0x7fffffff), null, false);
        }
        return InputBindResult.NO_EDITOR;
    }

    // ========================================================================
    // IME picker / subtype management.
    // ========================================================================

    @Override
    public void showInputMethodPickerFromClient(IInputMethodClient client,
            int auxiliarySubtypeMode) throws RemoteException {
        OhImeMetadata.picker();
    }

    @Override
    public void showInputMethodPickerFromSystem(int auxiliarySubtypeMode, int displayId)
            throws RemoteException {
        OhImeMetadata.picker();
    }

    @Override
    public boolean isInputMethodPickerShownForTest() throws RemoteException {
        return false;
    }

    @Override
    public void setAdditionalInputMethodSubtypes(String id, InputMethodSubtype[] subtypes,
            int userId) throws RemoteException {
        throw new SecurityException("Configure OH keyboard subtypes through its controller");
    }

    @Override
    public void setExplicitlyEnabledInputMethodSubtypes(String imeId, int[] subtypeHashCodes,
            int userId) throws RemoteException {
        throw new SecurityException("Configure OH keyboard subtypes through its controller");
    }

    // ========================================================================
    // Display geometry / surface.
    // ========================================================================

    @Override
    public int getInputMethodWindowVisibleHeight(IInputMethodClient client) throws RemoteException {
        return OhImeBridge.visibleHeight();
    }

    @Override
    public void reportVirtualDisplayGeometryAsync(IInputMethodClient parentClient,
            int childDisplayId, float[] matrixValues) throws RemoteException {
    }

    @Override
    public void reportPerceptibleAsync(IBinder windowToken, boolean perceptible)
            throws RemoteException {
    }

    @Override
    public void removeImeSurface() throws RemoteException {
    }

    @Override
    public void removeImeSurfaceFromWindowAsync(IBinder windowToken) throws RemoteException {
    }

    // ========================================================================
    // Tracing  (disabled in adapter — IMM client tolerates false everywhere)
    // ========================================================================

    @Override
    public void startProtoDump(byte[] protoDump, int source, String where) throws RemoteException {
    }

    @Override
    public boolean isImeTraceEnabled() throws RemoteException {
        return false;
    }

    @Override
    public void startImeTrace() throws RemoteException {
    }

    @Override
    public void stopImeTrace() throws RemoteException {
    }

    // ========================================================================
    // Stylus handwriting  (not supported in adapter — false / no-op)
    // ========================================================================

    @Override
    public void startStylusHandwriting(IInputMethodClient client) throws RemoteException {
    }

    @Override
    public void prepareStylusHandwritingDelegation(IInputMethodClient client, int userId,
            String delegatePackageName, String delegatorPackageName) throws RemoteException {
    }

    @Override
    public boolean acceptStylusHandwritingDelegation(IInputMethodClient client, int userId,
            String delegatePackageName, String delegatorPackageName) throws RemoteException {
        return false;
    }

    @Override
    public boolean isStylusHandwritingAvailableAsUser(int userId) throws RemoteException {
        return false;
    }

    @Override
    public void addVirtualStylusIdForTestSession(IInputMethodClient client) throws RemoteException {
    }

    @Override
    public void setStylusWindowIdleTimeoutForTest(IInputMethodClient client, long timeout)
            throws RemoteException {
    }

    // ========================================================================
    // ImeTracker service  (returns null — IMM client uses ImeTracker.forLogging
    // local helper which is independent of this binder).
    // ========================================================================

    @Override
    public IImeTracker getImeTrackerService() throws RemoteException {
        return null;
    }
}

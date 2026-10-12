/*
 * WindowSessionAdapter.java
 *
 * Replaces IWindowSessionBridge (InvocationHandler pattern) with a direct
 * class inheritance approach extending IWindowSession.Stub.
 *
 * Routes Android IWindowSession calls to OpenHarmony ISession /
 * ISceneSessionManager system services via JNI.
 *
 * Methods are categorized as:
 *   [BRIDGED] - Mapped to OH equivalent via native call
 *   [STUB]    - No OH equivalent, returns safe default
 */
package adapter.window;

import adapter.window.InputEventBridge;

import android.content.ClipData;
import android.content.res.Configuration;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.Region;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteCallback;
import android.os.RemoteException;
import android.util.Log;
import android.util.MergedConfiguration;
import android.view.DisplayCutout;
import android.view.InputChannel;
import android.view.IWindow;
import android.view.IWindowId;
import android.view.IWindowSession;
import android.view.MotionEvent;
import android.view.InsetsSourceControl;
import android.view.InsetsState;
import android.view.Surface;
import android.view.SurfaceControl;
import android.view.View;
import android.view.WindowManager;
import android.window.ClientWindowFrames;
import android.window.OnBackInvokedCallbackInfo;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Adapter that bridges Android's IWindowSession to OpenHarmony's
 * ISession + ISceneSessionManager.
 *
 * Extends IWindowSession.Stub directly instead of using an InvocationHandler
 * proxy, providing compile-time safety for all 42 AIDL methods.
 */
public class WindowSessionAdapter extends IWindowSession.Stub {

    private static final String TAG = "OH_WSAdapter";

    private final long mOhSession;

    // Track OH sessions created by this adapter, keyed by IWindow IBinder
    private final Map<IBinder, int[]> mSessionMap = new HashMap<>();

    // Android gives a window no surface while its activity is not yet visible, and WindowToken
    // stacks an activity's base window below the token's other windows. OpenHarmony stacks this
    // app's windows by creation order (measured: raising, not raising and WINDOW_TYPE_MEDIA all
    // leave the last-created window on top), so a dialog shown from onCreate/onResume, created
    // before the activity's own window, ends up underneath it. Such a dialog is held back: it
    // gets its input channel and frames at once, and its OH session right after the base window
    // of its token has one, so creation order is Android's stacking order.
    private static final long HELD_BACK_FALLBACK_MS = 3000;
    private static final long HELD_BACK_LIMIT_MS = 60000;
    private final java.util.Set<IBinder> mTokensWithBase = new java.util.HashSet<>();
    private final Map<IBinder, IBinder> mBaseWindowTokens = new HashMap<>();
    private final Map<IBinder, HeldBackWindow> mHeldBack = new java.util.LinkedHashMap<>();
    private android.os.Handler mMainHandler;

    private static final class HeldBackWindow {
        final IWindow window;
        final IBinder token;
        final String bundleName, abilityName, moduleName, windowName;
        final int windowType, layerStackId, width, height;
        final long ohTokenAddr;
        final float dimAmount;
        int lastWidth, lastHeight;

        HeldBackWindow(IWindow window, IBinder token, String bundleName, String abilityName,
                String moduleName, String windowName, int windowType, int layerStackId,
                int width, int height, long ohTokenAddr) {
            this.window = window; this.token = token; this.bundleName = bundleName;
            this.abilityName = abilityName; this.moduleName = moduleName; this.windowName = windowName;
            this.windowType = windowType; this.layerStackId = layerStackId; this.width = width;
            this.height = height; this.ohTokenAddr = ohTokenAddr;
            this.lastWidth = width; this.lastHeight = height;
            this.dimAmount = 0f;
        }

        HeldBackWindow(HeldBackWindow held, float dimAmount) {
            this.window = held.window; this.token = held.token; this.bundleName = held.bundleName;
            this.abilityName = held.abilityName; this.moduleName = held.moduleName;
            this.windowName = held.windowName; this.windowType = held.windowType;
            this.layerStackId = held.layerStackId; this.width = held.width; this.height = held.height;
            this.ohTokenAddr = held.ohTokenAddr; this.lastWidth = held.lastWidth;
            this.lastHeight = held.lastHeight; this.dimAmount = dimAmount;
        }
    }

    // [G2.8-RELAYOUT-LOOP 2026-06-01] Reverse-push IWindow.resized only ONCE per window
    // (bootstrap to escape ViewRootImpl's 0x0-frame spiral). Re-pushing every relayout
    // re-triggers a ViewRootImpl traversal → infinite relayout loop that churns the surface.
    private final java.util.Set<IBinder> mReversePushed =
            java.util.Collections.synchronizedSet(new java.util.HashSet<IBinder>());
    // [G2.8b-SC-CACHE 2026-06-01] Cache the SurfaceControl per window. Building a NEW
    // SurfaceControl every relayout makes ViewRootImpl see a changed surface each traversal
    // → hwui destroys+recreates its EGLSurface → the recreate fails (cannot bind a 2nd window
    // surface to the same OH producer) → libhwui abort before frame #1 draws.
    private final Map<IBinder, SurfaceControl> mSurfaceControls =
            java.util.Collections.synchronizedMap(new HashMap<IBinder, SurfaceControl>());

    private static native long nativeGetOHSessionService();
    /**
     * Spec: doc/window_manager_ipc_adapter_design.html §3.1.5.2 / §3.1.5.6.1
     * - bundleName/abilityName/moduleName: SCB decoration / theme routing (§3.1.4.2)
     * - ohTokenAddr: OH AbilityRecord token raw pointer; lookup via
     *   AppSchedulerBridge.OhTokenRegistry.findOhToken(attrs.token).
     *   Native side reinterpret_cast → sptr<IRemoteObject> → CreateAndConnect
     *   with property->SetTokenState(true). Pre-fix value 0 meant "no token";
     *   non-zero closes the §3.1.4.6.3 流转断点.
     * Returns int[6]: {sessionId, surfaceNodeId, displayId, w, h, wsErrCode}
     *   wsErrCode = 0 on success, OH WSError integer on failure (§3.1.5.6.2)
     */
    private static native int[] nativeCreateSession(Object androidWindow,
            String bundleName, String abilityName, String moduleName,
            String windowName,
            int androidWindowType, int displayId,
            int requestedWidth, int requestedHeight,
            long ohTokenAddr);
    private static native int nativeUpdateSessionRect(int sessionId,
            int x, int y, int width, int height);
    private static native int nativeNotifyDrawingCompleted(int sessionId);
    private static native void nativeDestroySession(int sessionId);
    // 2026-05-19: visibility transition helpers — counterpart to AddWindow at
    // session creation.  Called from relayout based on viewVisibility arg.
    // See oh_window_manager_client.cpp::hideWindow / showWindow for design
    // notes (in-App-process C++ cache, idempotent at native layer).
    private static native int nativeHideWindow(int sessionId);
    private static native int nativeShowWindow(int sessionId);
    private static native long nativeGetSurfaceNodeId(int sessionId);
    // [G3.6-MULTIWINDOW 2026-06-02] stamp the SurfaceControl's session (bridge JNI ->
    // oh_sc_attach_session via dlsym) so each window's SC resolves to its OWN OH NativeWindow.
    private static native void nativeAttachSessionToSc(SurfaceControl sc, int sessionId);
    private static native int nativeInjectTouchEvent(int sessionId, int action,
            float x, float y, long downTime, long eventTime);

    // Surface bridge native methods
    private static native boolean nativeCreateOHSurface(int sessionId, String windowName,
            int width, int height, int format);
    private static native long nativeGetSurfaceHandle(int sessionId,
            int width, int height, int format);
    private static native void nativeNotifySurfaceDrawingCompleted(int sessionId);
    private static native void nativeUpdateSurfaceSize(int sessionId, int width, int height);
    private static native void nativeDestroyOHSurface(int sessionId);
    private static native int[] nativeDequeueBuffer(long producerHandle,
            int width, int height, int format, long usage);
    private static native int nativeQueueBuffer(long producerHandle, int slot, int fenceFd,
            long timestamp, int cropLeft, int cropTop, int cropRight, int cropBottom);
    private static native int nativeCancelBuffer(long producerHandle, int slot, int fenceFd);

    /**
     * Creates a new WindowSessionAdapter.
     */
    public WindowSessionAdapter() {
        mOhSession = nativeGetOHSessionService();
        Log.i(TAG, "WindowSessionAdapter created, ohSession=0x" + Long.toHexString(mOhSession));
    }

    // §3.1.5.6.3 — Android attrs.type → "is this the main DecorView?" predicate.
    // Reuse path applies only to main windows; sub windows / system overlays
    // legitimately need their own SceneSession.
    // FLAG_DIM_BEHIND: WMS puts a dim layer under the window. OpenHarmony has no such flag, so
    // the layer is a real window: a translucent black view added on the same token right before
    // the dimming window gets its OH session, so creation order stacks it just below. As with
    // Android's Dimmer, only the topmost visible dimming window of a token shows its layer.
    private static final class DimLayer {
        final IBinder token;
        final View view;
        final int color;
        boolean ownerVisible = true;
        boolean shown = true;

        DimLayer(IBinder token, View view, int color) {
            this.token = token;
            this.view = view;
            this.color = color;
        }
    }

    private final Map<IBinder, DimLayer> mDimLayers = new java.util.LinkedHashMap<>();

    // ViewRootImpl passes LayoutParams to relayout only when they changed (null otherwise), and
    // placement needs gravity, x/y and flags on every relayout, so each window's are kept.
    private final Map<IBinder, WindowManager.LayoutParams> mWindowAttrs = new HashMap<>();

    private WindowManager.LayoutParams rememberAttrs(IWindow window, WindowManager.LayoutParams attrs) {
        synchronized (mWindowAttrs) {
            if (attrs != null) {
                WindowManager.LayoutParams copy = new WindowManager.LayoutParams();
                copy.copyFrom(attrs);
                mWindowAttrs.put(window.asBinder(), copy);
                return copy;
            }
            return mWindowAttrs.get(window.asBinder());
        }
    }

    private static float dimAmountOf(WindowManager.LayoutParams attrs) {
        return attrs != null && (attrs.flags & WindowManager.LayoutParams.FLAG_DIM_BEHIND) != 0
                ? Math.max(0f, Math.min(1f, attrs.dimAmount)) : 0f;
    }

    private void addDimLayer(IBinder owner, IBinder token, float dimAmount, String ownerName) {
        if (dimAmount <= 0f || token == null) return;
        synchronized (mDimLayers) {
            if (mDimLayers.containsKey(owner)) return;
        }
        try {
            android.app.Application app = android.app.ActivityThread.currentApplication();
            if (app == null) return;
            View dim = new View(app);
            int color = android.graphics.Color.argb(Math.round(dimAmount * 255f), 0, 0, 0);
            dim.setBackgroundColor(color);
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.TYPE_APPLICATION,
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                            | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                    android.graphics.PixelFormat.TRANSLUCENT);
            lp.token = token;
            lp.windowAnimations = 0;
            lp.setTitle("DimLayer:" + ownerName);
            synchronized (mDimLayers) {
                mDimLayers.put(owner, new DimLayer(token, dim, color));
            }
            ((WindowManager) app.getSystemService(android.content.Context.WINDOW_SERVICE)).addView(dim, lp);
            System.err.println("[OH_WSA] dim layer " + dimAmount + " under '" + ownerName + "'");
            updateDimLayers(token);
        } catch (Throwable t) {
            synchronized (mDimLayers) {
                mDimLayers.remove(owner);
            }
            System.err.println("[OH_WSA] dim layer failed: " + t);
        }
    }

    private void setDimOwnerVisible(IBinder owner, boolean visible) {
        IBinder token;
        synchronized (mDimLayers) {
            DimLayer layer = mDimLayers.get(owner);
            if (layer == null || layer.ownerVisible == visible) return;
            layer.ownerVisible = visible;
            token = layer.token;
        }
        updateDimLayers(token);
    }

    private void removeDimLayer(IBinder owner) {
        final DimLayer layer;
        synchronized (mDimLayers) {
            layer = mDimLayers.remove(owner);
        }
        if (layer == null) return;
        if (mMainHandler == null) mMainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        mMainHandler.post(new Runnable() {
            @Override
            public void run() {
                try {
                    android.app.Application app = android.app.ActivityThread.currentApplication();
                    if (app != null && layer.view.isAttachedToWindow()) {
                        ((WindowManager) app.getSystemService(android.content.Context.WINDOW_SERVICE))
                                .removeView(layer.view);
                    }
                } catch (Throwable t) {
                    System.err.println("[OH_WSA] dim layer removal failed: " + t);
                }
            }
        });
        updateDimLayers(layer.token);
    }

    /** Show only the topmost visible dimming window's layer on a token. */
    private void updateDimLayers(IBinder token) {
        java.util.List<DimLayer> layers = new java.util.ArrayList<>();
        synchronized (mDimLayers) {
            for (DimLayer layer : mDimLayers.values()) {
                if (layer.token == token) layers.add(layer);
            }
        }
        DimLayer top = null;
        for (DimLayer layer : layers) {
            if (layer.ownerVisible) top = layer;
        }
        // An inactive layer is drawn transparent rather than hidden: hiding an OH window does
        // not take effect on this board (nativeHideWindow rc=1005), so its last frame would stay.
        for (DimLayer layer : layers) {
            final boolean show = layer == top;
            if (layer.shown == show) continue;
            layer.shown = show;
            final View view = layer.view;
            final int color = show ? layer.color : 0;
            if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) {
                view.setBackgroundColor(color);
            } else {
                view.post(new Runnable() {
                    @Override
                    public void run() {
                        view.setBackgroundColor(color);
                    }
                });
            }
        }
    }

    /**
     * Where WMS puts a window that does not fill the display: Android's own WindowLayout, from its
     * gravity, x/y, margins and the system-bar insets it fits, so a dialog is centred. Windows
     * that fill the display keep the full-display frame they have always had here. Only the
     * position is taken; the size stays the one the surface is allocated with.
     */
    private static Rect placeWindow(IWindow window, WindowManager.LayoutParams attrs, int width, int height) {
        Rect frame = new Rect(0, 0, width, height);
        int[] display = getDefaultDisplaySize();
        if (attrs == null || (attrs.width == WindowManager.LayoutParams.MATCH_PARENT
                && attrs.height == WindowManager.LayoutParams.MATCH_PARENT)
                || (width >= display[0] && height >= display[1])) {
            return frame;
        }
        try {
            InsetsState state = new InsetsState();
            if (window != null) OhInsetsBridge.copyForWindow(window, state);
            Rect bounds = new Rect(0, 0, display[0], display[1]);
            Rect cutoutSafe = new Rect();
            state.getDisplayCutoutSafe(cutoutSafe);
            ClientWindowFrames frames = new ClientWindowFrames();
            new android.view.WindowLayout().computeFrames(attrs, state, cutoutSafe, bounds,
                    android.app.WindowConfiguration.WINDOWING_MODE_FULLSCREEN, width, height,
                    android.view.WindowInsets.Type.defaultVisible(), 1f, frames);
            frame.offsetTo(frames.frame.left, frames.frame.top);
        } catch (Throwable t) {
            System.err.println("[OH_WSA] window placement failed, keeping (0,0): " + t);
        }
        return frame;
    }

    /**
     * Android's sub-window types (FIRST_SUB_WINDOW to LAST_SUB_WINDOW): a PopupWindow's panel,
     * menus, dropdowns, attached dialogs. The native client gives an application window (a dialog)
     * an OH sub-window session of its own, but asks for these as an OH main window, which this
     * board refuses (1005); the window then draws into a surface node with no session, which nothing
     * shows: an overflow menu took taps and was never seen. A sub-window is an application window
     * placed over its parent, so it asks as one (sessionTypeFor) and gets the display-sized OH
     * window a dialog gets: placement moves the window's content to its frame inside the OH window
     * (placeWindow, applyPosition), and an OH window only as wide as the popup clips it away.
     */
    private static boolean isSubWindow(int windowType) {
        return windowType >= WindowManager.LayoutParams.FIRST_SUB_WINDOW
                && windowType <= WindowManager.LayoutParams.LAST_SUB_WINDOW;
    }

    private static int sessionTypeFor(int windowType) {
        return isSubWindow(windowType) ? WindowManager.LayoutParams.TYPE_APPLICATION : windowType;
    }

    /** Move a placed window's layer to its frame; a size update resets the layer to (0,0). */
    private static void applyPosition(SurfaceControl sc, Rect frame) {
        if (sc == null || !sc.isValid() || (frame.left == 0 && frame.top == 0)) return;
        try {
            new SurfaceControl.Transaction()
                    .setBufferSize(sc, frame.width(), frame.height())
                    .setPosition(sc, frame.left, frame.top)
                    .apply();
        } catch (Throwable t) {
            System.err.println("[OH_WSA] layer position failed: " + t);
        }
    }

    /** An application window on an activity token whose base window has no OH session yet. */
    private boolean shouldHoldBack(int windowType, IBinder token) {
        if (token == null
                || windowType == WindowManager.LayoutParams.TYPE_BASE_APPLICATION
                || windowType == WindowManager.LayoutParams.TYPE_APPLICATION_STARTING
                || windowType < WindowManager.LayoutParams.FIRST_APPLICATION_WINDOW
                || windowType > WindowManager.LayoutParams.LAST_APPLICATION_WINDOW) {
            return false;
        }
        synchronized (mHeldBack) {
            return !mTokensWithBase.contains(token);
        }
    }

    private int holdBack(final HeldBackWindow held, WindowManager.LayoutParams attrs,
            InputChannel outInputChannel, InsetsState insetsState, Rect attachedFrame,
            float[] sizeCompatScale) {
        final IBinder key = held.window.asBinder();
        synchronized (mHeldBack) {
            mHeldBack.put(key, held);
        }
        System.err.println("[OH_WSA] holding back type=" + held.windowType + " '" + held.windowName
                + "' until its activity's window has an OH session");
        if (outInputChannel != null) {
            InputEventBridge.getInstance().createUnboundInputChannelPair(key, held.windowName)
                    .copyTo(outInputChannel);
        }
        if (insetsState != null) OhInsetsBridge.copyForWindow(held.window, insetsState);
        if (sizeCompatScale != null && sizeCompatScale.length > 0) sizeCompatScale[0] = 1.0f;
        if (attachedFrame != null) attachedFrame.set(0, 0, held.width, held.height);
        if (mMainHandler == null) mMainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        scheduleHeldBackCheck(key, held.token, android.os.SystemClock.uptimeMillis());
        return buildAddDisplayFlags(attrs);
    }

    /**
     * An activity that never adds a window of its own (windowNoDisplay, or it finished) must not
     * keep its dialogs invisible. One that is still launching will add its window when it
     * resumes, however long its onCreate takes (McDonald's needs more than 3 s on this board), so
     * the dialog keeps waiting for it, up to a limit.
     */
    private void scheduleHeldBackCheck(final IBinder key, final IBinder token, final long heldSince) {
        mMainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                synchronized (mHeldBack) {
                    if (!mHeldBack.containsKey(key)) return;
                }
                long waited = android.os.SystemClock.uptimeMillis() - heldSince;
                if (activityStillComing(token) && waited < HELD_BACK_LIMIT_MS) {
                    scheduleHeldBackCheck(key, token, heldSince);
                    return;
                }
                createHeldBackSession(key, "no base window after " + waited + " ms");
            }
        }, HELD_BACK_FALLBACK_MS);
    }

    /** The activity behind a token will still add its own window: alive, displayable, not resumed yet. */
    private static boolean activityStillComing(IBinder token) {
        try {
            android.app.ActivityThread thread = android.app.ActivityThread.currentActivityThread();
            android.app.Activity activity = thread != null ? thread.getActivity(token) : null;
            if (activity == null || activity.isFinishing() || activity.isDestroyed()) return false;
            java.lang.reflect.Field visible = android.app.Activity.class.getDeclaredField("mVisibleFromClient");
            visible.setAccessible(true);
            if (!visible.getBoolean(activity)) return false;
            java.lang.reflect.Field resumed = android.app.Activity.class.getDeclaredField("mResumed");
            resumed.setAccessible(true);
            return !resumed.getBoolean(activity);
        } catch (Throwable t) {
            System.err.println("[OH_WSA] activity state unreadable, releasing held-back window: " + t);
            return false;
        }
    }

    private void baseWindowAdded(IBinder baseWindow, IBinder token) {
        java.util.List<IBinder> release = new java.util.ArrayList<>();
        synchronized (mHeldBack) {
            mTokensWithBase.add(token);
            mBaseWindowTokens.put(baseWindow, token);
            for (Map.Entry<IBinder, HeldBackWindow> entry : mHeldBack.entrySet()) {
                if (entry.getValue().token == token) release.add(entry.getKey());
            }
        }
        for (IBinder key : release) createHeldBackSession(key, "its activity's window now has one");
    }

    /** Create a held-back window's OH session, in the order it was added, and make it relayout. */
    private void createHeldBackSession(IBinder key, String why) {
        HeldBackWindow held;
        synchronized (mHeldBack) {
            held = mHeldBack.remove(key);
        }
        if (held == null) return;
        addDimLayer(key, held.token, held.dimAmount, held.windowName);
        int[] sessionInfo;
        try {
            sessionInfo = nativeCreateSession(key, held.bundleName, held.abilityName, held.moduleName,
                    held.windowName, held.windowType, held.layerStackId, held.width, held.height,
                    held.ohTokenAddr);
        } catch (Throwable t) {
            System.err.println("[OH_WSA] held-back nativeCreateSession threw " + t);
            return;
        }
        if (sessionInfo == null || sessionInfo.length < 6 || sessionInfo[0] < 0) {
            System.err.println("[OH_WSA] held-back window got no OH session: "
                    + (sessionInfo == null || sessionInfo.length < 6 ? "malformed" : "wsErr=" + sessionInfo[5]));
            return;
        }
        mSessionMap.put(key, sessionInfo);
        InputEventBridge.getInstance().bindInputChannel(key, sessionInfo[0]);
        System.err.println("[OH_WSA] held-back type=" + held.windowType + " '" + held.windowName
                + "' -> sessionId=" + sessionInfo[0] + " (" + why + ")");
        // Its earlier relayouts returned no surface; a forced relayout now picks one up.
        try {
            ClientWindowFrames frames = new ClientWindowFrames();
            frames.frame.set(0, 0, held.lastWidth, held.lastHeight);
            frames.displayFrame.set(0, 0, held.lastWidth, held.lastHeight);
            frames.parentFrame.set(0, 0, held.lastWidth, held.lastHeight);
            frames.attachedFrame = new Rect(0, 0, held.lastWidth, held.lastHeight);
            InsetsState insets = new InsetsState();
            OhInsetsBridge.copyForWindow(held.window, insets);
            held.window.resized(frames, false /* reportDraw */, new android.util.MergedConfiguration(),
                    insets, true /* forceLayout */, false /* alwaysConsumeSystemBars */,
                    0 /* displayId */, 0 /* syncSeqId */, false /* dragResizing */, null);
        } catch (Throwable t) {
            System.err.println("[OH_WSA] held-back relayout request failed: " + t);
        }
    }

    private static boolean isMainAppWindow(int androidWindowType) {
        // TYPE_BASE_APPLICATION=1 / TYPE_APPLICATION=2 / TYPE_APPLICATION_STARTING=3
        return androidWindowType >= 1 && androidWindowType <= 3;
    }

    // §3.1.5.6.2 — OH WSError to AOSP ADD_* code. Real WSError enum values
    // (from foundation/window/window_manager/window_scene/interfaces/include/ws_common.h):
    //   0  = WS_OK
    //   1  = WS_DO_NOTHING
    //   2..18 = various invalid/permission codes
    //   8  = WS_ERROR_NOT_SYSTEM_APP
    //   1000 = WS_ERROR_NEED_REPORT_BASE
    //   1001 = WS_ERROR_NULLPTR
    //   1002 = WS_ERROR_INVALID_TYPE
    //   1003 = WS_ERROR_INVALID_PARAM
    //   1004 = WS_ERROR_SAMGR
    //   1005 = WS_ERROR_IPC_FAILED   ← G2.14 currently observed, comes from
    //                                  early-exit when ssmProxy_==nullptr (3-hop
    //                                  chain blocked by foundation-side IMockSession
    //                                  systemic IPC issue)
    private static int mapWsErrorToAddCode(int wsErr) {
        switch (wsErr) {
            case 8:    // WS_ERROR_NOT_SYSTEM_APP
                return android.view.WindowManagerGlobal.ADD_PERMISSION_DENIED;     // -8
            case 1003: // WS_ERROR_INVALID_PARAM — real "bad token / bad window" cause
                return android.view.WindowManagerGlobal.ADD_BAD_APP_TOKEN;          // -1
            case 1002: // WS_ERROR_INVALID_TYPE — bad windowType param
                return android.view.WindowManagerGlobal.ADD_INVALID_TYPE;           // -10
            case 1001: // WS_ERROR_NULLPTR
            case 1004: // WS_ERROR_SAMGR
            case 1005: // WS_ERROR_IPC_FAILED
            default:
                return android.view.WindowManagerGlobal.ADD_INVALID_DISPLAY;       // -9, IPC layer
        }
    }

    // §3.1.4.2 — best-effort lookup of the App's bundleName.
    // ActivityThread.currentPackageName() may return null very early (before
    // handleBindApplication completes); fall back to "android" so the SSM
    // call still has a non-null bundleName for SCB routing.
    // ---- SurfaceView: a surface of its own ---------------------------------------------------
    // Android composes a SurfaceView's layer beside its window's; here a SurfaceControl resolves
    // to its session's one OH window, so a SurfaceView's producer (an engine's EGL or Vulkan
    // swapchain) and hwui drew into the same OH window: Flutter and game engines never showed.
    // A SurfaceView that fills its window gets an OH sub-window session of its own, created after
    // the activity's (so above it: OH stacks app windows by creation order) and bound to its
    // BLAST SurfaceControl before its BLASTBufferQueue resolves a window. OH windows cannot be
    // placed here (updateSessionRect has no IPC on this path; every window sits at 0,0), so a
    // smaller SurfaceView gets one only where it starts at its window's origin, which a window
    // of its size sits on; elsewhere -- an inline video, say -- it keeps sharing its window.
    // Shared, its producer and hwui race for the window's EGL surface: usp's GLSurfaceView
    // (1200x1152 at the top of a 1200x1920 window) won it in 3 of 5 launches, and hwui aborted
    // ("drawRenderNode called on a context with no surface!"); when hwui won, the emulator's
    // screen was missing.
    // OH moves focus to the SurfaceView's window once it shows; it is created for a SurfaceViewWindow,
    // through which that focus counts as its activity window's (WindowCallbackBridge).
    private static final Map<Object, Integer> sSurfaceViewSessions = new java.util.IdentityHashMap<>();
    // The window each such session was created for: it names the SurfaceView's own window, so its
    // OH focus counts as the window's (WindowCallbackBridge's focus groups).
    private static final Map<Object, SurfaceViewWindow> sSurfaceViewWindows = new java.util.IdentityHashMap<>();

    /** SurfaceView (framework, by reflection) after building its BLAST layer. 0: not taken. */
    public static int attachSurfaceView(Object surfaceView, SurfaceControl blast, String name,
            int width, int height, int windowWidth, int windowHeight, IBinder token) {
        int[] at = new int[2];
        if (surfaceView instanceof android.view.View) {
            ((android.view.View) surfaceView).getLocationInWindow(at);
        }
        boolean fills = width * 10 >= windowWidth * 9 && height * 10 >= windowHeight * 9;
        boolean atOrigin = at[0] == 0 && at[1] == 0;
        if (blast == null || width <= 0 || height <= 0 || !(fills || atOrigin)) {
            // Said, so a SurfaceView left sharing its window shows why, and where it sits.
            System.err.println("[OH_WSA] SurfaceView '" + name + "' " + width + "x" + height
                    + " at " + at[0] + "," + at[1] + " in a " + windowWidth + "x" + windowHeight
                    + " window shares it");
            return 0;
        }
        try {
            long ohTokenAddr = 0L;
            if (token != null) {
                Long addr = adapter.core.OhTokenRegistry.findOhToken(token);
                if (addr != null) ohTokenAddr = addr;
            }
            android.view.ViewRootImpl owner = surfaceView instanceof android.view.View
                    ? ((android.view.View) surfaceView).getViewRootImpl() : null;
            SurfaceViewWindow window = new SurfaceViewWindow(owner);
            int[] info = nativeCreateSession(window, currentPackageName(),
                    currentActivityName(), "entry", "SurfaceView:" + name,
                    WindowManager.LayoutParams.TYPE_APPLICATION, 0, width, height, ohTokenAddr);
            if (info == null || info.length < 6 || info[0] <= 0) {
                System.err.println("[OH_WSA] SurfaceView '" + name + "' got no OH session: "
                        + (info == null || info.length < 6 ? "malformed" : "wsErr=" + info[5]));
                return 0;
            }
            int sessionId = info[0];
            nativeShowWindow(sessionId);
            nativeAttachSessionToSc(blast, sessionId);
            synchronized (sSurfaceViewSessions) {
                sSurfaceViewSessions.put(surfaceView, sessionId);
                sSurfaceViewWindows.put(surfaceView, window);
            }
            System.err.println("[OH_WSA] SurfaceView '" + name + "' " + width + "x" + height
                    + " at " + at[0] + "," + at[1] + " -> own sessionId=" + sessionId);
            return sessionId;
        } catch (Throwable t) {
            System.err.println("[OH_WSA] SurfaceView '" + name + "' own session failed: " + t);
            return 0;
        }
    }

    /** SurfaceView (framework, by reflection) when it releases its surfaces. */
    public static void detachSurfaceView(Object surfaceView) {
        Integer sessionId;
        SurfaceViewWindow window;
        synchronized (sSurfaceViewSessions) {
            sessionId = sSurfaceViewSessions.remove(surfaceView);
            window = sSurfaceViewWindows.remove(surfaceView);
        }
        if (window != null) WindowCallbackBridge.surfaceWindowGone(window);
        if (sessionId == null) return;
        try {
            nativeDestroySession(sessionId);
            System.err.println("[OH_WSA] SurfaceView session " + sessionId + " destroyed");
        } catch (Throwable t) {
            System.err.println("[OH_WSA] SurfaceView session " + sessionId + " destroy failed: " + t);
        }
    }

    private static String currentPackageName() {
        try {
            Class<?> c = Class.forName("android.app.ActivityThread");
            String name = (String) c.getMethod("currentPackageName").invoke(null);
            if (name != null && !name.isEmpty()) return name;
        } catch (Throwable ignored) {}
        return "android";
    }

    // §3.1.4.2 — best-effort lookup of the current Activity's simple name.
    // We don't have a stable ActivityThread API for this, so we walk the
    // top-of-stack record reflectively. Falls back to "MainAbility" so
    // native side fallback kicks in identically.
    private static String currentActivityName() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Object instance = at.getMethod("currentActivityThread").invoke(null);
            if (instance == null) return "MainAbility";
            java.lang.reflect.Field f = at.getDeclaredField("mActivities");
            f.setAccessible(true);
            Object map = f.get(instance);
            if (map instanceof java.util.Map) {
                @SuppressWarnings("unchecked")
                java.util.Map<Object, Object> m = (java.util.Map<Object, Object>) map;
                for (Object record : m.values()) {
                    if (record == null) continue;
                    java.lang.reflect.Field af = record.getClass().getDeclaredField("activity");
                    af.setAccessible(true);
                    Object activity = af.get(record);
                    if (activity != null) {
                        String full = activity.getClass().getName();
                        int dot = full.lastIndexOf('.');
                        return (dot >= 0 && dot + 1 < full.length()) ? full.substring(dot + 1) : full;
                    }
                }
            }
        } catch (Throwable ignored) {}
        return "MainAbility";
    }

    // ====================================================================
    // Category 1: Window Lifecycle
    // ====================================================================

    /**
     * [BRIDGED] addToDisplay -> OH ISceneSessionManager.CreateAndConnectSpecificSession
     */
    @Override
    public int addToDisplay(IWindow window, WindowManager.LayoutParams attrs,
            int viewVisibility, int layerStackId, int requestedVisibleTypes,
            InputChannel outInputChannel, InsetsState insetsState,
            InsetsSourceControl.Array activeControls, Rect attachedFrame,
            float[] sizeCompatScale) throws RemoteException {
        System.err.println("[OH_WSA] ENTRY addToDisplay attrs.type=" + attrs.type
                + " attrs.token=" + attrs.token + " w=" + attrs.width + " h=" + attrs.height
                + " flags=0x" + Integer.toHexString(attrs.flags)
                + " (HW_ACCEL=" + ((attrs.flags
                        & WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED) != 0)
                + ")");
        logBridged("addToDisplay", "-> OH ISceneSessionManager.CreateAndConnectSpecificSession");
        rememberAttrs(window, attrs);

        String windowName = attrs.getTitle() != null ? attrs.getTitle().toString() : "AndroidWindow";
        int windowType = attrs.type;
        // 2026-05-11 G2.14ak — for MATCH_PARENT / FILL_PARENT (-1) / WRAP_CONTENT (-2)
        // attrs.width/height we must resolve to actual display pixels here, because
        // (a) AOSP IPC carries the raw flag (-1) — server side resolves;
        // (b) we are the binder server (extends IWindowSession.Stub) in this scope;
        // (c) OH IWindowManager::CreateWindow/AddWindow take rect IN only, no out param
        //     for resolved rect (verified via oh_window_manager_client.cpp + OH 7.0.0.18
        //     headers).  Source of truth = DisplayManagerGlobal → our OH_DisplayMgrAdapter
        //     → OH NativeDisplayManager, which returns DAYU200 native 720×1280.
        // Old code hardcoded 1080×2340 fallback (dayu210 residue); that propagated through
        // sessionInfo → relayout → SurfaceControl.setBufferSize → hwui drew at 1080×2340
        // coordinates → frame buffer mostly-white because content was outside 720×1280
        // composited region.
        int[] dispWH = getDefaultDisplaySize();
        int width = attrs.width > 0 ? attrs.width : dispWH[0];
        int height = attrs.height > 0 ? attrs.height : dispWH[1];

        // §3.1.4.2 — fetch real bundleName/abilityName from ActivityThread so
        // OH SCB routes window decoration / theme correctly. Native side
        // applies safe fallbacks ("entry" / "MainAbility") if any are blank.
        String bundleName  = currentPackageName();
        String abilityName = currentActivityName();
        String moduleName  = "entry";  // HAP default; P3: derive from APK manifest

        // §3.1.5.6.1 P1 — close the token 流转断点. attrs.token is the Activity's
        // adapter-side IBinder set by OhTokenRegistry on ScheduleLaunchAbility;
        // findOhToken reverses it to the OH AbilityRecord token raw pointer
        // which is what SCB CreateAndConnectSpecificSession expects in its
        // token IRemoteObject param. 0 = no token (P1 fallback).
        long ohTokenAddr = 0L;
        if (attrs.token != null) {
            Long addr = adapter.core.OhTokenRegistry.findOhToken(attrs.token);
            if (addr != null) ohTokenAddr = addr;
        }

        // §3.1.5.6.3 P2 — reuse OH SCB-auto-created main SceneSession when
        // available, instead of creating a duplicate via CreateAndConnect.
        // The capture happens in SessionStageAdapter (when SCB binds an
        // already-created main session to our stage) and writes into
        // OhTokenRegistry.setMainSessionId. If no captured session,
        // fall through to the normal CreateAndConnect path.
        // NOTE: capture wiring is currently API-only (registry surface is in
        // place) — concrete trigger awaits an OH-side mechanism to expose
        // the auto-created main session's persistentId to our SessionStage.
        // Until then, getMainSessionId returns -1 and we always go to create.
        if (isMainAppWindow(windowType) && attrs.token != null) {
            int existing = adapter.core.OhTokenRegistry.getMainSessionId(attrs.token);
            if (existing > 0) {
                Log.i(TAG, "addToDisplay: reusing existing OH main SceneSession id="
                        + existing + " (skipping CreateAndConnect)");
                int[] reuseInfo = new int[] { existing, -1, layerStackId, width, height, 0 };
                mSessionMap.put(window.asBinder(), reuseInfo);
                if (windowType == WindowManager.LayoutParams.TYPE_BASE_APPLICATION) {
                    baseWindowAdded(window.asBinder(), attrs.token);
                }
                if (outInputChannel != null) {
                    InputChannel clientChannel = InputEventBridge.getInstance()
                            .createInputChannelPair(window.asBinder(), existing, windowName);
                    clientChannel.copyTo(outInputChannel);
                }
                OhInsetsBridge.copyForWindow(window, insetsState);
                if (sizeCompatScale != null && sizeCompatScale.length > 0) sizeCompatScale[0] = 1.0f;
                return buildAddDisplayFlags(attrs);  // ADD_FLAG_APP_VISIBLE | IN_TOUCH_MODE
            }
        }

        // A dim layer is created right before the window it dims and must stay right under it.
        boolean dimLayer = attrs.getTitle() != null && attrs.getTitle().toString().startsWith("DimLayer:");
        if (!dimLayer && shouldHoldBack(windowType, attrs.token)) {
            return holdBack(new HeldBackWindow(new HeldBackWindow(window, attrs.token, bundleName,
                    abilityName, moduleName, windowName, windowType, layerStackId, width, height,
                    ohTokenAddr), dimAmountOf(attrs)),
                    attrs, outInputChannel, insetsState, attachedFrame, sizeCompatScale);
        }

        addDimLayer(window.asBinder(), attrs.token, dimAmountOf(attrs), windowName);
        System.err.println("[OH_WSA] PRE-native nativeCreateSession");
        int[] sessionInfo;
        try {
            boolean sub = isSubWindow(windowType);
            sessionInfo = nativeCreateSession(window.asBinder(),
                    bundleName, abilityName, moduleName, windowName,
                    sessionTypeFor(windowType), layerStackId,
                    sub ? dispWH[0] : width, sub ? dispWH[1] : height,
                    ohTokenAddr);
            System.err.println("[OH_WSA] POST-native sessionInfo length=" + (sessionInfo == null ? -1 : sessionInfo.length));
        } catch (Throwable t) {
            System.err.println("[OH_WSA] nativeCreateSession threw " + t);
            t.printStackTrace(System.err);
            throw t;
        }

        // §3.1.5.6.2 — semantic error code mapping. nativeCreateSession returns
        // int[6]={sessionId, surfaceNodeId, displayId, w, h, wsErr} — wsErr is
        // the raw OH WSError on failure. Map each to the AOSP ADD_* code that
        // most closely reflects the cause; avoid the pre-fix blanket
        // ADD_BAD_APP_TOKEN which masquerades as a token problem.
        if (sessionInfo == null || sessionInfo.length < 6) {
            Log.e(TAG, "nativeCreateSession returned malformed array");
            return android.view.WindowManagerGlobal.ADD_INVALID_DISPLAY;  // -3, JNI fault
        }
        if (sessionInfo[0] < 0) {
            int wsErr = sessionInfo[5];
            int addCode = mapWsErrorToAddCode(wsErr);
            Log.e(TAG, "Failed to create OH session, wsErr=" + wsErr
                    + " -> addCode=" + addCode);
            return addCode;
        }

        // G2.14d — instrument with System.err so logs appear via [stderr]
        // tag (Log.i with TAG=OH_WSAdapter inexplicably hides post-construction
        // calls in this build's hilog; stderr always survives). Each step
        // identifies which line throws if ViewRootImpl post-add code or
        // adapter post-success code triggers preallocated NCDFE.
        System.err.println("[OH_WSA] step1 native returned sessionId=" + sessionInfo[0]
                + " surfaceNode=" + sessionInfo[1] + " size="
                + sessionInfo[3] + "x" + sessionInfo[4]);
        mSessionMap.put(window.asBinder(), sessionInfo);
        System.err.println("[OH_WSA] step2 mSessionMap.put OK");
        if (windowType == WindowManager.LayoutParams.TYPE_BASE_APPLICATION && attrs.token != null) {
            baseWindowAdded(window.asBinder(), attrs.token);
        }

        if (outInputChannel != null) {
            try {
                InputChannel clientChannel = InputEventBridge.getInstance()
                        .createInputChannelPair(window.asBinder(), sessionInfo[0], windowName);
                System.err.println("[OH_WSA] step3a createInputChannelPair OK");
                clientChannel.copyTo(outInputChannel);
                System.err.println("[OH_WSA] step3b InputChannel.copyTo OK");
            } catch (Throwable t) {
                System.err.println("[OH_WSA] step3 FAIL " + t);
                t.printStackTrace(System.err);
                throw t;
            }
        } else {
            System.err.println("[OH_WSA] step3 SKIP outInputChannel=null");
        }

        if (insetsState != null) {
            try {
                OhInsetsBridge.copyForWindow(window, insetsState);
                System.err.println("[OH_WSA] step4 insetsState copied OK");
            } catch (Throwable t) {
                System.err.println("[OH_WSA] step4 FAIL " + t);
                t.printStackTrace(System.err);
                throw t;
            }
        } else {
            System.err.println("[OH_WSA] step4 SKIP insetsState=null");
        }

        if (sizeCompatScale != null && sizeCompatScale.length > 0) {
            sizeCompatScale[0] = 1.0f;
            System.err.println("[OH_WSA] step5 sizeCompatScale[0]=1.0f OK");
        }

        if (attachedFrame != null) {
            try {
                // 2026-05-11 G2.14ak — same dayu210 hardcoded fallback removed here.
                // sessionInfo[3]/[4] come from nativeCreateSession which now gets correct
                // values from the addToDisplay path above; fallback uses Display.
                int[] _wh = getDefaultDisplaySize();
                attachedFrame.set(0, 0,
                        sessionInfo[3] > 0 ? sessionInfo[3] : _wh[0],
                        sessionInfo[4] > 0 ? sessionInfo[4] : _wh[1]);
                System.err.println("[OH_WSA] step6 attachedFrame.set OK");
            } catch (Throwable t) {
                System.err.println("[OH_WSA] step6 FAIL " + t);
                t.printStackTrace(System.err);
                throw t;
            }
        }

        int flags = buildAddDisplayFlags(attrs);
        System.err.println("[OH_WSA] step7 returning addFlags=0x" + Integer.toHexString(flags));
        return flags;
    }

    /**
     * Build the bitfield return value for addToDisplay/addToDisplayAsUser per
     * AOSP IWindowSession protocol. Mirrors AOSP WindowManagerService.addWindow
     * (line 1797-1799 frameworks/base/services/.../WindowManagerService.java):
     *
     *   if (mActivityRecord == null || mActivityRecord.isClientVisible()) {
     *       res |= ADD_FLAG_APP_VISIBLE;
     *   }
     *   res |= ADD_FLAG_IN_TOUCH_MODE  (when display is in touch mode)
     *
     * adapter mapping (legacy WMS / SCB both apply since this is the AOSP
     * client-side protocol, independent of OH internal routing):
     *
     *   ADD_FLAG_APP_VISIBLE — set when (a) attrs.token == null (system window,
     *      AOSP isClientVisible() returns true for null mActivityRecord), or
     *      (b) attrs.token registered in OhTokenRegistry (ability is in
     *      launching/resuming, AOSP equivalent of mActivityRecord.isClientVisible())
     *   ADD_FLAG_IN_TOUCH_MODE — always set (DAYU200 is touch device)
     *   ADD_FLAG_USE_BLAST — NOT set (BLAST adapter incompatible with OH SurfaceControl)
     *   ADD_FLAG_ALWAYS_CONSUME_SYSTEM_BARS — NOT set (left for future)
     *
     * Without ADD_FLAG_APP_VISIBLE, ViewRootImpl initializes mAppVisible=false
     * (line 1469 ~/aosp/.../ViewRootImpl.java), getHostVisibility() returns
     * View.GONE, relayoutWindow vis=false, RenderThread does not draw frames,
     * helloworld window never appears on screen even though AMS state is
     * FOREGROUND. Verified G2.14y via hitrace.
     *
     * 2026-05-08 G2.14ag — ADD_FLAG_USE_BLAST attempt reverted:
     *   Initial hypothesis was that setting ADD_FLAG_USE_BLAST would route
     *   ViewRootImpl through BBQ_nativeGetSurface so mSurface.mNativeObject
     *   gets a real OHNativeWindow*. Implemented + deployed; helloworld cold
     *   start regressed to LIFECYCLE_TIMEOUT before reaching setView at all
     *   (onCreate→onResume gap independent of this flag). Reverted to keep
     *   diagnostic baseline; the onCreate→onResume gap is the actual blocker
     *   and has nothing to do with the BLAST gating in ViewRootImpl.
     */
    private int buildAddDisplayFlags(WindowManager.LayoutParams attrs) {
        int flags = android.view.WindowManagerGlobal.ADD_FLAG_IN_TOUCH_MODE;
        boolean isSystemWindow = (attrs == null || attrs.token == null);
        boolean isClientVisible = isSystemWindow
                || adapter.core.OhTokenRegistry.findOhToken(attrs.token) != null;
        if (isClientVisible) {
            flags |= android.view.WindowManagerGlobal.ADD_FLAG_APP_VISIBLE;
        }
        return flags;
    }

    /**
     * [BRIDGED] addToDisplayAsUser -> OH ISceneSessionManager.CreateAndConnectSpecificSession (with userId)
     */
    @Override
    public int addToDisplayAsUser(IWindow window, WindowManager.LayoutParams attrs,
            int viewVisibility, int layerStackId, int userId, int requestedVisibleTypes,
            InputChannel outInputChannel, InsetsState insetsState,
            InsetsSourceControl.Array activeControls, Rect attachedFrame,
            float[] sizeCompatScale) throws RemoteException {
        System.err.println("[OH_WSA] ENTRY addToDisplayAsUser userId=" + userId
                + " attrs.type=" + attrs.type
                + " flags=0x" + Integer.toHexString(attrs.flags)
                + " (HW_ACCEL=" + ((attrs.flags
                        & WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED) != 0)
                + ")");
        logBridged("addToDisplayAsUser", "-> OH ISceneSessionManager.CreateAndConnectSpecificSession");
        try {
            int r = addToDisplay(window, attrs, viewVisibility, layerStackId,
                    requestedVisibleTypes, outInputChannel, insetsState,
                    activeControls, attachedFrame, sizeCompatScale);
            System.err.println("[OH_WSA] addToDisplayAsUser delegate returned " + r);
            return r;
        } catch (Throwable t) {
            System.err.println("[OH_WSA] addToDisplayAsUser delegate threw " + t);
            t.printStackTrace(System.err);
            throw t;
        }
    }

    /**
     * [BRIDGED] addToDisplayWithoutInputChannel -> OH ISession (no input channel variant)
     */
    @Override
    public int addToDisplayWithoutInputChannel(IWindow window, WindowManager.LayoutParams attrs,
            int viewVisibility, int layerStackId, InsetsState insetsState,
            Rect attachedFrame, float[] sizeCompatScale) throws RemoteException {
        logBridged("addToDisplayWithoutInputChannel", "-> OH ISession (no input channel variant)");
        // TODO: Phase 2 - call native bridge without input channel
        return 0;
    }

    /**
     * [BRIDGED] remove -> OH ISession.Disconnect + ISceneSessionManager.DestroySession
     */

    public void remove(IWindow window) throws RemoteException {
        synchronized (mHeldBack) {
            mHeldBack.remove(window.asBinder());
            IBinder token = mBaseWindowTokens.remove(window.asBinder());
            if (token != null) mTokensWithBase.remove(token);
        }
        removeDimLayer(window.asBinder());
        synchronized (mWindowAttrs) {
            mWindowAttrs.remove(window.asBinder());
        }
        OhWindowPolicy.remove(window);
        OhInsetsBridge.remove(window);
        logBridged("remove", "-> OH ISession.Disconnect");
        // Window focus moves to the window under this one, as WMS moves it.
        WindowCallbackBridge.windowRemoved(window);

        // Clean up InputChannel
        InputEventBridge.getInstance().destroyInputChannel(window.asBinder());

        int[] sessionInfo = mSessionMap.remove(window.asBinder());
        if (sessionInfo != null) {
            int sessionId = sessionInfo[0];
            // Destroy OH surface resources (RSSurfaceNode, RSUIDirector, OHGraphicBufferProducer)
            nativeDestroyOHSurface(sessionId);
            // Destroy OH window session (ISession.Disconnect + SSM.DestroyAndDisconnect)
            // 2026-05-11 G2.14am Bug B note: nativeDestroySession internally calls
            // IWindowManager::DestroyWindow which triggers
            // WindowNodeContainer::DestroyWindowNode → StartingWindow::Release
            // StartWinSurfaceNode (OH window_node_container.cpp:626) — leashWindow +
            // startingWindow cleanup happens in this path for graceful Activity
            // destroy.  For aa force-stop kill (process death without remove()
            // invocation), cleanup relies on OH foundation's REMOTE_DIED
            // handler.  No additional adapter-side work needed here; OH-side
            // patch (ohos_patches/) is the right venue for force-stop fix.
            nativeDestroySession(sessionId);
            Log.i(TAG, "OH session destroyed: id=" + sessionId);
        } else {
            Log.w(TAG, "remove: no session found for window");
        }
    }

    // ====================================================================
    // Category 2: Window Layout
    // ====================================================================

    /**
     * [BRIDGED] relayout -> OH ISession.UpdateSessionRect + UpdateSizeChangeReason
     */

    @Override
    public int relayoutLegacy(IWindow window, WindowManager.LayoutParams attrs,
            int requestedWidth, int requestedHeight, int viewVisibility,
            int flags, int seq, int lastSyncSeqId, ClientWindowFrames outFrames,
            MergedConfiguration outMergedConfiguration, SurfaceControl outSurfaceControl,
            InsetsState insetsState, InsetsSourceControl.Array activeControls,
            Bundle bundle) throws RemoteException {
        logBridged("relayout", "-> OH ISession.UpdateSessionRect");

        final WindowManager.LayoutParams windowAttrs = rememberAttrs(window, attrs);
        HeldBackWindow held;
        synchronized (mHeldBack) {
            held = mHeldBack.get(window.asBinder());
        }
        if (held != null) {
            // What WMS does for a window whose activity is not visible yet: frames, no surface.
            int[] display = getDefaultDisplaySize();
            int width = resolveRelayoutDimension(attrs != null ? attrs.width : 0, requestedWidth, display[0]);
            int height = resolveRelayoutDimension(attrs != null ? attrs.height : 0, requestedHeight, display[1]);
            held.lastWidth = width;
            held.lastHeight = height;
            if (outFrames != null) {
                outFrames.frame.set(placeWindow(window, windowAttrs, width, height));
                outFrames.displayFrame.set(0, 0, display[0], display[1]);
                outFrames.parentFrame.set(0, 0, display[0], display[1]);
            }
            fillMergedConfiguration(outMergedConfiguration, width, height);
            if (insetsState != null) OhInsetsBridge.copyForWindow(window, insetsState);
            System.err.println("[OH_WSA-relayout] held back " + width + "x" + height
                    + ": no surface until its activity's window has an OH session");
            return 0;
        }

        int[] sessionInfo = mSessionMap.get(window.asBinder());
        if (sessionInfo == null) {
            Log.e(TAG, "relayout: no session found for window");
            return -1;
        }

        int sessionId = sessionInfo[0];
        // §537 (surfaceInsets must be zeroed) is done natively in
        // oh_window_manager_client.cpp::wl_zero_surface_insets, called from
        // nativeGetSurfaceNodeId_impl below — this jar is built by a separate pipeline
        // (build_adapter_runtime_bcp.sh) that is not part of the arm64 bridge build.
        // AOSP WMS resolves MATCH_PARENT from LayoutParams against the display/window
        // bounds; requestedWidth/requestedHeight are only the client's latest measured
        // dimensions.  Treating every positive requested dimension as authoritative lets
        // a transient 80x80 bootstrap measurement permanently shrink a newly launched
        // full-screen Activity (observed when opening Toutiao search results).  Preserve
        // measured dimensions for WRAP_CONTENT/fixed-size windows, but resolve
        // MATCH_PARENT at this WMS boundary from the session's display-sized frame.
        OhWindowPolicy.update(this, window, attrs, requestedWidth, requestedHeight, viewVisibility);
        int width = resolveRelayoutDimension(
                attrs != null ? attrs.width : 0, requestedWidth, sessionInfo[3]);
        int height = resolveRelayoutDimension(
                attrs != null ? attrs.height : 0, requestedHeight, sessionInfo[4]);

        // G2.14as r3 — log actual width/height that relayout will use
        System.err.println("[OH_WSA-relayout] requestedWH=" + requestedWidth + "x" + requestedHeight
                + " sessionInfo[3,4]=" + sessionInfo[3] + "x" + sessionInfo[4]
                + " -> useWH=" + width + "x" + height
                + " visibility=" + viewVisibility);

        // 2026-05-19: visibility transition — when AOSP signals INVISIBLE/GONE,
        // hide the WMS window so launcher can claim foreground; when VISIBLE
        // returns, re-show.  Idempotent at C++ layer (wmsShown cache on
        // SessionEntry), so calling every relayout is safe and 0 extra IPC.
        // Without this, helloworld stays z-order top after home-key press.
        // See android.view.View constants: VISIBLE=0, INVISIBLE=4, GONE=8.
        if (viewVisibility == android.view.View.VISIBLE) {
            int rcShow = nativeShowWindow(sessionId);
            if (rcShow != 0) {
                System.err.println("[OH_WSA-relayout] nativeShowWindow session=" + sessionId
                        + " rc=" + rcShow);
            }
        } else {
            int rcHide = nativeHideWindow(sessionId);
            if (rcHide != 0) {
                System.err.println("[OH_WSA-relayout] nativeHideWindow session=" + sessionId
                        + " rc=" + rcHide);
            }
        }

        // Update OH session rect
        nativeUpdateSessionRect(sessionId, 0, 0, width, height);

        // Ensure OH surface bridge is created for this session
        String windowName = "OH_Surface_" + sessionId;
        int pixelFormat = (attrs != null) ? attrs.format : 1; // default RGBA_8888
        nativeCreateOHSurface(sessionId, windowName, width, height, pixelFormat);

        // Get (or create) the OHGraphicBufferProducer handle
        long surfaceHandle = nativeGetSurfaceHandle(sessionId, width, height, pixelFormat);
        if (surfaceHandle != 0) {
            Log.i(TAG, "relayout: OH surface handle=0x" + Long.toHexString(surfaceHandle)
                    + " for session " + sessionId);
        } else {
            Log.e(TAG, "relayout: Failed to get OH surface handle for session " + sessionId);
        }

        // Update surface size if dimensions changed
        nativeUpdateSurfaceSize(sessionId, width, height);
        final Rect placed = placeWindow(window, windowAttrs, width, height);
        setDimOwnerVisible(window.asBinder(), viewVisibility == android.view.View.VISIBLE);

        // Populate output frames
        if (outFrames != null) {
            Rect frame = new Rect(placed);
            outFrames.frame.set(frame);
            if (placed.left != 0 || placed.top != 0) {
                int[] display = getDefaultDisplaySize();
                outFrames.displayFrame.set(0, 0, display[0], display[1]);
                outFrames.parentFrame.set(0, 0, display[0], display[1]);
            } else {
                outFrames.displayFrame.set(frame);
                outFrames.parentFrame.set(frame);
            }
            // G2.14as r3 — verify outFrames really populated
            System.err.println("[OH_WSA-relayout] outFrames populated: frame=" + outFrames.frame
                    + " displayFrame=" + outFrames.displayFrame
                    + " parentFrame=" + outFrames.parentFrame);
        } else {
            System.err.println("[OH_WSA-relayout] outFrames == null !!");
        }

        // 2026-05-11 G2.14as r4 — reverse-push frame to ViewRootImpl via IWindow.resized.
        // AOSP WMS pushes frame to client via IWindow.resized (not just relayout
        // return).  ViewRootImpl in LOCAL_LAYOUT mode computeFrames(measuredWidth=0,
        // measuredHeight=0) → frame=(0,0,0,0) → relayoutAsync=true → outFrames
        // not returned by oneway path → mTmpFrames.frame stays (0,0,0,0) forever
        // (death spiral: 0 → measure 0 → relayout 0).  IWindow.resized breaks the
        // spiral by reverse-pushing the real frame, which ViewRootImpl.handleResized
        // calls setFrame(frame, false) → mWinFrame=(0,0,720,1280) → next measure
        // uses real size.  Adapter must mimic this path since OH server doesn't
        // know to call it.
        // [G2.8-RELAYOUT-LOOP] only reverse-push ONCE per window (bootstrap). Re-pushing every
        // relayout re-triggers a ViewRootImpl traversal → infinite loop (see field decl).
        if (!mReversePushed.contains(window.asBinder())) {
            try {
                ClientWindowFrames pushedFrames = new ClientWindowFrames();
                pushedFrames.frame.set(placed);
                pushedFrames.displayFrame.set(0, 0, width, height);
                pushedFrames.parentFrame.set(0, 0, width, height);
                pushedFrames.attachedFrame = new Rect(0, 0, width, height);
                android.util.MergedConfiguration cfg = new android.util.MergedConfiguration();
                // empty global+override config is fine — Activity already created with
                // real config; this is just to satisfy the IWindow.resized signature.
                InsetsState pushedInsets = new InsetsState();
                OhInsetsBridge.copyForWindow(window, pushedInsets);
                // displayId for default display
                int displayId = 0;
                window.resized(pushedFrames, false /* reportDraw */, cfg, pushedInsets,
                        true /* forceLayout */, false /* alwaysConsumeSystemBars */,
                        displayId, 0 /* syncSeqId */, false /* dragResizing */, null);
                mReversePushed.add(window.asBinder());
                System.err.println("[OH_WSA-relayout] reverse-pushed via IWindow.resized (once): "
                        + width + "x" + height);
            } catch (Throwable t) {
                System.err.println("[OH_WSA-relayout] IWindow.resized reverse-push failed: " + t);
            }
        } else {
            System.err.println("[G2.8] reverse-push SKIPPED (already bootstrapped "
                    + width + "x" + height + ") -- breaking relayout loop");
        }

        // Populate output MergedConfiguration with defaults
        fillMergedConfiguration(outMergedConfiguration, width, height);

        // Create a SurfaceControl backed by the OH RSSurfaceNode.  2026-05-02
        // G2.14r: outSurfaceControl is now stamped with sessionId via
        // nativeAttachSessionToSc so the downstream BLASTBufferQueue can resolve
        // it back to the OH RSSurfaceNode (via OHWindowManagerClient.getOhNativeWindow)
        // and produce a real OHNativeWindow* for hwui's eglCreateWindowSurface.
        // Without this step, BBQ_nativeGetSurface returns nullptr and the whole
        // hwui render path runs on an empty Surface (mNativeObject==0), causing
        // the post-activityResumed SIGABRT.  See doc/graphics_rendering_design
        // appendix G2.14r for the full causal chain.
        if (outSurfaceControl != null) {
            long surfaceNodeId = nativeGetSurfaceNodeId(sessionId);
            try {
                // [G2.8b-SC-CACHE 2026-06-01] Build the SurfaceControl ONCE per window and reuse
                // it on every subsequent relayout. Creating a new SurfaceControl each call makes
                // ViewRootImpl see a changed surface → hwui destroys+recreates its EGLSurface →
                // the recreate fails (cannot bind a 2nd window surface to the same OH producer) →
                // libhwui abort before frame #1 draws. Reusing the same SC keeps hwui's first
                // (now-successful, post-G3.4b format/usage fix) EGL surface alive so it can draw.
                SurfaceControl sc = mSurfaceControls.get(window.asBinder());
                if (sc == null) {
                    SurfaceControl.Builder builder = new SurfaceControl.Builder()
                            .setName(windowName)
                            .setBufferSize(width, height);
                    sc = builder.build();
                    mSurfaceControls.put(window.asBinder(), sc);
                    Log.i(TAG, "relayout: created SurfaceControl ONCE (G2.8b) surfaceNodeId="
                            + surfaceNodeId + " producerHandle=0x" + Long.toHexString(surfaceHandle));
                } else {
                    Log.i(TAG, "relayout: reusing cached SurfaceControl (G2.8b) for session " + sessionId);
                }
                outSurfaceControl.copyFrom(sc, "OH_relayout");
                // [G3.6-MULTIWINDOW 2026-06-02] Stamp THIS window's session onto the SC hwui
                // will render to, so sc_to_oh_native_window resolves it to THIS window's OH
                // NativeWindow instead of the thread-local last-attached session. Without it,
                // noice's 2nd window (AppIntro vs Main) collided on the 1st window's already-
                // EGL-bound nw -> 2nd eglCreateWindowSurface failed -> libhwui abort.
                nativeAttachSessionToSc(outSurfaceControl, sessionId);
                applyPosition(outSurfaceControl, placed);

                // G2.14r: instead of adding a new BCP native method (which
                // requires boot image rebuild), the session ID is communicated
                // to BLASTBufferQueue via OHWindowManagerClient's "last
                // attached session" thread-local — set in nativeUpdateSessionRect
                // (already called above for this session).  BBQ_nativeUpdate
                // reads from there if SurfaceControl has no session attached.
            } catch (Exception e) {
                Log.e(TAG, "Failed to create SurfaceControl", e);
            }
        }

        // Populate InsetsState
        if (insetsState != null) {
            OhInsetsBridge.copyForWindow(window, insetsState);
        }

        return 0;
    }

    private static void fillMergedConfiguration(MergedConfiguration outMergedConfiguration,
            int width, int height) {
        if (outMergedConfiguration == null) return;
        // 2026-05-11 G2.14as r6 — populate WindowConfiguration.mBounds.
        // ViewRootImpl.relayoutWindow in LOCAL_LAYOUT mode calls
        //   mWindowLayout.computeFrames(... winConfig.getBounds(),
        //                                measuredWidth, measuredHeight, ..., mTmpFrames)
        // to compute mTmpFrames.frame.  Without bounds in winConfig + with
        // measuredWidth/Height=0 (first traversal before any successful
        // measure), computeFrames returns frame=(0,0,0,0) → relayoutAsync=true
        // → outFrames not propagated → mWinFrame stays 0×0 → next measure
        // still 0 → death spiral.  Populating winConfig.mBounds breaks the
        // spiral.
        //
        // The bounds are the display's, as WMS gives every window of an activity its activity's
        // configuration, not the window's own size. ViewRootImpl lays its window out locally inside
        // these bounds, by its gravity and x/y (WindowLayout, as placeWindow does here), and takes
        // the frame as its window's position. Inside its own size a centred dialog came out at
        // (0,0): the app was told its dialog sat at the top left (getLocationOnScreen) while it was
        // drawn centred, and a popup anchored in that dialog would open in the wrong place.
        int[] display = getDefaultDisplaySize();
        Configuration config = sizedConfiguration(display[0], display[1]);
        outMergedConfiguration.setOverrideConfiguration(config);
        // The global side is the display's, as WMS reports it for every window. ViewRootImpl hands
        // it to ActivityThread's config callback, which applies it to the process's resources, and
        // a context's Display and WindowManager metrics read their size from those resources'
        // window bounds. Set from this window, a smaller window shrank the app's whole idea of the
        // screen: Draw Anywhere's 180x180 overlay button made the display 180 px wide, and placing
        // its 640 px toolbar threw "Cannot coerce value to an empty range: maximum -460".
        Configuration globalConfig = sizedConfiguration(display[0], display[1]);
        outMergedConfiguration.setGlobalConfiguration(globalConfig);
        System.err.println("[OH_WSA-relayout] outMergedConfiguration set: window=" + width + "x" + height
                + " winConfig.mBounds=" + config.windowConfiguration.getBounds()
                + " screenDp=" + config.screenWidthDp + "x" + config.screenHeightDp
                + " densityDpi=" + config.densityDpi
                + " global=" + globalConfig.screenWidthDp + "x" + globalConfig.screenHeightDp);
    }

    /** A configuration whose screen and window bounds are width x height pixels. */
    private static Configuration sizedConfiguration(int width, int height) {
        Configuration config = new Configuration();
        config.screenWidthDp = width * 160 / 320; // approximate dp conversion
        config.screenHeightDp = height * 160 / 320;
        config.densityDpi = 320;
        Rect bounds = new Rect(0, 0, width, height);
        config.windowConfiguration.setBounds(bounds);
        config.windowConfiguration.setAppBounds(bounds);
        config.windowConfiguration.setMaxBounds(bounds);
        return config;
    }

    /**
     * [BRIDGED] relayoutAsync -> OH ISession.UpdateSessionRect (async, oneway)
     */
    @Override
    public void relayoutAsync(IWindow window, WindowManager.LayoutParams attrs,
            int requestedWidth, int requestedHeight, int viewVisibility,
            int flags, int seq, int lastSyncSeqId) throws RemoteException {
        OhWindowPolicy.update(this, window, attrs, requestedWidth, requestedHeight, viewVisibility);
        logBridged("relayoutAsync", "-> OH ISession.UpdateSessionRect (async)");
        // TODO: Phase 2 - call native bridge async
    }

    /**
     * [BRIDGED] outOfMemory -> OH memory pressure notification
     */
    @Override
    public boolean outOfMemory(IWindow window) throws RemoteException {
        logBridged("outOfMemory", "-> OH memory management");
        return false;
    }

    // ====================================================================
    // Category 3: Insets and Drawing
    // ====================================================================

    /**
     * [BRIDGED] setInsets -> OH ISession.SetAvoidArea
     */
    @Override
    public void setInsets(IWindow window, int touchableInsets, Rect contentInsets,
            Rect visibleInsets, Region touchableRegion) throws RemoteException {
        logBridged("setInsets", "-> OH ISession.SetAvoidArea");
        // TODO: Phase 2 - call native bridge to set avoid area
    }

    /**
     * [BRIDGED] finishDrawing -> OH ISessionStage.NotifyDrawingCompleted
     */
    @Override
    public void finishDrawing(IWindow window, SurfaceControl.Transaction postDrawTransaction,
            int seqId) throws RemoteException {
        logBridged("finishDrawing", "-> OH ISessionStage.NotifyDrawingCompleted");

        int[] sessionInfo = mSessionMap.get(window.asBinder());
        if (sessionInfo != null) {
            int sessionId = sessionInfo[0];
            // Flush RSUIDirector::SendMessages() to commit render instructions to RenderService
            nativeNotifySurfaceDrawingCompleted(sessionId);
            // Notify OH ISession that drawing is completed
            nativeNotifyDrawingCompleted(sessionId);
        } else {
            Log.w(TAG, "finishDrawing: no session found for window");
        }
    }

    /**
     * [STUB] clearTouchableRegion - no direct OH equivalent
     */
    @Override
    public void clearTouchableRegion(IWindow window) throws RemoteException {
        logStub("clearTouchableRegion", "no direct OH equivalent");
    }

    /**
     * [STUB] cancelDraw - returns false (do not cancel)
     */
    @Override
    public boolean cancelDraw(IWindow window) throws RemoteException {
        logStub("cancelDraw", "no OH equivalent, allowing draw");
        return false;
    }

    // ====================================================================
    // Category 4: Haptic Feedback
    // ====================================================================

    /**
     * [STUB] performHapticFeedback - OH handles haptics differently
     */

    public boolean performHapticFeedback(int effectId, boolean always) throws RemoteException {
        logStub("performHapticFeedback", "OH haptic framework not mapped");
        return false;
    }

    /**
     * [STUB] performHapticFeedbackAsync - OH handles haptics differently (oneway)
     */

    public void performHapticFeedbackAsync(int effectId, boolean always) throws RemoteException {
        logStub("performHapticFeedbackAsync", "OH haptic framework not mapped");
    }

    // ====================================================================
    // Category 5: Drag and Drop
    // ====================================================================

    /**
     * [BRIDGED] performDrag -> OH drag and drop framework
     */

    public IBinder performDrag(IWindow window, int flags, SurfaceControl surface,
            int touchSource, float touchX, float touchY, float thumbCenterX,
            float thumbCenterY, ClipData data) throws RemoteException {
        logBridged("performDrag", "-> OH drag framework");
        // TODO: Phase 2 - call native drag start
        return null;
    }

    /**
     * [STUB] dropForAccessibility - no OH equivalent
     */
    @Override
    public boolean dropForAccessibility(IWindow window, int x, int y) throws RemoteException {
        logStub("dropForAccessibility", "no OH equivalent");
        return false;
    }

    /**
     * [BRIDGED] reportDropResult -> OH drag result notification (oneway)
     */
    @Override
    public void reportDropResult(IWindow window, boolean consumed) throws RemoteException {
        logBridged("reportDropResult", "-> OH drag result notification");
        // TODO: Phase 2 - call native report drop result
    }

    /**
     * [BRIDGED] cancelDragAndDrop -> OH cancel drag (oneway)
     */
    @Override
    public void cancelDragAndDrop(IBinder dragToken, boolean skipAnimation) throws RemoteException {
        logBridged("cancelDragAndDrop", "-> OH cancel drag");
        // TODO: Phase 2 - call native cancel drag
    }

    /**
     * [BRIDGED] dragRecipientEntered -> OH drag recipient notification (oneway)
     */
    @Override
    public void dragRecipientEntered(IWindow window) throws RemoteException {
        logBridged("dragRecipientEntered", "-> OH drag recipient entered");
    }

    /**
     * [BRIDGED] dragRecipientExited -> OH drag recipient notification (oneway)
     */
    @Override
    public void dragRecipientExited(IWindow window) throws RemoteException {
        logBridged("dragRecipientExited", "-> OH drag recipient exited");
    }

    // ====================================================================
    // Category 6: Wallpaper
    // ====================================================================

    /**
     * [STUB] setWallpaperPosition - OH wallpaper system differs (oneway)
     */
    @Override
    public void setWallpaperPosition(IBinder windowToken, float x, float y,
            float xstep, float ystep) throws RemoteException {
        logStub("setWallpaperPosition", "OH wallpaper position not mapped");
    }

    /**
     * [STUB] setWallpaperZoomOut - OH wallpaper system differs (oneway)
     */
    @Override
    public void setWallpaperZoomOut(IBinder windowToken, float scale) throws RemoteException {
        logStub("setWallpaperZoomOut", "OH wallpaper zoom not mapped");
    }

    /**
     * [STUB] setShouldZoomOutWallpaper - OH wallpaper system differs (oneway)
     */
    @Override
    public void setShouldZoomOutWallpaper(IBinder windowToken, boolean shouldZoom)
            throws RemoteException {
        logStub("setShouldZoomOutWallpaper", "OH wallpaper zoom not mapped");
    }

    /**
     * [STUB] wallpaperOffsetsComplete - OH wallpaper system differs (oneway)
     */
    @Override
    public void wallpaperOffsetsComplete(IBinder window) throws RemoteException {
        logStub("wallpaperOffsetsComplete", "OH wallpaper not mapped");
    }

    /**
     * [STUB] setWallpaperDisplayOffset - OH wallpaper system differs (oneway)
     */
    @Override
    public void setWallpaperDisplayOffset(IBinder windowToken, int x, int y)
            throws RemoteException {
        logStub("setWallpaperDisplayOffset", "OH wallpaper not mapped");
    }

    /**
     * [STUB] sendWallpaperCommand - OH wallpaper system differs
     */

    @Override
    public void sendWallpaperCommand(IBinder window, String action, int x, int y,
            int z, Bundle extras, boolean sync) throws RemoteException {
        logStub("sendWallpaperCommand", "OH wallpaper command not mapped");
    }

    /**
     * [STUB] wallpaperCommandComplete - OH wallpaper system differs (oneway)
     */
    @Override
    public void wallpaperCommandComplete(IBinder window, Bundle result) throws RemoteException {
        logStub("wallpaperCommandComplete", "OH wallpaper not mapped");
    }

    // ====================================================================
    // Category 7: Input and Pointer
    // ====================================================================

    /**
     * [BRIDGED] updatePointerIcon -> OH input pointer icon (oneway)
     */

    public void updatePointerIcon(IWindow window) throws RemoteException {
        logBridged("updatePointerIcon", "-> OH input pointer icon");
        // TODO: Phase 2 - call native pointer icon update
    }

    /**
     * [BRIDGED] updateTapExcludeRegion -> OH ISession tap exclude (oneway)
     */
    @Override
    public void updateTapExcludeRegion(IWindow window, Region region) throws RemoteException {
        logBridged("updateTapExcludeRegion", "-> OH ISession tap exclude");
        // TODO: Phase 2 - call native tap exclude update
    }

    /**
     * [BRIDGED] updateRequestedVisibleTypes -> OH ISession visible types (oneway)
     */
    @Override
    public void updateRequestedVisibleTypes(IWindow window, int requestedVisibleTypes)
            throws RemoteException {
        logBridged("updateRequestedVisibleTypes", "-> OH ISession visible types");
        // TODO: Phase 2 - call native visible types update
    }

    /**
     * [BRIDGED] grantInputChannel -> OH ISession input channel
     */

    public void grantInputChannel(int displayId, SurfaceControl surface, IWindow window,
            IBinder hostInputToken, int flags, int privateFlags, int inputFeatures,
            int type, IBinder windowToken, IBinder focusGrantToken, String inputHandleName,
            InputChannel outInputChannel) throws RemoteException {
        logBridged("grantInputChannel", "-> OH ISession input channel");
        // TODO: Phase 2 - call native grant input channel
    }

    /**
     * [BRIDGED] updateInputChannel -> OH input channel update (oneway)
     */
    @Override
    public void updateInputChannel(IBinder channelToken, int displayId,
            SurfaceControl surface, int flags, int privateFlags, int inputFeatures,
            Region region) throws RemoteException {
        logBridged("updateInputChannel", "-> OH input channel update");
        // TODO: Phase 2 - call native update input channel
    }

    // ====================================================================
    // Category 8: Focus and Embedded Windows
    // ====================================================================

    /**
     * [STUB] grantEmbeddedWindowFocus - OH embedded window focus model differs
     */

    public void grantEmbeddedWindowFocus(IWindow window, IBinder inputToken,
            boolean grantFocus) throws RemoteException {
        logStub("grantEmbeddedWindowFocus", "OH embedded window focus not mapped");
    }

    /**
     * [STUB] transferEmbeddedTouchFocusToHost - OH embedded touch model differs
     */

    public boolean transferEmbeddedTouchFocusToHost(IWindow embeddedWindow)
            throws RemoteException {
        logStub("transferEmbeddedTouchFocusToHost", "OH embedded touch focus not mapped");
        return false;
    }

    // ====================================================================
    // Category 9: Task / Window Movement
    // ====================================================================

    /**
     * [STUB] startMovingTask - OH task management differs
     */
    @Override
    public boolean startMovingTask(IWindow window, float startX, float startY)
            throws RemoteException {
        logStub("startMovingTask", "OH task movement not mapped");
        return false;
    }

    /**
     * [STUB] finishMovingTask - OH task management differs (oneway)
     */
    @Override
    public void finishMovingTask(IWindow window) throws RemoteException {
        logStub("finishMovingTask", "OH task movement not mapped");
    }

    // ====================================================================
    // Category 10: System Gesture and Keep-Clear Areas
    // ====================================================================

    /**
     * [STUB] reportSystemGestureExclusionChanged - OH gesture exclusion not mapped (oneway)
     */
    @Override
    public void reportSystemGestureExclusionChanged(IWindow window, List<Rect> exclusionRects)
            throws RemoteException {
        logStub("reportSystemGestureExclusionChanged", "OH gesture exclusion not mapped");
    }

    /**
     * [STUB] reportKeepClearAreasChanged - OH keep-clear areas not mapped (oneway)
     */
    @Override
    public void reportKeepClearAreasChanged(IWindow window, List<Rect> restricted,
            List<Rect> unrestricted) throws RemoteException {
        logStub("reportKeepClearAreasChanged", "OH keep-clear areas not mapped");
    }

    // ====================================================================
    // Category 11: Display Hash and Back Navigation
    // ====================================================================

    /**
     * [STUB] generateDisplayHash - no OH equivalent (oneway)
     */
    @Override
    public void generateDisplayHash(IWindow window, Rect boundsInWindow,
            String hashAlgorithm, RemoteCallback callback) throws RemoteException {
        logStub("generateDisplayHash", "no OH equivalent");
    }

    /**
     * [BRIDGED] setOnBackInvokedCallbackInfo -> OH back navigation (oneway)
     */
    @Override
    public void setOnBackInvokedCallbackInfo(IWindow window,
            OnBackInvokedCallbackInfo callbackInfo) throws RemoteException {
        logBridged("setOnBackInvokedCallbackInfo", "-> OH back navigation callback");
        // TODO: Phase 2 - call native back invocation registration
    }

    // ====================================================================
    // Category 12: Accessibility and Misc
    // ====================================================================

    /**
     * [BRIDGED] onRectangleOnScreenRequested -> OH ISession visible area request (oneway)
     */
    @Override
    public void onRectangleOnScreenRequested(IBinder token, Rect rectangle)
            throws RemoteException {
        logBridged("onRectangleOnScreenRequested", "-> OH ISession visible area request");
        // TODO: Phase 2 - call native rectangle on screen
    }

    /**
     * [STUB] getWindowId - OH accessibility window ID
     */
    @Override
    public IWindowId getWindowId(IBinder window) throws RemoteException {
        logStub("getWindowId", "OH accessibility window ID not mapped");
        return null;
    }

    /**
     * [STUB] pokeDrawLock - OH does not use draw locks
     */
    @Override
    public void pokeDrawLock(IBinder window) throws RemoteException {
        logStub("pokeDrawLock", "OH does not use draw locks");
    }

    // ====================================================================
    // Helper Methods
    // ====================================================================

    /**
     * Logs a bridged method call with its OH target mapping.
     */
    private void logBridged(String method, String target) {
        Log.d(TAG, "[BRIDGED] " + method + " " + target);
    }

    /**
     * Logs a stub method call with the reason it is not bridged.
     */
    private void logStub(String method, String reason) {
        Log.d(TAG, "[STUB] " + method + " - " + reason);
    }

    /**
     * 2026-05-11 G2.14ak — resolve MATCH_PARENT / FILL_PARENT (-1) / WRAP_CONTENT
     * (-2) attrs.width|height to actual display pixels.
     *
     * Why this method exists at this architectural layer:
     *   - AOSP IWindowSession.addToDisplay carries `attrs.width = -1` over binder
     *     (server-side resolves; client never pre-resolves)
     *   - WindowSessionAdapter extends IWindowSession.Stub — IS the binder server
     *     in our adapter scope (no system_server WMS exists between us and OH)
     *   - OH IWindowManager::CreateWindow/AddWindow take rect IN, no out param for
     *     resolved rect (verified oh_window_manager_client.cpp lines 302-320 +
     *     OH 7.0.0.18 IWindowManager.h headers — no GetWindowRect(windowId,&) API)
     *
     * Data source = DisplayManagerGlobal → OH_DisplayMgrAdapter → OH NDM, which
     * already correctly returns 720×1280 on DAYU200 (per existing hilog
     * `OH_DisplayMgrAdapter getDisplayInfo(0) -> 720x1280@69Hz`).
     *
     * Baseline fallback uses DAYU200 (the project's only target board per
     * CLAUDE.md).  An earlier dayu210 hardcode (1080×2340) caused TextViews /
     * Buttons to render outside the HWC-composited 720×1280 visible region.
     */
    static int[] getDefaultDisplaySize() {
        try {
            android.view.Display d = android.hardware.display.DisplayManagerGlobal
                    .getInstance()
                    .getRealDisplay(android.view.Display.DEFAULT_DISPLAY);
            if (d != null) {
                android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
                d.getRealMetrics(dm);
                if (dm.widthPixels > 0 && dm.heightPixels > 0) {
                    return new int[]{dm.widthPixels, dm.heightPixels};
                }
            }
        } catch (Throwable t) {
            Log.w(TAG, "getDefaultDisplaySize: DisplayManagerGlobal lookup failed,"
                    + " using DAYU200 baseline 720x1280", t);
        }
        // DAYU200 native resolution (per CLAUDE.md target hardware).
        return new int[]{720, 1280};
    }

    static int resolveRelayoutDimension(int layoutDimension, int requestedDimension,
            int sessionDimension) {
        if (layoutDimension == WindowManager.LayoutParams.MATCH_PARENT) {
            return sessionDimension;
        }
        if (requestedDimension > 0) {
            return requestedDimension;
        }
        if (layoutDimension > 0) {
            return layoutDimension;
        }
        return sessionDimension;
    }

    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public void remove(android.os.IBinder clientToken) throws android.os.RemoteException {
        remove(IWindow.Stub.asInterface(clientToken));
    }

    @Override
    public int relayout(android.view.IWindow window, android.view.WindowManager.LayoutParams attrs, int requestedWidth, int requestedHeight, int viewVisibility, int flags, int seq, int lastSyncSeqId, android.view.WindowRelayoutResult outRelayoutResult) throws android.os.RemoteException {
        int result = relayoutLegacy(window, attrs, requestedWidth, requestedHeight, viewVisibility, flags, seq, lastSyncSeqId, outRelayoutResult.frames, outRelayoutResult.mergedConfiguration, outRelayoutResult.surfaceControl, outRelayoutResult.insetsState, outRelayoutResult.activeControls, new Bundle());
        outRelayoutResult.syncSeqId = lastSyncSeqId;
        if (result >= 0 && attrs != null && attrs.type >= WindowManager.LayoutParams.FIRST_APPLICATION_WINDOW && attrs.type <= WindowManager.LayoutParams.LAST_APPLICATION_WINDOW) {
            outRelayoutResult.activityWindowInfo = new android.window.ActivityWindowInfo();
            outRelayoutResult.activityWindowInfo.set(false, outRelayoutResult.frames.frame, outRelayoutResult.frames.frame);
        }
        return result;
    }

    @Override
    public boolean performHapticFeedback(int effectId, boolean always, boolean fromIme) throws android.os.RemoteException {
        return performHapticFeedback(effectId, always);
    }

    @Override
    public void performHapticFeedbackAsync(int effectId, boolean always, boolean fromIme) throws android.os.RemoteException {
        performHapticFeedbackAsync(effectId, always);
    }

    @Override
    public android.os.IBinder performDrag(android.view.IWindow window, int flags, android.view.SurfaceControl surface, int touchSource, int touchDeviceId, int touchPointerId, float touchX, float touchY, float thumbCenterX, float thumbCenterY, android.content.ClipData data) throws android.os.RemoteException {
        return performDrag(window, flags, surface, touchSource, touchX, touchY, thumbCenterX, thumbCenterY, data);
    }

    @Override
    public void grantInputChannel(int displayId, android.view.SurfaceControl surface, android.os.IBinder clientToken, android.window.InputTransferToken hostInputTransferToken, int flags, int privateFlags, int inputFeatures, int type, android.os.IBinder windowToken, android.window.InputTransferToken embeddedInputTransferToken, java.lang.String inputHandleName, android.view.InputChannel outInputChannel) throws android.os.RemoteException {
        throw new UnsupportedOperationException("OpenHarmony embedded input-channel provider is not connected");
    }

    @Override
    public void grantEmbeddedWindowFocus(android.view.IWindow window, android.window.InputTransferToken inputToken, boolean grantFocus) throws android.os.RemoteException {
        grantEmbeddedWindowFocus(window, inputToken == null ? null : inputToken.getToken(), grantFocus);
    }

    @Override
    public void reportDecorViewGestureInterceptionChanged(android.view.IWindow window, boolean intercepted) throws android.os.RemoteException {
        logStub("reportDecorViewGestureInterceptionChanged", "OH decor gestures use the OH input dispatcher");
    }

    @Override
    public boolean moveFocusToAdjacentWindow(android.view.IWindow fromWindow, int direction) throws android.os.RemoteException {
        return false;
    }
}

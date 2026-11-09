package adapter.window;

/**
 * A sub-window's z-level among its parent's sub-windows, for an OH session the bridge created. Every
 * Android window here is an OH sub-window of the host's window; OH stacks siblings by z-level, then by
 * order. The native is in liboh_window_policy.so (oh_window_order.cpp), a library of the runtime
 * built beside the bridge.
 */
final class OhWindowOrder {
    private OhWindowOrder() {}

    private static final class Native {
        static { System.loadLibrary("oh_window_policy"); }
        static void load() {}
    }

    private static native int nativeSetSubWindowZLevel(int session, int zLevel);

    /** The request's result: 0 when submitted. Integer.MIN_VALUE when the native could not run. */
    static int setSubWindowZLevel(int session, int zLevel) {
        try {
            Native.load();
            return nativeSetSubWindowZLevel(session, zLevel);
        } catch (Throwable failure) {
            System.err.println("[OH_WINDOW_ORDER] session=" + session + " zLevel=" + zLevel
                    + " failed: " + failure);
            return Integer.MIN_VALUE;
        }
    }
}

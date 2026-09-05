package adapter.window;

import android.view.View;

/** Explicit capability fallback for OH runtimes lacking a hardware functor. */
public final class OhWebViewBridge {
    private OhWebViewBridge() {}
    public static void initialized(View view) {
        if ("1".equals(System.getenv("WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK"))) {
            view.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
            System.err.println("[OH_WEBVIEW] software fallback; hardware functor unavailable");
        }
    }
}

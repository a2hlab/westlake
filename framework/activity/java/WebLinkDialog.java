package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.Dialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.util.List;

/**
 * A web link opened in the app, where Android opens it in the browser.
 *
 * Every Android device has a browser, so apps open links (VIEW of an http or https address, no
 * component or package named) without checking for one. AllTrails, Gemini (a Play Store page) and
 * Insta360 died of ActivityNotFoundException when nothing here handled such a start: OpenHarmony
 * resolves no handler for VIEW https on the board. The page opens instead in a WebView, in a
 * full-screen dialog over the activity that started it, and Back closes it. A navigation to an
 * address that is not http or https (an app's own redirect, as at the end of a sign-in) is handed on
 * as VIEW of that address, as a browser hands it on, and closes the dialog. A link that names a
 * package keeps Android's answer when that package is missing: ActivityNotFoundException.
 */
final class WebLinkDialog {
    private static final String TAG = "WL_WebLink";

    private WebLinkDialog() {}

    /** Whether intent asks a browser for a web page: VIEW of http(s), no component or package named. */
    static boolean opensWebLink(Intent intent) {
        if (!Intent.ACTION_VIEW.equals(intent.getAction())) return false;
        if (intent.getComponent() != null || intent.getPackage() != null) return false;
        return isWeb(intent.getData());
    }

    private static boolean isWeb(Uri uri) {
        String scheme = uri != null ? uri.getScheme() : null;
        return "http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme);
    }

    /**
     * Shows uri over the activity resultTo names, else over the top activity of the app's task; on the
     * main thread, after the caller's current message, as a started activity would come. False when
     * the app has no live activity to show it over.
     */
    static boolean show(Uri uri, IBinder resultTo) {
        ActivityThread thread = ActivityThread.currentActivityThread();
        if (thread == null) return false;
        Activity over = resultTo != null ? thread.getActivity(resultTo) : null;
        if (over == null) {
            List<IBinder> tokens = ActivityClientControllerAdapter.taskTokens();
            over = tokens.isEmpty() ? null : thread.getActivity(tokens.get(tokens.size() - 1));
        }
        if (over == null || over.isFinishing()) return false;
        final Activity activity = over;
        new Handler(Looper.getMainLooper()).post(() -> open(activity, uri));
        return true;
    }

    private static void open(final Activity activity, Uri uri) {
        if (activity.isFinishing() || activity.isDestroyed()) {
            Log.i(TAG, "not opened, its activity is gone: " + uri);
            return;
        }
        try {
            final Dialog dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_Light_NoActionBar);
            final WebView web = new WebView(activity);
            web.getSettings().setJavaScriptEnabled(true);
            web.getSettings().setDomStorageEnabled(true);
            web.setWebViewClient(new WebViewClient() {
                @Override
                public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                    Uri next = request.getUrl();
                    if (isWeb(next)) return false;
                    try {
                        activity.startActivity(new Intent(Intent.ACTION_VIEW, next));
                    } catch (Throwable throwable) {
                        Log.i(TAG, "nothing handles " + next + ": " + throwable);
                    }
                    dialog.dismiss();
                    return true;
                }
            });
            dialog.setContentView(web, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            dialog.setOnDismissListener(d -> web.destroy());
            dialog.show();
            web.loadUrl(uri.toString());
            Log.i(TAG, "opened " + uri + " over " + activity.getComponentName());
        } catch (Throwable throwable) {
            Log.w(TAG, "could not open " + uri + ": " + throwable);
        }
    }
}

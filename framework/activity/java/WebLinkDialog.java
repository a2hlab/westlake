package adapter.activity;

import android.app.Activity;
import android.app.ActivityThread;
import android.app.Dialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.List;
import java.util.Locale;

/**
 * A web link opened in the app, where Android opens it in the browser.
 *
 * Every Android device has a browser, so apps open links (VIEW of an http or https address, no
 * component or package named) without checking for one. AllTrails, Gemini (a Play Store page) and
 * Insta360 died of ActivityNotFoundException when nothing here handled such a start: OpenHarmony
 * resolves no handler for VIEW https on the board. The page opens instead in a WebView, in a
 * full-screen dialog over the activity that started it, and Back closes it. A navigation to an
 * address that is not http or https (an app's own redirect, as at the end of a sign-in) is handed on
 * as VIEW of that address, as a browser hands it on, and closes the dialog. So is a navigation to a
 * web address the app itself claims as a verified link (see appLink). A link that names a
 * package keeps Android's answer when that package is missing: ActivityNotFoundException.
 *
 * Apps that look for a browser before they open a link find one: the package manager answers with
 * a stand-in browser (PackageManagerAdapter.WEB_LINK_BROWSER), and a link started in that package,
 * as AppAuth starts a sign-in page in the browser it chose, opens here too.
 */
final class WebLinkDialog {
    private static final String TAG = "WL_WebLink";

    private WebLinkDialog() {}

    /**
     * Whether intent asks a browser for a web page: VIEW of http(s), with no component or package
     * named, or naming the stand-in browser.
     */
    static boolean opensWebLink(Intent intent) {
        if (!Intent.ACTION_VIEW.equals(intent.getAction())) return false;
        String target = intent.getComponent() != null ? intent.getComponent().getPackageName() : intent.getPackage();
        if (target != null && !adapter.packagemanager.PackageManagerAdapter.WEB_LINK_BROWSER.equals(target)) {
            return false;
        }
        return isWeb(intent.getData());
    }

    private static boolean isWeb(Uri uri) {
        String scheme = uri != null ? uri.getScheme() : null;
        return "http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme);
    }

    /**
     * VIEW of uri for the app's own activity that claims it as a verified link (an http or https
     * intent filter with autoVerify), else null. A browser hands a navigation to such a link to the
     * app that owns it, which is how a web sign-in returns: Uber's Google sign-in ends with a redirect
     * to https://auth.uber.com/v2/googleauth/rider, which its sign-in activity claims. Loaded as a
     * page, that address shows Uber's "Sorry, we couldn't find that page" and the sign-in is lost.
     */
    static Intent appLink(Activity activity, Uri uri) {
        Intent view = new Intent(Intent.ACTION_VIEW, uri)
                .addCategory(Intent.CATEGORY_BROWSABLE)
                .setPackage(activity.getPackageName());
        for (ResolveInfo info : adapter.packagemanager.SourcePackageRegistry.queryActivities(view, null,
                PackageManager.MATCH_DEFAULT_ONLY | PackageManager.GET_RESOLVED_FILTER)) {
            if (info.filter != null && info.filter.getAutoVerify() && info.activityInfo != null) {
                return view.setClassName(info.activityInfo.packageName, info.activityInfo.name);
            }
        }
        return null;
    }

    /** Origin of a web document, as displayed above the page. Never include the URL's user data. */
    static String webOrigin(String address) {
        if (address == null) return null;
        try {
            URI uri = new URI(address);
            String scheme = uri.getScheme();
            String host = uri.getHost();
            if (!("https".equalsIgnoreCase(scheme) || "http".equalsIgnoreCase(scheme))
                    || host == null || uri.getRawUserInfo() != null || host.indexOf('%') >= 0) {
                return null;
            }
            int port = uri.getPort();
            if (port > 65535) return null;
            boolean defaultPort = port == -1 || port == ("https".equalsIgnoreCase(scheme) ? 443 : 80);
            return scheme.toLowerCase(Locale.ROOT) + "://" + host.toLowerCase(Locale.ROOT)
                    + (defaultPort ? "" : ":" + port);
        } catch (URISyntaxException invalid) {
            return null;
        }
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
            Log.i(TAG, "Web link skipped because its activity is gone");
            return;
        }
        try {
            final Dialog dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_Light_NoActionBar);
            final TextView address = new TextView(dialog.getContext());
            address.setTextDirection(View.TEXT_DIRECTION_LTR);
            int inset = Math.round(8 * activity.getResources().getDisplayMetrics().density);
            address.setPadding(inset * 2, inset, inset * 2, inset);
            address.setText("Opening page…");
            final WebView web = new WebView(activity);
            web.getSettings().setJavaScriptEnabled(true);
            web.getSettings().setDomStorageEnabled(true);
            web.setWebViewClient(new WebViewClient() {
                private boolean mainFrameFailed;

                @Override
                public void onPageStarted(WebView view, String url, Bitmap favicon) {
                    mainFrameFailed = false;
                    address.setText("Opening page…");
                }

                @Override
                public void onPageCommitVisible(WebView view, String url) {
                    String committed = webOrigin(url);
                    address.setText(!mainFrameFailed && committed != null && committed.equals(webOrigin(view.getUrl()))
                            ? committed : "Page address unavailable");
                }

                @Override
                public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                    if (request.isForMainFrame()) {
                        mainFrameFailed = true;
                        address.setText("Page address unavailable");
                    }
                    super.onReceivedError(view, request, error);
                }

                @Override
                public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                    Uri next = request.getUrl();
                    if (isWeb(next)) {
                        Intent link = request.isForMainFrame() ? appLink(activity, next) : null;
                        if (link == null) return false;
                        try {
                            activity.startActivity(link);
                        } catch (Throwable throwable) {
                            Log.i(TAG, "App link failed: " + throwable.getClass().getSimpleName());
                            return false;
                        }
                        // The class only: the address can carry a sign-in code.
                        Log.i(TAG, "App link handed to " + link.getComponent().getClassName());
                        dialog.dismiss();
                        return true;
                    }
                    try {
                        activity.startActivity(new Intent(Intent.ACTION_VIEW, next));
                    } catch (Throwable throwable) {
                        Log.i(TAG, "Redirect failed: " + throwable.getClass().getSimpleName());
                    }
                    dialog.dismiss();
                    return true;
                }
            });
            LinearLayout layout = new LinearLayout(dialog.getContext());
            layout.setOrientation(LinearLayout.VERTICAL);
            // Some OH subwindows extend under the status bar without receiving a status-bar InsetsSource.
            int statusBarDimen = layout.getResources().getIdentifier("status_bar_height", "dimen", "android");
            int nominalBar = statusBarDimen == 0 ? 0 : layout.getResources().getDimensionPixelSize(statusBarDimen);
            int[] position = new int[2];
            layout.addOnLayoutChangeListener((v, l, t, r, b, oldL, oldT, oldR, oldB) -> {
                WindowInsets insets = v.getRootWindowInsets();
                int reportedBar = insets == null ? 0 : insets.getInsets(WindowInsets.Type.statusBars()).top;
                v.getLocationOnScreen(position);
                int padding = Math.max(0, Math.max(nominalBar, reportedBar) - position[1]);
                if (padding != v.getPaddingTop()) v.setPadding(0, padding, 0, 0);
            });
            layout.addView(address, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            layout.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
            dialog.setContentView(layout, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            dialog.setOnDismissListener(d -> web.destroy());
            dialog.show();
            web.loadUrl(uri.toString());
            Log.i(TAG, "Web link opened over " + activity.getComponentName());
        } catch (Throwable throwable) {
            Log.w(TAG, "Web link failed: " + throwable.getClass().getSimpleName());
        }
    }
}

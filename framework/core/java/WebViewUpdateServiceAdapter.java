/*
 * WebViewUpdateServiceAdapter.java
 *
 * Minimal system-service boundary for an explicitly sideloaded, board-matched
 * AOSP WebView provider. Android SystemServer normally owns this service; OHOS
 * does not run SystemServer, so the adapter supplies the selected PackageInfo
 * while leaving android.webkit.WebViewFactory and the Chromium provider intact.
 */
package adapter.core;

import adapter.packagemanager.PackageManagerAdapter;

import android.content.pm.PackageInfo;
import android.webkit.IWebViewUpdateService;
import android.webkit.WebViewProviderInfo;
import android.webkit.WebViewProviderResponse;

public final class WebViewUpdateServiceAdapter extends IWebViewUpdateService.Stub {
    private static final WebViewUpdateServiceAdapter INSTANCE =
            new WebViewUpdateServiceAdapter();

    private WebViewUpdateServiceAdapter() {}

    public static WebViewUpdateServiceAdapter getInstance() {
        return INSTANCE;
    }

    public static boolean isAvailable() {
        return PackageManagerAdapter.getSideloadedWebViewPackageInfo() != null;
    }

    @Override
    public WebViewProviderResponse waitForAndGetProvider() {
        PackageInfo pi = PackageManagerAdapter.getSideloadedWebViewPackageInfo();
        System.err.println("[WESTLAKE-WEBVIEW] waitForAndGetProvider package="
                + (pi != null ? pi.packageName : "<missing>"));
        // LIBLOAD_SUCCESS. The provider APK and native library are then loaded
        // by the normal WebViewFactory path in framework.jar.
        return new WebViewProviderResponse(pi, pi != null ? 0 : 4);
    }

    @Override
    public PackageInfo getCurrentWebViewPackage() {
        return PackageManagerAdapter.getSideloadedWebViewPackageInfo();
    }

    @Override
    public String getCurrentWebViewPackageName() {
        PackageInfo pi = getCurrentWebViewPackage();
        return pi != null ? pi.packageName : null;
    }

    @Override
    public String changeProviderAndSetting(String newProvider) {
        return getCurrentWebViewPackageName();
    }

    @Override
    public WebViewProviderInfo[] getAllWebViewPackages() {
        return new WebViewProviderInfo[0];
    }

    @Override
    public WebViewProviderInfo[] getValidWebViewPackages() {
        return new WebViewProviderInfo[0];
    }

    @Override
    public boolean isMultiProcessEnabled() {
        // appspawn-x does not yet host Chromium renderer child services.
        return false;
    }

    @Override
    public void enableMultiProcess(boolean enable) {}

    @Override
    public void notifyRelroCreationCompleted() {}

    // Android 15 service boundary. Unsupported OH capabilities fail explicitly.
    @Override
    public android.webkit.WebViewProviderInfo getDefaultWebViewPackage() throws android.os.RemoteException {
        PackageInfo pi = getCurrentWebViewPackage();
        return pi == null ? null : new WebViewProviderInfo(pi.packageName, "Westlake WebView", true, false, new String[0]);
    }
}

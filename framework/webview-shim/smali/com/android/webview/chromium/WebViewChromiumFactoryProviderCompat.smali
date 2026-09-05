.class public Lcom/android/webview/chromium/WebViewChromiumFactoryProviderCompat;
.super Lcom/android/webview/chromium/WebViewChromiumFactoryProviderForT;
.source "WebViewChromiumFactoryProviderCompat.java"

# Compatibility boundary for WebView wrappers which probe the optional
# ByteDance three-argument createWebView extension.  The third argument only
# selects a dedicated TTWebView instance; when the wrapper has selected the
# Android system provider, the stock two-argument factory is authoritative.

.method public constructor <init>(Landroid/webkit/WebViewDelegate;)V
    .locals 0

    invoke-direct {p0, p1}, Lcom/android/webview/chromium/WebViewChromiumFactoryProviderForT;-><init>(Landroid/webkit/WebViewDelegate;)V

    return-void
.end method

.method public createWebView(Landroid/webkit/WebView;Landroid/webkit/WebView$PrivateAccess;Z)Landroid/webkit/WebViewProvider;
    .locals 1

    invoke-super {p0, p1, p2}, Lcom/android/webview/chromium/WebViewChromiumFactoryProviderForT;->createWebView(Landroid/webkit/WebView;Landroid/webkit/WebView$PrivateAccess;)Landroid/webkit/WebViewProvider;

    move-result-object v0

    return-object v0
.end method

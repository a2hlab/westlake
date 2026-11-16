# The captured WebView input, rebuilt from source

The launcher (`probe_source_app.py --webview-input` in a2hlab/manifest) runs WebView from six files that
`parity27-runtime-inputs.lock.json` pins by hash: the files mapped in the accepted Toutiao process in
September. They were built by hand on one machine and no recipe was kept, so nobody else could
produce them. This directory holds what is needed to rebuild them byte for byte. The tool is the
manifest's `tools/build_webview_input.py`.

A launch with `--bionic-shim-build` uses that build's `libwebview_bionic_shim.so` and `libandroid.so`
instead of the captured ones. The other four files are the ones rebuilt here:

| File | What it is | Rebuilt from |
| --- | --- | --- |
| `webview-t.apk` | Chromium WebView 109.0.5414.123 (`com.android.webview`, unsigned), with Westlake classes and hooks | AOSP's prebuilt `prebuilt/arm64/webview.apk` at `platform/external/chromium-webview` commit `259ec8d2ad6dd574a7e6de96b317a80de87c227b`, plus `apk/` |
| `webview-t-lib/libwebviewchromium.so` | That APK's arm64 engine with 11 imports renamed to the shim's ABI translations | The same APK |
| `webview-t-lib/libjnigraphics.so` | The `AndroidBitmap_*` shim | `../libjnigraphics_webview_shim.cpp` |
| `webview-t-lib/libwebviewchromium_plat_support.so` | AOSP's WebView platform support (draw functors, canvas access) for OH | AOSP's `native/webview/plat_support` plus `plat-support/` |

## The APK

The captured `classes.dex` is AOSP's with 15 classes added and 5 Chromium classes changed:

- `apk/smali/`: `westlake.WlFix` and its inner classes, and `com.ohos.shim.bridge.OHBridge`.
  `../smali/` adds `WebViewChromiumFactoryProviderCompat`.
- `apk/chromium-classes.patch`: hooks into those classes:
  - `WlFix.web_draw`, `web_scroll` and `web_callback` from `WebViewChromium`, `AwContents` and
    `WebContentsImpl`;
  - the three-argument `createWebView` on `WebViewChromiumFactoryProviderForT`;
  - a `Display.getRealSize` fallback when Chromium reads a zero display size.
- `apk/dex-order.json`: smali orders a few interned dex tables by thread timing, and the captured
  dex was assembled multi-threaded. These byte ranges (table offsets, indices and the header
  checksum and signature) turn a single-threaded assembly into the captured bytes.

The patch is against the baksmali text after two normalizations the captured dex shows:
1. `.line` entries after a method's last instruction are gone.
2. Trailing default static values are gone.

`WlFix` is diagnostic and repair code written for Toutiao:
- page script probes;
- a JPEG repair path;
- `setWebContentsDebuggingEnabled(true)`;
- a network self-test against `https://httpbin.org/image/jpeg`.

It is kept because it is part of the accepted bytes. Removing it means a new input and a new device
acceptance.

## The platform support library

- `plat-support/plat-support.patch` turns the pinned AOSP 15 `native/webview/plat_support` files into
  the Android 13-era sources the capture was built from (functor table version 3), with
  `[WESTLAKE-WEB*-741]` diagnostics.
- `plat-support/JNIHelp.h.patch` is Westlake's per-method native registration.

The build reads headers from these pinned trees:
- the manifest's AOSP and OpenHarmony Skia trees;
- `a2hlab/legacy-runtime-sources`;
- `vendored/`: two AOSP `android/bitmap.h` versions that no a2hlab mirror carries:
  - `android-11.0.0_r48`, blob `2631b144`;
  - `android-13.0.0_r1`, blob `35f87f96`.

Four OpenHarmony Skia m133 headers mark their class `SK_API_TYPE` instead of `SK_API`. The tool makes
that one-word change.

Link inputs that were copied from a device are replaced by stub libraries exporting the symbols the
link used, which gives identical output.

## Licenses

AOSP sources and headers are Apache 2.0. Chromium's code, which the smali patch context shows, is
under Chromium's BSD-style license. OpenHarmony's Skia is BSD-style.

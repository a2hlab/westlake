/*
 * libnativewindow.so as staged beside the bionic shim: a name for DT_NEEDED, and nothing else.
 *
 * Twelve of the HK top 1000 name it -- Maps' and YouTube's libgoogle3.so, TensorFlow Lite's GPU
 * delegate, Bilibili's filter SDK, two games' frame-rate layer and more. OH has no such library,
 * so their load failed before a single symbol was looked up. This file gives the dynamic linker
 * one to find on the Android namespace's path. It defines no entry point: the importers' symbols
 * bind to the runtime's libhwui.so and to the preloaded shim (webview_bionic_shim.c).
 *
 * Its version script declares LIBNATIVEWINDOW, the version those importers ask this library for,
 * so the version exists in the library their DT_VERNEED names; the shim's own exports carry it
 * too, since that is where the loader finds the definitions.
 */
typedef int westlake_nativewindow_stub_defines_nothing;

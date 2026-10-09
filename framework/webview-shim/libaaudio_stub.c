/*
 * libaaudio.so as staged beside the bionic shim: a name for DT_NEEDED, and nothing else.
 *
 * Felicity's audio engine, Jami's core and Linphone's AAudio plugin name libaaudio.so in DT_NEEDED.
 * OH has no such library, so their load failed before a single symbol was looked up. This file gives
 * the dynamic linker one to find on the Android namespace's path. It defines no AAudio entry point:
 * the app library's imports bind to the preloaded shim's (aaudio_ohaudio.c), and an Android dlopen of
 * libaaudio.so is answered with the shim's handle, so no lookup ever lands here.
 */
typedef int westlake_aaudio_stub_defines_nothing;

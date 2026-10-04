/*
 * libGLESv1_CM.so: Android's OpenGL ES 1.x library, as the board's vendor driver provides it.
 *
 * OH ships no libGLESv1_CM.so. The board's GLES 1.x implementation is the Mali driver,
 * libGLES_mali.z.so (installed as /vendor/lib64/chipsetsdk/libGLESv1_impl.so), which exports the
 * GLES 1.x entry points itself. This library defines nothing: it only names the driver in
 * DT_NEEDED, so a library that links GLESv1_CM resolves those entry points through it, the way
 * Android's loader library hands them to the vendor's GLES 1 implementation.
 *
 * SDL2 links GLESv1_CM and GLESv2 both; without this file libSDL2.so did not load at all
 * ("Error loading shared library libGLESv1_CM.so"), and SDLActivity then died on its first
 * native call (nativeSetenv). Functions GLES 2 also has still bind to libGLESv2.so first, which
 * SDL names earlier.
 */

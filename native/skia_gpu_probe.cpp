// Source Ganesh GPU rendering contract, independent of Android Java startup.
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "SOURCE_GPU_FAIL line=%d %s egl=0x%x gl=0x%x\n", \
            __LINE__, #expr, eglGetError(), glGetError()); return 1; } } while (0)

int main() {
    auto makeContext = static_cast<sk_sp<GrDirectContext>(*)()>(&GrDirectContexts::MakeGL);
    Dl_info provider{};
    CHECK(dladdr(reinterpret_cast<void*>(makeContext), &provider));
    CHECK(provider.dli_fname && strstr(provider.dli_fname, "/a2hlab-native-") &&
          !strcmp(strrchr(provider.dli_fname, '/'), "/libhwui.so"));
    printf("SOURCE_GPU_PROVIDER %s\n", provider.dli_fname);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK(display != EGL_NO_DISPLAY);
    EGLint major, minor;
    CHECK(eglInitialize(display, &major, &minor));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    CHECK(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
    EGLint dimensions[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
    EGLSurface drawable = eglCreatePbufferSurface(display, config, dimensions);
    CHECK(drawable != EGL_NO_SURFACE);
    EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    CHECK(context != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(display, drawable, drawable, context));
    printf("SOURCE_GPU_DRIVER %s / %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    glClearColor(0.2f, 7.0f / 15.0f, 11.0f / 15.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char control[4]{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, control);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(control[0] == 0x33 && control[1] == 0x77 && control[2] == 0xbb && control[3] == 255);
    puts("SOURCE_GPU_HOST_CONTROL_PASS");
    auto gpu = makeContext();
    CHECK(gpu && gpu->backend() == GrBackendApi::kOpenGL);
    auto info = SkImageInfo::Make(32, 32, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    auto surface = SkSurfaces::RenderTarget(gpu.get(), skgpu::Budgeted::kYes, info);
    CHECK(surface && surface->recordingContext() == gpu.get());
    auto* canvas = surface->getCanvas();
    canvas->clear(0xff3377bb);
    SkPaint paint;
    paint.setColor(0xff112233);
    canvas->drawRect(SkRect::MakeXYWH(2, 2, 10, 20), paint);
    paint.setColor(0xff44aa66);
    paint.setAntiAlias(true);
    canvas->drawCircle(23, 16, 6, paint);
    gpu->flushAndSubmit(GrSyncCpu::kYes);
    unsigned char pixels[32 * 32 * 4]{};
    CHECK(surface->readPixels(info, pixels, 32 * 4, 0, 0));
    auto pixel = [&](int x, int y, unsigned r, unsigned g, unsigned b) {
        auto* p = pixels + (y * 32 + x) * 4;
        return p[0] == r && p[1] == g && p[2] == b && p[3] == 255;
    };
    CHECK(pixel(0, 0, 0x33, 0x77, 0xbb));
    CHECK(pixel(5, 10, 0x11, 0x22, 0x33));
    CHECK(pixel(23, 16, 0x44, 0xaa, 0x66));
    CHECK(pixel(31, 31, 0x33, 0x77, 0xbb));
    surface.reset();
    gpu.reset();
    CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(display, context));
    CHECK(eglDestroySurface(display, drawable));
    CHECK(eglTerminate(display));
    puts("SOURCE_GPU_RENDER_PASS width=32 height=32 checked_pixels=4");
    return 0;
}

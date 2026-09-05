/*
 * Diagnostic GLESv2 boundary for Android media-player DSOs on OpenHarmony.
 *
 * The guest DSO imports GLESv2 while the board implements the API in the
 * platform GLESv3 client library.  This proxy keeps that dependency graph but
 * brackets the state-changing calls so the first GL error can be attributed to
 * the call and arguments which created it.  A consumed error is returned by
 * the guest's next glGetError(), preserving the player's observable behavior.
 */
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static void *g_real_gles;
static __thread GLenum g_pending_error;
static unsigned long g_error_count;

static void trace_line(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
    (void)write(STDERR_FILENO, line, length);
}

static void *real_symbol(const char *name)
{
    if (g_real_gles == NULL) {
        g_real_gles = dlopen("/system/lib64/platformsdk/libGLESv3.so",
                            RTLD_NOW | RTLD_LOCAL);
        trace_line("[WESTLAKE-TTGL] real=%p error=%s\n", g_real_gles,
                   g_real_gles == NULL ? dlerror() : "none");
    }
    void *symbol = g_real_gles == NULL ? NULL : dlsym(g_real_gles, name);
    if (symbol == NULL) {
        trace_line("[WESTLAKE-TTGL] missing %s error=%s\n", name, dlerror());
    }
    return symbol;
}

static GLenum take_real_error(void)
{
    static GLenum (*real_glGetError)(void);
    if (real_glGetError == NULL) {
        real_glGetError = (GLenum (*)(void))real_symbol("glGetError");
    }
    return real_glGetError == NULL ? GL_NO_ERROR : real_glGetError();
}

static void preserve_error(GLenum error)
{
    if (error != GL_NO_ERROR && g_pending_error == GL_NO_ERROR) {
        g_pending_error = error;
    }
}

static void before_call(const char *name)
{
    GLenum error = take_real_error();
    if (error != GL_NO_ERROR) {
        preserve_error(error);
        trace_line("[WESTLAKE-TTGL] preexisting before=%s error=0x%x\n",
                   name, error);
    }
}

static void after_call(const char *name, const char *details)
{
    GLenum error = take_real_error();
    if (error == GL_NO_ERROR) return;
    preserve_error(error);
    unsigned long count = __atomic_add_fetch(&g_error_count, 1, __ATOMIC_RELAXED);
    if (count <= 200 || count == 500 || count == 1000 || count % 5000 == 0) {
        trace_line("[WESTLAKE-TTGL] call=%s error=0x%x count=%lu %s\n",
                   name, error, count, details == NULL ? "" : details);
    }
}

GLenum glGetError(void)
{
    if (g_pending_error != GL_NO_ERROR) {
        GLenum error = g_pending_error;
        g_pending_error = GL_NO_ERROR;
        return error;
    }
    return take_real_error();
}

#define WRAP_VOID0(name)                                                    \
    void name(void)                                                        \
    {                                                                      \
        static void (*real)(void);                                         \
        if (real == NULL) real = (void (*)(void))real_symbol(#name);       \
        before_call(#name);                                                \
        if (real != NULL) real();                                          \
        after_call(#name, "");                                            \
    }

#define WRAP_VOID1(name, t1, a1)                                           \
    void name(t1 a1)                                                       \
    {                                                                      \
        static void (*real)(t1);                                           \
        if (real == NULL) real = (void (*)(t1))real_symbol(#name);         \
        before_call(#name);                                                \
        if (real != NULL) real(a1);                                        \
        after_call(#name, "");                                            \
    }

#define WRAP_VOID2(name, t1, a1, t2, a2)                                  \
    void name(t1 a1, t2 a2)                                               \
    {                                                                      \
        static void (*real)(t1, t2);                                       \
        if (real == NULL) real = (void (*)(t1, t2))real_symbol(#name);     \
        before_call(#name);                                                \
        if (real != NULL) real(a1, a2);                                    \
        after_call(#name, "");                                            \
    }

WRAP_VOID0(glFinish)
WRAP_VOID0(glFlush)
WRAP_VOID1(glClear, GLbitfield, mask)
WRAP_VOID1(glCompileShader, GLuint, shader)
WRAP_VOID1(glDisable, GLenum, cap)
WRAP_VOID1(glDisableVertexAttribArray, GLuint, index)
WRAP_VOID1(glEnable, GLenum, cap)
WRAP_VOID1(glEnableVertexAttribArray, GLuint, index)
WRAP_VOID1(glLinkProgram, GLuint, program)
WRAP_VOID1(glUseProgram, GLuint, program)
/* glActiveTexture has one parameter; keep its diagnostic useful explicitly. */
void glActiveTexture(GLenum texture)
{
    static void (*real)(GLenum);
    if (real == NULL) real = (void (*)(GLenum))real_symbol("glActiveTexture");
    before_call("glActiveTexture");
    if (real != NULL) real(texture);
    char details[64];
    (void)snprintf(details, sizeof(details), "texture=0x%x", texture);
    after_call("glActiveTexture", details);
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    static void (*real)(GLenum, GLuint);
    if (real == NULL) real = (void (*)(GLenum, GLuint))real_symbol("glBindBuffer");
    before_call("glBindBuffer");
    if (real != NULL) real(target, buffer);
    char details[96];
    (void)snprintf(details, sizeof(details), "target=0x%x buffer=%u", target, buffer);
    after_call("glBindBuffer", details);
}

void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    static void (*real)(GLenum, GLuint);
    if (real == NULL) real = (void (*)(GLenum, GLuint))real_symbol("glBindFramebuffer");
    before_call("glBindFramebuffer");
    if (real != NULL) real(target, framebuffer);
    char details[96];
    (void)snprintf(details, sizeof(details), "target=0x%x framebuffer=%u", target, framebuffer);
    after_call("glBindFramebuffer", details);
}

void glBindTexture(GLenum target, GLuint texture)
{
    static void (*real)(GLenum, GLuint);
    if (real == NULL) real = (void (*)(GLenum, GLuint))real_symbol("glBindTexture");
    before_call("glBindTexture");
    if (real != NULL) real(target, texture);
    char details[96];
    (void)snprintf(details, sizeof(details), "target=0x%x texture=%u", target, texture);
    after_call("glBindTexture", details);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    static void (*real)(GLenum, GLenum);
    if (real == NULL) real = (void (*)(GLenum, GLenum))real_symbol("glBlendFunc");
    before_call("glBlendFunc");
    if (real != NULL) real(sfactor, dfactor);
    after_call("glBlendFunc", "");
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    static void (*real)(GLenum, GLsizeiptr, const void *, GLenum);
    if (real == NULL) real = (void (*)(GLenum, GLsizeiptr, const void *, GLenum))real_symbol("glBufferData");
    before_call("glBufferData");
    if (real != NULL) real(target, size, data, usage);
    char details[128];
    (void)snprintf(details, sizeof(details), "target=0x%x size=%ld data=%p usage=0x%x",
                   target, (long)size, data, usage);
    after_call("glBufferData", details);
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    static void (*real)(GLfloat, GLfloat, GLfloat, GLfloat);
    if (real == NULL) real = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))real_symbol("glClearColor");
    before_call("glClearColor");
    if (real != NULL) real(red, green, blue, alpha);
    after_call("glClearColor", "");
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    static void (*real)(GLenum, GLint, GLsizei);
    if (real == NULL) real = (void (*)(GLenum, GLint, GLsizei))real_symbol("glDrawArrays");
    before_call("glDrawArrays");
    if (real != NULL) real(mode, first, count);
    char details[96];
    (void)snprintf(details, sizeof(details), "mode=0x%x first=%d count=%d", mode, first, count);
    after_call("glDrawArrays", details);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    static void (*real)(GLenum, GLsizei, GLenum, const void *);
    if (real == NULL) real = (void (*)(GLenum, GLsizei, GLenum, const void *))real_symbol("glDrawElements");
    before_call("glDrawElements");
    if (real != NULL) real(mode, count, type, indices);
    char details[128];
    (void)snprintf(details, sizeof(details), "mode=0x%x count=%d type=0x%x indices=%p",
                   mode, count, type, indices);
    after_call("glDrawElements", details);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level)
{
    static void (*real)(GLenum, GLenum, GLenum, GLuint, GLint);
    if (real == NULL) real = (void (*)(GLenum, GLenum, GLenum, GLuint, GLint))real_symbol("glFramebufferTexture2D");
    before_call("glFramebufferTexture2D");
    if (real != NULL) real(target, attachment, textarget, texture, level);
    char details[160];
    (void)snprintf(details, sizeof(details),
                   "target=0x%x attachment=0x%x textarget=0x%x texture=%u level=%d",
                   target, attachment, textarget, texture, level);
    after_call("glFramebufferTexture2D", details);
}

void glPixelStorei(GLenum pname, GLint param)
{
    static void (*real)(GLenum, GLint);
    if (real == NULL) real = (void (*)(GLenum, GLint))real_symbol("glPixelStorei");
    before_call("glPixelStorei");
    if (real != NULL) real(pname, param);
    after_call("glPixelStorei", "");
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void *pixels)
{
    static void (*real)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    if (real == NULL) real = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))real_symbol("glTexImage2D");
    before_call("glTexImage2D");
    if (real != NULL) real(target, level, internalformat, width, height, border, format, type, pixels);
    char details[192];
    (void)snprintf(details, sizeof(details),
                   "target=0x%x level=%d internal=0x%x size=%dx%d border=%d format=0x%x type=0x%x pixels=%p",
                   target, level, internalformat, width, height, border, format, type, pixels);
    after_call("glTexImage2D", details);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    static void (*real)(GLenum, GLenum, GLint);
    if (real == NULL) real = (void (*)(GLenum, GLenum, GLint))real_symbol("glTexParameteri");
    before_call("glTexParameteri");
    if (real != NULL) real(target, pname, param);
    after_call("glTexParameteri", "");
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels)
{
    static void (*real)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    if (real == NULL) real = (void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))real_symbol("glTexSubImage2D");
    before_call("glTexSubImage2D");
    if (real != NULL) real(target, level, xoffset, yoffset, width, height, format, type, pixels);
    char details[192];
    (void)snprintf(details, sizeof(details),
                   "target=0x%x level=%d offset=%d,%d size=%dx%d format=0x%x type=0x%x pixels=%p",
                   target, level, xoffset, yoffset, width, height, format, type, pixels);
    after_call("glTexSubImage2D", details);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void *pointer)
{
    static void (*real)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    if (real == NULL) real = (void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))real_symbol("glVertexAttribPointer");
    before_call("glVertexAttribPointer");
    if (real != NULL) real(index, size, type, normalized, stride, pointer);
    after_call("glVertexAttribPointer", "");
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    static void (*real)(GLint, GLint, GLsizei, GLsizei);
    if (real == NULL) real = (void (*)(GLint, GLint, GLsizei, GLsizei))real_symbol("glViewport");
    before_call("glViewport");
    if (real != NULL) real(x, y, width, height);
    char details[96];
    (void)snprintf(details, sizeof(details), "rect=%d,%d %dx%d", x, y, width, height);
    after_call("glViewport", details);
}

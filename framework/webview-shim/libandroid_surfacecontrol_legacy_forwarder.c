/*
 * Legacy partial SurfaceControl ABI retained by the known-good WebView
 * libandroid.so.  Chromium rejects this incomplete table and stays on its
 * software compositor, but preserving the symbols makes API-33 choreographer
 * A/B builds differ from the known-good artifact only in choreographer APIs.
 */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

static void *surface_bridge_symbol(const char *name)
{
    static void *bridge;
    if (bridge == NULL) {
        bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return bridge != NULL ? dlsym(bridge, name) : NULL;
}

void *ASurfaceControl_create(void *parent, const char *debug_name)
{
    typedef void *(*Fn)(void *, const char *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceControl_create");
    return fn != NULL ? fn(parent, debug_name) : NULL;
}

void ASurfaceControl_acquire(void *control)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceControl_acquire");
    if (fn != NULL) fn(control);
}

void ASurfaceControl_release(void *control)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceControl_release");
    if (fn != NULL) fn(control);
}

void ASurfaceControl_registerSurfaceStatsListener(
        void *control, int32_t id, void *context, void *listener)
{
    typedef void (*Fn)(void *, int32_t, void *, void *);
    Fn fn = (Fn)surface_bridge_symbol(
            "ASurfaceControl_registerSurfaceStatsListener");
    if (fn != NULL) fn(control, id, context, listener);
}

void ASurfaceControl_unregisterSurfaceStatsListener(
        void *context, void *listener)
{
    typedef void (*Fn)(void *, void *);
    Fn fn = (Fn)surface_bridge_symbol(
            "ASurfaceControl_unregisterSurfaceStatsListener");
    if (fn != NULL) fn(context, listener);
}

int64_t ASurfaceControlStats_getAcquireTime(void *stats)
{
    typedef int64_t (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceControlStats_getAcquireTime");
    return fn != NULL ? fn(stats) : 0;
}

uint64_t ASurfaceControlStats_getFrameNumber(void *stats)
{
    typedef uint64_t (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceControlStats_getFrameNumber");
    return fn != NULL ? fn(stats) : 0;
}

void *ASurfaceTransaction_create(void)
{
    typedef void *(*Fn)(void);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_create");
    return fn != NULL ? fn() : NULL;
}

void ASurfaceTransaction_delete(void *transaction)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_delete");
    if (fn != NULL) fn(transaction);
}

void ASurfaceTransaction_apply(void *transaction)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_apply");
    if (fn != NULL) fn(transaction);
}

void ASurfaceTransaction_reparent(
        void *transaction, void *control, void *new_parent)
{
    typedef void (*Fn)(void *, void *, void *);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_reparent");
    if (fn != NULL) fn(transaction, control, new_parent);
}

void ASurfaceTransaction_setVisibility(
        void *transaction, void *control, int8_t visible)
{
    typedef void (*Fn)(void *, void *, int8_t);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_setVisibility");
    if (fn != NULL) fn(transaction, control, visible);
}

void ASurfaceTransaction_setZOrder(
        void *transaction, void *control, int32_t z_order)
{
    typedef void (*Fn)(void *, void *, int32_t);
    Fn fn = (Fn)surface_bridge_symbol("ASurfaceTransaction_setZOrder");
    if (fn != NULL) fn(transaction, control, z_order);
}

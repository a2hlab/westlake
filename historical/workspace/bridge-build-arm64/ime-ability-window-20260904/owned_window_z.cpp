// Narrow, opt-in binary-boundary equivalent of the source fix in
// oh_window_manager_client.cpp. Preserves the pinned main bridge for this test.
// RSNode::SetPositionZ(float) is a non-virtual exported method; no vtable/layout
// reconstruction or privileged window/focus API is involved.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

using SetZ = void (*)(void*, float);
extern "C" void owned_set_z(void*, float) __asm__("_ZN4OHOS5Rosen6RSNode12SetPositionZEf");
extern "C" void owned_set_z(void* node, float z) {
    static SetZ original = reinterpret_cast<SetZ>(dlsym(RTLD_NEXT,
        "_ZN4OHOS5Rosen6RSNode12SetPositionZEf"));
    if (!original) { std::fputs("[OH_OWNED_Z] missing platform SetPositionZ\n", stderr); std::abort(); }
    Dl_info caller{};
    const bool enabled = std::getenv("WL_ABILITY_OWNED_WINDOWS") && std::getenv("WL_SUB_WINDOW");
    if (enabled && z >= 9000.0f && z < 9100.0f &&
        dladdr(__builtin_return_address(0), &caller) && caller.dli_fname &&
        std::strstr(caller.dli_fname, "/liboh_adapter_bridge.so")) {
        std::fprintf(stderr, "[OH_OWNED_Z] suppressed legacy client Z %.1f; SceneBoard owns ordering\n", z);
        return;
    }
    original(node, z);
}

"""Build the session property boundary using matching platform headers and ABI."""
from pathlib import Path
import shlex, subprocess
ROOT=Path(__file__).resolve().parent
OH=Path('/home/dspfac/openharmony'); SDK=Path('/home/dspfac/ohos-sdk-6.1/linux/native')
BRIDGE=Path('/home/dspfac/bridge-build')
def build(output):
    ninja=OH/'out/rk3568/obj/adapter/framework/jni/oh_adapter_bridge.ninja'
    lines=ninja.read_text().splitlines()
    inc=shlex.split(next(x.split('=',1)[1] for x in lines if x.startswith('include_dirs =')))
    inc=['-I'+str((ninja.parents[4]/x[2:]).resolve()) for x in inc if x.startswith('-I') and 'musl/' not in x]
    # Resolve transitive platform headers from the existing bridge build graph.
    inc += ['-I'+str(OH/p) for p in (
        'base/inputmethod/imf/interfaces/inner_api/inputmethod_controller/include',
        'base/inputmethod/imf/frameworks/native/inputmethod_controller/include',
        'base/inputmethod/imf/frameworks/native/inputmethod_ability/include',
        'base/inputmethod/imf/interfaces/inner_api/inputmethod_ability/include',
        'base/inputmethod/imf/services/include', 'base/inputmethod/imf/common/include',
        'base/inputmethod/imf/common',
        'commonlibrary/c_utils/base/include',
        'foundation/communication/ipc/interfaces/innerkits/ipc_core/include')]
    inc += ['-I'+str(OH/'third_party/skia'), '-I'+str(OH/'foundation/window/window_manager/window_scene/session/host/include/zidl')]
    inc += ['-I'+str(OH/'foundation/window/window_manager/window_scene/interfaces/include')]
    inc += ['-I'+str(OH/'foundation/window/window_manager/interfaces/innerkits/wm')]
    board=ROOT.parent/'out/board_libs'
    toolchain=OH/'prebuilts/clang/ohos/linux-x86_64/15.0.4/llvm'
    boundary=['-DOH_WINDOW_POLICY_OVERLAY', '-DRENDER_SERVICE_CLIENT_CORE_FEATURE_RS_KEYFRAME_NODE_H']
    links=['-ldl']
    subprocess.run([str(toolchain/'bin/clang++'),'--target=aarch64-linux-ohos',
        '--sysroot='+str(SDK/'sysroot'),'-shared','-fPIC','-O2','-std=c++17',
        '-fno-rtti','-D__OHOS__','-D_GNU_SOURCE','-DUSE_M133_SKIA','-Wno-c++11-narrowing','-Wl,-z,defs',
        '-I'+str(BRIDGE/'aosp/libnativehelper/include_jni'),
        *boundary,'-I'+str(BRIDGE/'src/framework/window/jni'),
        '-I'+str(BRIDGE/'src/framework/window/jni/board_api/skia_m133'),
        '-I'+str(BRIDGE/'src/framework/window/jni/board_api/oh6'),*inc,
        str(BRIDGE/'src/framework/window/jni/oh_window_policy.cpp'),
        '-L'+str(board),*links,'-lipc_single.z','-lutils.z','-o',str(output)],check=True)
if __name__=='__main__':build(ROOT/'liboh_window_policy_v25.so')

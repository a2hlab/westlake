"""Build the small official-header OH IME metadata bridge with strict linking."""
from pathlib import Path
import shlex, subprocess
ROOT=Path(__file__).resolve().parent
OH=Path('/home/dspfac/openharmony'); SDK=Path('/home/dspfac/ohos-sdk-6.1/linux/native')
BRIDGE=Path('/home/dspfac/bridge-build')
def build(output, geometry=False):
    ninja=OH/'out/rk3568/obj/adapter/framework/jni/oh_adapter_bridge.ninja'
    lines=ninja.read_text().splitlines()
    inc=shlex.split(next(x.split('=',1)[1] for x in lines if x.startswith('include_dirs =')))
    inc=['-I'+str((ninja.parents[4]/x[2:]).resolve()) for x in inc if x.startswith('-I') and 'musl/' not in x]
    # The input method header is a direct dependency of this isolated unit.
    inc += ['-I'+str(OH/p) for p in (
        'base/inputmethod/imf/interfaces/inner_api/inputmethod_controller/include',
        'base/inputmethod/imf/frameworks/native/inputmethod_controller/include',
        'base/inputmethod/imf/frameworks/native/inputmethod_ability/include',
        'base/inputmethod/imf/interfaces/inner_api/inputmethod_ability/include',
        'base/inputmethod/imf/services/include', 'base/inputmethod/imf/common/include',
        'base/inputmethod/imf/common',
        'commonlibrary/c_utils/base/include',
        'foundation/communication/ipc/interfaces/innerkits/ipc_core/include')]
    inc += ['-I'+str(OH/'third_party/skia')]
    inc += ['-I'+str(OH/'foundation/window/window_manager/window_scene/interfaces/include')]
    inc += ['-I'+str(OH/'foundation/window/window_manager/interfaces/innerkits/wm')]
    board=ROOT.parent/'out/board_libs'
    toolchain=OH/'prebuilts/clang/ohos/linux-x86_64/15.0.4/llvm'
    # Callback overlay constructs no SessionStageAdapter/vtable. Allow an
    # unrelated board-vs-checkout signature to remain an unimplemented method;
    # the actual callback and data/subobject offsets remain compiler-checked.
    # SessionStageStub declares no RSWindowKeyFrameNode object, only the
    # forward-declared type in ISessionStage. Avoid its unnecessary full header
    # and static RenderService command registration in this callback-only DSO.
    boundary=['-DOH_CALLBACK_OVERLAY_ONLY','-DRENDER_SERVICE_CLIENT_CORE_FEATURE_RS_KEYFRAME_NODE_H'] if geometry else ['-include',str(BRIDGE/'src/framework/window/jni/board_api/oh6/input_method_property.h')]
    links=['-ldl'] if geometry else ['-linputmethod_client.z']
    subprocess.run([str(toolchain/'bin/clang++'),'--target=aarch64-linux-ohos',
        '--sysroot='+str(SDK/'sysroot'),'-shared','-fPIC','-O2','-std=c++17',
        '-fno-rtti','-D__OHOS__','-D_GNU_SOURCE','-DUSE_M133_SKIA','-Wno-c++11-narrowing','-Wl,-z,defs',
        '-I'+str(BRIDGE/'aosp/libnativehelper/include_jni'),
        *boundary,'-I'+str(BRIDGE/'src/framework/window/jni'),
        '-I'+str(BRIDGE/'src/framework/window/jni/board_api/skia_m133'),
        '-I'+str(BRIDGE/'src/framework/window/jni/board_api/oh6'),*inc,
        str(BRIDGE/'src/framework/window/jni'/('oh_ime_geometry.cpp' if geometry else 'oh_ime_metadata.cpp')),
        '-L'+str(board),*links,'-lutils.z','-o',str(output)],check=True)
if __name__=='__main__':build(ROOT/'liboh_ime_metadata.so')

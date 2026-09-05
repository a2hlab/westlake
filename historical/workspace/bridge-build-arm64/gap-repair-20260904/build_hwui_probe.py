from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parent;ARM=ROOT.parent
prefix=(ARM/'build_libhwui_arm64.sh').read_text().split('PHASE1_SRCS=')[0]
prefix=prefix.replace('HWUI_SRC=$A14/libs/hwui','HWUI_SRC=$A15/frameworks/base/libs/hwui')
prefix=prefix.replace('OUT_BUILD=$B64/hwui-build','OUT_BUILD='+str(ROOT/'hwui-build'))
prefix=prefix.replace('MK14=/home/dspfac/aosp-14-minikin','MK14=$A15/frameworks/minikin')
command=prefix+'\nset -e\n$CXX $CB $INC -fno-rtti -c '+str(ROOT/'hwui_functor.cpp')+' -o "$OBJ/functor.o"\n'
command+='$CXX $CB $INC -I$HWUI_SRC/pipeline/skia -I$SKIA_OH/include/android -fno-rtti -c '+str(ROOT/'GLFunctorDrawable.cpp')+' -o "$OBJ/gl.o"\n'
command+='$CXX $CB $INC -fno-rtti -c '+str(ROOT/'SkAndroidFunctorUtils.cpp')+' -o "$OBJ/skia_utils.o"\n'
command+='$CXX --target=aarch64-linux-ohos --sysroot=$SR -shared -fPIC -Wl,-Bsymbolic -Wl,-z,defs "$OBJ/functor.o" "$OBJ/gl.o" "$OBJ/skia_utils.o" '+str(ROOT/'baseline-libhwui.so')+' -L$BOARD -lskia_canvaskit.z -lGLESv3 -L$AOSPLIB -lutils -lcutils -lbase -llog -o '+str(ROOT/'liboh_hwui_functor.so')+'\n'
raise SystemExit(subprocess.run(['bash','-c',command]).returncode)

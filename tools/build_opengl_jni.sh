#!/bin/bash
# build_opengl_jni.sh <android-source> <oh-sdk-native> <out-dir>
# Builds libwl_opengl_jni.so: AOSP's generated GL JNI sources compiled unchanged, the OH port of
# EGLImpl, and forwarders for GL functions OH's libGLESv3 lacks, linked against OH's public libEGL/libGLESv3 and the runtime's
# libnativehelper and liblog.
set -e
SRC=$1; SDK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd); D=$HERE/framework/opengl-jni
J=$SRC/frameworks-base/core/jni
mkdir -p $OUT/obj $OUT/gles1-include
# Only Android's GLES 1 headers: OH's SDK has none. EGL, KHR and GLES2/3 must come from OH's SDK,
# because Android's eglplatform.h does not recognise OH and falls back to X11.
ln -sfn $SRC/frameworks-native/opengl/include/GLES $OUT/gles1-include/GLES
# ...plus the one GLES3 header OH's SDK lacks; header search falls back per file to OH's GLES3.
mkdir -p $OUT/gles1-include/GLES3
ln -sfn $SRC/frameworks-native/opengl/include/GLES3/gl3ext.h $OUT/gles1-include/GLES3/gl3ext.h
CXX="$SDK/llvm/bin/clang++ --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -std=c++17"
CC="$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2"
INC="-I$D/compat -I$SRC/libnativehelper/include -I$SRC/libnativehelper/include_platform \
     -I$SRC/libnativehelper/include_jni -I$SRC/logging/liblog/include -I$OUT/gles1-include \
     -D_GNU_SOURCE -include limits.h -include sys/types.h -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES -Wno-unused-parameter -Wno-unused-function"
objs=()
for f in com_google_android_gles_jni_GLImpl android_opengl_GLES10 android_opengl_GLES10Ext \
         android_opengl_GLES11 android_opengl_GLES11Ext android_opengl_GLES20 android_opengl_GLES30 \
         android_opengl_GLES31 android_opengl_GLES31Ext android_opengl_GLES32; do
  $CXX $INC -c $J/$f.cpp -o $OUT/obj/$f.o; objs+=($OUT/obj/$f.o)
done
$CXX $INC -c $D/oh_egl_impl.cpp -o $OUT/obj/oh_egl_impl.o; objs+=($OUT/obj/oh_egl_impl.o)
$CXX $INC -c $D/opengl_jni_onload.cpp -o $OUT/obj/onload.o; objs+=($OUT/obj/onload.o)
# Forwarders cover what the SDK's public libGLESv3 does not export -- the link-time contract, not
# what a particular board happens to export -- and resolve those through eglGetProcAddress.
$SDK/llvm/bin/llvm-readelf -W --dyn-syms $SDK/sysroot/usr/lib/aarch64-linux-ohos/libGLESv3.so \
  | awk '$7!="UND"{split($8,a,"@"); print a[1]}' > $OUT/obj/libGLESv3.exports
python3 $HERE/tools/gen_gl_forwarders.py $OUT/obj/libGLESv3.exports $OUT/obj/gl_forwarders.c \
  $SRC/frameworks-native/opengl/include/GLES/gl.h $SRC/frameworks-native/opengl/include/GLES/glext.h \
  $SDK/sysroot/usr/include/GLES2/gl2ext.h >/dev/null
$CC $INC -c $OUT/obj/gl_forwarders.c -o $OUT/obj/glfwd.o; objs+=($OUT/obj/glfwd.o)
$CC $INC -c $D/gles1_pointer_bounds.c -o $OUT/obj/bounds.o; objs+=($OUT/obj/bounds.o)
$SDK/llvm/bin/clang++ --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -shared -fuse-ld=lld \
  -Wl,-soname,libwl_opengl_jni.so -Wl,-z,defs -Wl,--error-limit=0 -Wl,--version-script=$D/libwl_opengl_jni.map -static-libstdc++ \
  "${objs[@]}" -L$OUT/lib -lnativehelper -llog -lEGL -lGLESv3 -ldl -o $OUT/libwl_opengl_jni.so
rm -rf $OUT/obj $OUT/gles1-include
echo "built $OUT/libwl_opengl_jni.so"

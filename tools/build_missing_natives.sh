#!/bin/bash
# build_missing_natives.sh <android-source> <oh-sdk-native> <out-dir>
# Builds libwl_missing_natives.so from framework/javacore-shim/missing_natives.c, media_natives.c, text_natives.c, audiorecord_natives.c,
# audioports_natives.c, localsocket_natives.c, canvas_natives.c, sqlite_natives.c and notification_natives.c,
# plus expat_natives.cpp (AOSP's SAX parser natives over a static, hidden expat): platform natives
# the staged runtime declares but never registers, bound onto the boot classes from JNI_OnLoad.
set -e
SRC=$1; SDK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
CC="$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2"
OBJ=$OUT/obj; mkdir -p $OBJ
for f in xmlparse xmlrole xmltok; do
  $CC -fvisibility=hidden -fno-strict-aliasing -Wall -Werror -DHAVE_EXPAT_CONFIG_H -DHAVE_SYSCALL_GETRANDOM -include stdio.h -I$SRC/expat \
    -c $SRC/expat/expat/lib/$f.c -o $OBJ/$f.o
done
${CC/clang /clang++ } -std=c++17 -fvisibility=hidden -Wall -Werror -I$HERE/framework/javacore-shim/expat-compat \
  -I$SRC/libcore/luni/src/main/native -I$SRC/libnativehelper/include -I$SRC/libnativehelper/header_only_include \
  -I$SRC/libnativehelper/include_platform_header_only -I$SRC/libnativehelper/include_jni -I$SRC/expat/expat/lib \
  -c $HERE/framework/javacore-shim/expat_natives.cpp -o $OBJ/expat_natives.o
# libcore's NIO natives (ojluni), unmodified, with the InetAddress field IDs and platform-string
# helpers they use; ojluni_nio_natives.c supplies the few libopenjdkjvm/libnativehelper symbols.
OJ=$SRC/libcore/ojluni/src/main/native
for f in Net IOUtil SocketChannelImpl ServerSocketChannelImpl DatagramChannelImpl DatagramDispatcher \
         PollArrayWrapper FileDispatcherImpl MappedByteBuffer InetAddress Inet4Address Inet6Address \
         UnixNativeDispatcher jni_util jni_util_md net_util net_util_md; do
  $CC -fvisibility=hidden -w -D_GNU_SOURCE -DLINUX -I$OJ -I$HERE/framework/javacore-shim/expat-compat \
    -I$SRC/libnativehelper/include -I$SRC/libnativehelper/include_jni -I$SRC/libnativehelper/header_only_include \
    -c $OJ/$f.c -o $OBJ/oj_$f.o
done
for f in missing_natives media_natives text_natives audiorecord_natives audioports_natives ojluni_nio_natives localsocket_natives canvas_natives sqlite_natives notification_natives; do
  $CC -Wall -Wextra -Werror -I$SRC/libnativehelper/include_jni -c $HERE/framework/javacore-shim/$f.c -o $OBJ/$f.o
done
${CC/clang /clang++ } -shared -fuse-ld=lld -Wl,-soname,libwl_missing_natives.so -Wl,-z,defs \
  $OBJ/missing_natives.o $OBJ/media_natives.o $OBJ/text_natives.o $OBJ/audiorecord_natives.o $OBJ/audioports_natives.o $OBJ/ojluni_nio_natives.o \
  $OBJ/localsocket_natives.o $OBJ/canvas_natives.o $OBJ/sqlite_natives.o $OBJ/notification_natives.o $OBJ/oj_*.o \
  $OBJ/expat_natives.o $OBJ/xmlparse.o $OBJ/xmlrole.o $OBJ/xmltok.o -static-libstdc++ -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--version-script=$HERE/framework/javacore-shim/libwl_missing_natives.map -lm -lc -o $OUT/libwl_missing_natives.so
echo "built $OUT/libwl_missing_natives.so"

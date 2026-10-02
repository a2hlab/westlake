#!/bin/bash
# build_vulkan_shim.sh <oh-sdk-native> <oh-libvulkan.so> <out-dir>
# Builds libvulkan.so from framework/vulkan-shim: OH's Vulkan loader plus VK_KHR_android_surface.
# Every public vk* export of the given OH loader forwards to it through a generated trampoline,
# except the three vulkan_shim.c implements.
set -e
SDK=$1; OHVK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
OWN="vkCreateInstance vkEnumerateInstanceExtensionProperties vkGetInstanceProcAddr"
$SDK/llvm/bin/llvm-readelf -W --dyn-syms $OHVK | awk '$4=="FUNC" && $5=="GLOBAL" && $7!="UND" {print $8}' \
  | sed 's/@.*//' | grep '^vk' | sort -u > $OUT/exports.txt
python3 - "$OUT" $OWN <<'PY'
import sys
out, own = sys.argv[1], set(sys.argv[2:])
names = [n for n in open(out + '/exports.txt').read().split() if n not in own]
s = ['.hidden wl_vk_count', '.hidden wl_vk_names', '.hidden wl_vk_table', '.section .rodata', '.global wl_vk_count', '.p2align 2', 'wl_vk_count: .word %d' % len(names)]
s += ['.section .rodata.str1.1,"aMS",@progbits,1'] + ['.Ln%d: .asciz "%s"' % (i, n) for i, n in enumerate(names)]
s += ['.section .data.rel.ro', '.global wl_vk_names', '.p2align 3', 'wl_vk_names:'] + ['.quad .Ln%d' % i for i in range(len(names))]
s += ['.bss', '.global wl_vk_table', '.p2align 3', 'wl_vk_table: .zero %d' % (8 * len(names)), '.text']
for i, n in enumerate(names):
    s += ['.global %s' % n, '.type %s,%%function' % n, '.p2align 2', '%s:' % n,
          '  adrp x16, wl_vk_table', '  add x16, x16, :lo12:wl_vk_table',
          '  ldr x16, [x16, #%d]' % (8 * i) if 8 * i < 32768 else '  mov x17, #%d\n  ldr x16, [x16, x17]' % (8 * i),
          '  br x16', '.size %s, .-%s' % (n, n)]
open(out + '/vulkan_trampolines.S', 'w').write('\n'.join(s) + '\n')
print('trampolines', len(names))
PY
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Werror \
  -DVK_USE_PLATFORM_OHOS -shared -fuse-ld=lld -Wl,-soname,libvulkan.so -Wl,-z,defs \
  $HERE/framework/vulkan-shim/vulkan_shim.c $OUT/vulkan_trampolines.S -ldl -lc -o $OUT/libvulkan.so
echo "built $OUT/libvulkan.so"

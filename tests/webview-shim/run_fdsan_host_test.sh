#!/bin/sh
# Host test of the shim's fdsan block (ADR-0010 W1): extracts the block from webview_bionic_shim.c,
# builds it with the host compiler and checks the off, warn and fatal levels and descriptor reuse.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
shim="$here/../../framework/webview-shim/webview_bionic_shim.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
{
    printf '#define _GNU_SOURCE\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
    printf '#include <unistd.h>\n#include <dlfcn.h>\n#include <fcntl.h>\n#include <sys/mman.h>\n'
    sed -n '/^#define WL_FDSAN_PAGE_BITS/,/^int android_fdsan_set_error_level_from_property/p' "$shim"
    printf '{\n    (void)default_level;\n    return android_fdsan_get_error_level();\n}\n'
} > "$work/fdsan_block.c"
cp "$here/fdsan_host_test.c" "$work/"
${CC:-cc} -O2 -Wall -Wextra -Werror -o "$work/fdsan_test" "$work/fdsan_host_test.c" -ldl
"$work/fdsan_test"
WESTLAKE_FDSAN=warn "$work/fdsan_test" on 2> "$work/reports"
test "$(grep -c '^\[WL-FDSAN\] ' "$work/reports")" -eq 4
if WESTLAKE_FDSAN=fatal "$work/fdsan_test" on 2>/dev/null; then echo "fatal level did not abort"; exit 1; fi
echo FDSAN_HOST_TEST_PASS

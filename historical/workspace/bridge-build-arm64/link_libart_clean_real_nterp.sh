#!/bin/bash
set -euo pipefail

# Link libart from one clean, dependency-tracked build directory. Unlike the
# historical v100/v142 linker, this script has no frozen-object substitutions,
# filesystem-order semantics, or duplicate-definition escape hatch.

readonly ART_ROOT=/home/dspfac/art-latest
readonly BUILD=${WL_CLEAN_ART_BUILD_DIR:?set WL_CLEAN_ART_BUILD_DIR to a clean build directory}
readonly OUTPUT=${WL_CLEAN_ART_OUTPUT:?set WL_CLEAN_ART_OUTPUT to the candidate libart.so path}
readonly NDK=/home/dspfac/ohos-sdk-6.1/linux/native
readonly CXX=$NDK/llvm/bin/clang++
readonly NM=/home/dspfac/openharmony/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-nm
readonly LIBDIR=/home/dspfac/bridge-build-arm64/out/board_libs
readonly RUNTIME_BOUNDARY_SO=${WL_NTERP_RUNTIME_BOUNDARY_SO:-/home/dspfac/bridge-build-arm64/out-jit-perf-v118-boot-native-methodentered-fastpath-20260904/libwestlake_runtime_boundary.so}

required_objects=(
  "$BUILD/runtime/class_linker.o"
  "$BUILD/runtime/interpreter/mterp/nterp.o"
  "$BUILD/stubs/nterp_real_port.o"
  "$BUILD/asm_arm64/mterp_arm64ng.o"
  "$BUILD/asm_arm64/quick_entrypoints_arm64.o"
  "$BUILD/asm_arm64/jni_entrypoints_arm64.o"
  "$BUILD/asm_arm64/memcmp16_arm64.o"
  "$BUILD/sigchain/sigchain.o"
  "$BUILD/stubs/link_stubs_arm64.weak.o"
  "$BUILD/stubs/code_generator_vector_arm64_sve_stub.o"
  "$BUILD/stubs/fault_handler_stubs.o"
  "$BUILD/stubs/template_instantiations.weak.o"
  "$BUILD/stubs/metrics_stubs.weak.o"
  "$BUILD/stubs/thread_cpu_stub.weak.o"
  "$BUILD/stubs/openjdkjvm_runtime_services.weak.o"
  "$BUILD/fmtlib/format.o"
  "$BUILD/tinyxml2/tinyxml2.o"
)

for object in "${required_objects[@]}"; do
  if [[ ! -f "$object" ]]; then
    echo "missing clean-build object: $object" >&2
    exit 1
  fi
done
if [[ ! -f "$RUNTIME_BOUNDARY_SO" ]]; then
  echo "missing runtime boundary library: $RUNTIME_BOUNDARY_SO" >&2
  exit 1
fi

# Capture make's exit status as well as its output; process substitution would
# silently accept a failed inventory command. Extra stale objects are ignored.
inventory=$(make --no-print-directory -s -C "$ART_ROOT" -f Makefile.ohos-arm64 \
  BUILDDIR="$BUILD" print-clean-core-objects)
[[ -n "$inventory" ]] || { echo "empty clean object inventory" >&2; exit 1; }
mapfile -t core_objects <<< "$inventory"
for object in "${core_objects[@]}"; do
  [[ -s "$object" ]] || { echo "missing declared object: $object" >&2; exit 1; }
done

stub_objects=(
  "$BUILD/sigchain/sigchain.o"
  "$BUILD/stubs/link_stubs_arm64.weak.o"
  "$BUILD/stubs/code_generator_vector_arm64_sve_stub.o"
  "$BUILD/stubs/fault_handler_stubs.o"
  "$BUILD/stubs/template_instantiations.weak.o"
  "$BUILD/stubs/metrics_stubs.weak.o"
  "$BUILD/stubs/thread_cpu_stub.weak.o"
  "$BUILD/stubs/openjdkjvm_runtime_services.weak.o"
  "$BUILD/fmtlib/format.o"
  "$BUILD/tinyxml2/tinyxml2.o"
)

assembly_objects=(
  "$BUILD/asm_arm64/quick_entrypoints_arm64.o"
  "$BUILD/asm_arm64/jni_entrypoints_arm64.o"
  "$BUILD/asm_arm64/memcmp16_arm64.o"
  "$BUILD/asm_arm64/mterp_arm64ng.o"
)

mkdir -p "$(dirname "$OUTPUT")"
"$CXX" \
  --target=aarch64-linux-ohos \
  --sysroot="$NDK/sysroot" \
  -shared -fPIC -stdlib=libc++ -fuse-ld=lld \
  -Wl,-Bsymbolic \
  -Wl,--version-script="$ART_ROOT/ziparchive_hide.map" \
  -Wl,-soname,libart.so \
  -Wl,--allow-shlib-undefined \
  -o "$OUTPUT" \
  "$BUILD/stubs/nterp_real_port.o" \
  "${core_objects[@]}" \
  "${stub_objects[@]}" \
  "${assembly_objects[@]}" \
  -Wl,--no-as-needed "$RUNTIME_BOUNDARY_SO" -Wl,--as-needed \
  -L"$LIBDIR" -lz -lc -ldl -lpthread

chmod 755 "$OUTPUT"
md5sum "$OUTPUT"
sha256sum "${core_objects[@]}" "${stub_objects[@]}" "${assembly_objects[@]}" \
  "$BUILD/stubs/nterp_real_port.o" "$BUILD/asm_arm64/asm_defines.h" \
  "$ART_ROOT/Makefile.ohos-arm64" "$0" "$CXX" "$RUNTIME_BOUNDARY_SO" "$OUTPUT" \
  > "$OUTPUT.inputs.sha256"

readonly NTERP_SIZE=$(
  "$NM" -C -S "$OUTPUT" |
    awk '$4 == "ExecuteNterpImpl" && !found { print $2; found=1 }'
)
if [[ -z "$NTERP_SIZE" || "$NTERP_SIZE" == "0000000000000004" ]]; then
  echo "real-nterp invariant failed: ExecuteNterpImpl is still a stub" >&2
  exit 1
fi
if "$NM" -C "$OUTPUT" | grep wl_trace_ktr_calls >/dev/null; then
  echo "clean-interpreter invariant failed: legacy trace symbol is present" >&2
  exit 1
fi
if [[ $("$NM" -C --defined-only "$OUTPUT" | grep -c 'ClassLinker::ResolveMethodForNterp') -ne 1 ]]; then
  echo "resolver containment invariant failed: expected one ResolveMethodForNterp definition" >&2
  exit 1
fi
if "$NM" -C --undefined-only "$BUILD/runtime/interpreter/mterp/nterp.o" |
    grep -q 'ClassLinker::ResolveMethod<' ; then
  echo "resolver containment invariant failed: nterp still imports shared ResolveMethod" >&2
  exit 1
fi
if ! "$NM" -C --undefined-only "$BUILD/runtime/interpreter/mterp/nterp.o" |
    grep -q 'ClassLinker::ResolveMethodForNterp'; then
  echo "resolver containment invariant failed: nterp does not import its dedicated resolver" >&2
  exit 1
fi

"$NM" -C -S "$OUTPUT" |
  grep -E 'ExecuteNterpImpl|ClassLinker::ResolveMethodForNterp|artNterpAsmInstruction(Start|End)' || true
readelf -d "$OUTPUT" | grep NEEDED

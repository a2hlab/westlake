#!/bin/bash
# compile_oh_adapter_runtime.sh -- thin wrapper over tools/build_adapter_runtime.py
#
# This script used to delegate to $WORKDIR/build/build_oh_adapter_runtime_slim.sh
# with WORKDIR defaulting to /home/dspfac/bridge-build: a path on one person's
# machine, running a script that was never in this repository. It could not work
# for anyone else, and the jar it produced started from a byte-patched
# classes.dex nobody could regenerate.
#
# The real builder is tools/build_adapter_runtime.py. It compiles every class it
# ships, takes no absolute paths, and fails closed rather than emitting a jar
# with no dex in it. This wrapper exists only so the historical entry point
# still leads somewhere.
#
#   D8          path to d8            (required for a jar; not in the locked
#                                      toolchain set, so you supply it)
#   FRAMEWORK   adapter framework.jar (required for a deployable jar)
#   OUT         new output directory  (default: ./out/adapter-runtime)
#   JAVA_HOME   a JDK 11+
#
# With neither D8 nor FRAMEWORK set this runs the compile/type-check pass, which
# is what CI and a developer checking a change actually need.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${OUT:-$PWD/out/adapter-runtime}"

args=(--out "$OUT")
if [ -n "${D8:-}" ] && [ -n "${FRAMEWORK:-}" ]; then
    args+=(--d8 "$D8" --framework "$FRAMEWORK")
else
    echo "note: D8 and FRAMEWORK not both set -- compile/type-check only, no jar" >&2
    args+=(--classes-only)
fi

exec python3 "$ROOT/tools/build_adapter_runtime.py" "${args[@]}"

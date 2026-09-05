# ART JIT review fixes — 2026-09-04

Implemented source fixes for R1–R9 from the [review](ART-JIT-CODE-REVIEW-2026-09-04.md). The complete OHOS ARM64 runtime builds and links, 11 host regressions pass, and the isolated candidate starts ART and preloads the framework on-device. It is **not a replacement baseline yet**: app-level, GC/JNI stress, and performance validation remain outstanding.

Follow-up: the [Toutiao / OnePlus native comparison](ART-JIT-NATIVE-BENCHMARK-2026-09-04.md) now exercises real app launches and JIT compilation. Native parity is not demonstrated: the candidate has a blank feed and input/IME gaps, and the timings have unmatched endpoints and network conditions. The smoke-test evidence below describes the earlier stage; GC/JNI stress and acceptance remain open.

## Changes

| Review item | Implementation |
| --- | --- |
| R1/R2: wrong nterp layout and JNI flag | All ARM64 assembly and C++ assembly-support consumers use a single per-build `asm_defines.h`, generated from the target C++ layout. Removed the competing forced include. The legacy manual header rejects ARM64 consumers that bypass generation; its CriticalNative fallback is corrected too. |
| R3: inlining bypass | `Jit::IsMethodCompilationAllowed()` is shared by direct compilation and `HInliner::TryBuildAndInline()`, before inline/pattern substitution. AOT behavior is unchanged. Existing compiler intrinsic lowering is not redefined by this bytecode-admission rule. |
| R4: permanent retry suppression | Removed `requested[256]`. A safe boot method gets an early queue attempt; subsequent entries use ordinary ART hotness sampling. Queue deduplication remains owned by `JitThreadPool`, and rejection/failure/invalidation cannot permanently swallow samples. |
| R5: inconsistent boot OSR policy | One cheap ownership check excludes boot OSR in app-only mode at compilation admission, enqueue, shared `PrepareForOsr()`, and switch-interpreter entry. Allowlisted boot methods remain eligible for subsequent-entry compilation, not OSR. |
| R6: false readiness | Added PID/start-time identity, fatal/dead/zombie checks, a fresh view-response boundary, a positive-sized rectangle, a PID-owned visible buffered surface, and post-gate liveness. Polling uses a monotonic deadline and reports milliseconds. |
| R7: stale assembly dependencies | Assembly emits dependency files and depends on the generated header. C++ dependency tracking uses `-MD`, because `-MMD` omitted ART runtime headers reached through `-isystem`. Build-rule changes also invalidate declared objects. |
| R8: successful failed builds | Compilation failure branches now return failure. The clean linker uses make's complete declared core-object inventory, rejects missing/empty objects, ignores stray objects, and records input hashes. |
| R9: mismatched validated/executed inputs | The v142/v160 pinned wrappers explicitly export the verified framework directory and validate the effective APK, WebView APK, and delegated launch script. The new candidate wrapper uses the same locks and defaults to a separate socket. |

Legacy ARM64 recipes that still include the manual constants must adopt generated-header wiring; silently using that header is no longer supported. Historical v142 binary/object artifacts were not rewritten. Other historical versioned launch wrappers were not bulk-edited; use the updated wrappers or the new candidate runner for locked comparisons.

## Candidate and evidence

- [Candidate library](/home/dspfac/bridge-build-arm64/out-jit-review-fixes-20260904/libart.so): MD5 `c8b5e7352662109ddcb9ca3b97020894`; SHA-256 `ce8ecb0229bbd4d199adef54aacc532bf2704c42a957f1646ec54d7df12dd3ee`.
- [Locked candidate runner](/home/dspfac/bridge-build-arm64/run-jit-review-fixes-verify-20260904.sh): defaults to `AppSpawnXJitReview`, not the active default service. Its appspawn, bridge, and runtime-boundary binaries are unchanged v160 inputs; only ART was rebuilt here.
- [Link-input hashes](/home/dspfac/bridge-build-arm64/out-jit-review-fixes-20260904/libart.so.inputs.sha256), [changed-source hashes](/home/dspfac/bridge-build-arm64/jit-review-20260904/changed-source-inputs.sha256), [build log](/home/dspfac/bridge-build-arm64/jit-review-20260904/build.log), [link log](/home/dspfac/bridge-build-arm64/jit-review-20260904/link.log).
- [Regression tests](/home/dspfac/bridge-build-arm64/tests/test_jit_review_regressions.py), [test log](/home/dspfac/bridge-build-arm64/jit-review-20260904/tests.log): 11/11 passed. Tests exercise real shell recipes/readiness functions and import the actual C++ policy bodies into a host fixture. Runtime/queue doubles do **not** establish real ART compilation, invalidation, concurrency, or GC correctness.
- [Linked-binary disassembly](/home/dspfac/bridge-build-arm64/jit-review-20260904/abi-disassembly.txt): nterp cache lookup is `#2656` (`0xa60`), initialized allocation entrypoint is `#464` (`0x1d0`), JNI mask is `#1572864` (`0x180000`), and JNI SP clearing uses `~0x3`.
- [Device smoke log](/home/dspfac/bridge-build-arm64/jit-review-20260904/candidate-parent.log), [mapping/hash evidence](/home/dspfac/bridge-build-arm64/jit-review-20260904/device-smoke.txt): isolated PID 16494 loaded the candidate, started ART with JIT requested, preloaded the framework, and reached the separate socket's accept loop. Zero fatal-pattern matches were observed, but recoverable native-registration/security-provider diagnostics remain. No application child was launched from this candidate.

On-device validation with an inherited `ASX_FW_DIR=/wrong/fw` still validated and selected `/data/local/tmp/asx/fw`. The stricter readiness gate independently passed on the already-running original Toutiao child 4911 (`LIVE:31677318`) after 3690 ms, including a fresh dump and post-gate liveness. This is a harness smoke check, **not** a startup/performance measurement of the candidate.

The device's default shell has 32-bit arithmetic and lacks `awk`; RenderService's 64-bit creator-PID extraction therefore runs on the host. The real-device check caught both assumptions before handoff.

## Reproduce

From `/home/dspfac/art-latest`:

```sh
make -f Makefile.ohos-arm64 -j6 \
  BUILDDIR=/home/dspfac/art-latest/build-ohos-arm64-jit-review-20260904 \
  clean-runtime-objects
```

From `/home/dspfac/bridge-build-arm64`, choose a fresh output path:

```sh
WL_CLEAN_ART_BUILD_DIR=/home/dspfac/art-latest/build-ohos-arm64-jit-review-20260904 \
WL_CLEAN_ART_OUTPUT=/absolute/fresh-output/libart.so \
WL_NTERP_RUNTIME_BOUNDARY_SO=/home/dspfac/bridge-build-arm64/out-jit-review-fixes-20260904/libwestlake_runtime_boundary.so \
bash link_libart_clean_real_nterp.sh

JIT_REVIEW_BUILD=/home/dspfac/art-latest/build-ohos-arm64-jit-review-20260904 \
python3 -m unittest discover -s tests -p test_jit_review_regressions.py -v
```

The staged device runner supports `WL_JIT_REVIEW_VALIDATE_ONLY=1` for input validation without starting a process. Candidate files remain in a separate staging directory. The original default launcher was not replaced, and pinned v142 still hashes to `20e48b90b6b01468f155ed7c48097e70`.

## Remaining release gates

1. Run unchanged Noice, Material Catalog, and Toutiao against the candidate, pinned v142, and JIT-off control with identical input/state captures. No new candidate app/performance results are claimed here.
2. Exercise real JNI first-use lookup (normal/FastNative/CriticalNative, success/failure, interpreted/compiled callers), repeated GC, callbacks, exceptions, deoptimization, and native-frame walking. The historical bad-root crash is not diagnosed or fixed by inference.
3. Verify the inline boundary in compiler IR/emitted app code, retry after real queue stop/start and code invalidation, and the boot OSR exclusion through both interpreters.
4. Broaden boot-code support only after metadata/layout and GC-root contracts are validated. Full source/toolchain reproducibility, all dynamically loaded native-provider hashes, and opt-in JIT debug metadata remain larger work items; the recorded manifest is an object/input identity record, not a complete hermetic source package.

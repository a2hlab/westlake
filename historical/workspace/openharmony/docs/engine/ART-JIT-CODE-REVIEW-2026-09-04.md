# ART JIT optimization code review — 2026-09-04

Implementation follow-up: [R1–R9 source fixes, build/test evidence, and remaining runtime gates](ART-JIT-FIXES-2026-09-04.md). The findings below describe the pre-fix reviewed state; the isolated candidate has not replaced the pinned baseline.

The largest newly confirmed optimization defect is in the clean v152/v160 runtime: generated ARM64 nterp reads its interpreter cache at a different offset from the C++ code that populates it. There is also a confirmed JNI native-method flag mismatch in both that runtime and the saved v142 baseline. These should be addressed before further threshold tuning.

This review covers the Android ART JIT used by the OpenHarmony bridge, including compiler policy, inlining, interpreter transitions, OSR, JNI/assembly contracts, code-cache lifecycle, DexCache shortcuts, build composition, launch configuration, and validation. It does not cover Ark/JavaScript JIT, and is not an exhaustive audit of every unchanged upstream compiler pass.

Source inspection used the current dirty working trees in `/home/dspfac/aosp-art-15`, `/home/dspfac/art-latest`, and the bridge sources/scripts. ART HEAD was `42944515f999eb7c4a13bba4a303ca255c6328c1`; the reviewed working changes extend beyond that commit. No implementation or device state was changed. Historical test results below were read from saved evidence, not rerun on the device.

## Artifact identity

| Local artifact checked | MD5 verified during review | Relevant distinction |
| --- | --- | --- |
| `out-jit-perf-v142-nterp-trust-matched-dexcache-pinned-20260904/libart.so` | `20e48b90b6b01468f155ed7c48097e70` | Named accepted by the handoff; frozen object composition; correct nterp cache offset |
| `out-jit-v152-reproducible-sqlite-20260904/libart.so` | `8349874106302b6e492823563f332575` | New clean build; incorrect nterp cache offset |
| `out-jit-v160-reproducible-osc-adapter-20260905/libart.so` | `8349874106302b6e492823563f332575` | Same ART bytes as v152; bridge/boundary changes are separate |

Artifact paths in this table are under `/home/dspfac/bridge-build-arm64`. The v160 filename is recorded as found; it does not establish which runtime is currently deployed. The [handoff](/home/dspfac/bridge-build-arm64/TOUTIAO-HANDOFF.md:9) still names v142. Its performance results cannot be transferred to the newer clean runtime.

## Findings

P1 denotes a high-priority correctness or substantial execution-path defect. P2 denotes a functional, lifecycle, build, or validation defect that should be fixed. Binary-confirmed findings are distinguished from source-path findings; none is claimed to explain a particular recorded crash without a reproducer connecting them.

### R1 — P1: clean nterp looks up the wrong interpreter-cache slot

Locations: [Makefile.ohos-arm64](/home/dspfac/art-latest/Makefile.ohos-arm64:397), [manual constants](/home/dspfac/art-latest/stubs/asm_defines.h:140), [assembly cache lookup](/home/dspfac/aosp-art-15/runtime/interpreter/mterp/arm64ng/main.S:1694), [C++ cache update](/home/dspfac/aosp-art-15/runtime/interpreter/mterp/nterp.cc:129).

The build generates the correct `THREAD_INTERPRETER_CACHE_OFFSET = 0xa60`, then supplies it with `-include`. The assembly subsequently includes `runtime/asm_support.h`, which finds `stubs/asm_defines.h` first and redefines the offset to `0xa50`. Preprocessing the actual generated assembly confirmed the final value is `0xa50`.

Disassembly of the clean v160 artifact confirms both sides of the mismatch:

```text
nterp_op_invoke_virtual:
  add x16, x19, #2640       // lookup base: Thread + 0xa50

NterpGetMethod cache update:
  str x21, [x8, #2656]      // key: Thread + hash*16 + 0xa60
  str x20, [x8, #2664]      // value
```

Each normal C++ cache fill is one entry beyond the slot read by assembly. Repeating the same instruction therefore misses instead of reusing its cached resolution. Invoke, field, allocation, and other handlers using this macro pay repeated slow-path work. This is a binary-confirmed regression relative to v142, whose lookup uses `#2656`. No wall-time magnitude was measured in this review.

The same override also changes the object-allocation entrypoint from generated `0x1d0` to manual `0x1c8`; `nterp_op_new_instance` in the clean binary loads `#456`, while v142 loads `#464`. Those are the adjacent resolved/initialized allocation entrypoints. Correcting only the cache constant would leave this second discrepancy.

Fix direction: give all ARM64 assembly one authoritative generated header, prevent the manual header from overriding it, and verify the effective preprocessed constants and emitted instructions. A repeated invoke/field/allocation microbenchmark should prove cache reuse, not merely successful execution.

### R2 — P1: JNI lookup uses the wrong ART 15 CriticalNative bit

Locations: [manual CriticalNative constant](/home/dspfac/art-latest/stubs/asm_defines.h:4), [ART 15 flags](/home/dspfac/aosp-art-15/libdexfile/dex/modifiers.h:78), [JNI dispatch](/home/dspfac/aosp-art-15/runtime/arch/arm64/jni_entrypoints_arm64.S:103).

The shim defines `ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE` as `0x200000`; ART 15 defines it as `0x100000`. In ART 15, `0x200000` is the nterp invoke-fast-path flag. Consequently, both inspected runtime artifacts test `0x280000` for FastNative/CriticalNative, instead of `0x180000`.

For an unresolved CriticalNative reached through the GenericJNI lookup route, a method without the unrelated nterp bit can be sent to `artFindNativeMethod` while the thread is Runnable. That function expects Native state; [the runnable resolver has a different contract](/home/dspfac/aosp-art-15/runtime/entrypoints/jni/jni_entrypoints.cc:128). Conversely, a normal native carrying the nterp bit can be classified as fast. Eager native registration can hide the problem by avoiding lookup.

Fix direction: use the generated ART 15 flags for JNI assembly as well as nterp. Validate normal, FastNative, and CriticalNative first-use lookup, including failed lookup, GenericJNI, and compiled callers. The wrong instruction mask is proven; a device crash from this trigger was not reproduced here.

The saved v142 JNI lookup additionally clears only SP tag bit 0 (`and ... #...fffe`), whereas the current ART contract uses mask `0x3`. The clean binary clears both bits. This is further evidence that v142 is not equivalent to a fresh build of the current JNI source.

### R3 — P1: the boot-method restriction does not contain inlined boot code

Locations: [JIT admission policy](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:232), [top-level compiler gate](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:334), [inlining eligibility](/home/dspfac/aosp-art-15/compiler/optimizing/inliner.cc:1467), [pattern substitution](/home/dspfac/aosp-art-15/compiler/optimizing/inliner.cc:1630).

`WESTLAKE_JIT_APP_ONLY` is described as keeping compatibility-sensitive boot methods interpreted except for an allowlist. It filters separately compiled methods. The inliner does not consult that policy: an allowed app method can inline a verified, compilable boot method outside the allowlist. Pattern substitution also runs outside the top-level JIT gate. The launcher does not disable inlining, and the JIT initializes the normal 32-code-unit inline limit.

Thus the restriction is not a guarantee that excluded boot bytecode or its field accesses never execute as generated code. This matters while the policy explicitly cites unresolved boot field-access compatibility as its reason for existing. This is a source-confirmed policy hole, not proof that any particular inlined method miscompiles.

Fix direction: either resolve the underlying boot metadata/runtime incompatibility and remove the restriction, or enforce the intended temporary boundary at inline and substitution decisions too. Verify a small app caller of a non-allowlisted boot leaf using compiler IR or emitted code, with inlining enabled.

### R4 — P2: a failed or invalidated boot JNI compilation can be suppressed permanently

Locations: [one-shot requested table](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:2082), [sampling early return](/home/dspfac/aosp-art-15/runtime/jit/jit-inl.h:35), [stopped queue handling](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:2180), [code invalidation](/home/dspfac/aosp-art-15/runtime/jit/jit_code_cache.cc:1753).

`requested[256]` records the method before `AddCompileTask` and permanently returns success for subsequent samples. There is no removal after queue rejection, compile failure, or invalidation. In particular, a stopped thread pool silently rejects the task. Later native entries still hit the recorded pointer and bypass ordinary sampling, so the JNI stub need not ever be retried. Native methods cannot recover through nterp's separate hotness route.

The insertion loop also advances after a failed CAS even when the winning value is the same method, allowing racing calls to occupy more than one table slot. ART's real queue still deduplicates tasks, but this bounded table wastes capacity and does a linear scan on repeated generic-JNI calls.

Fix direction: use the existing queue's pending-task deduplication or an explicit retryable state that follows compile completion and invalidation. Test stop/start, forced compilation failure, and JNI-code invalidation followed by renewed calls.

### R5 — P2: the claimed boot OSR exclusion applies only to the switch interpreter

Locations: [switch OSR exclusion](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:676), [nterp hot-method path](/home/dspfac/aosp-art-15/runtime/interpreter/mterp/nterp.cc:703), [OSR enqueue](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:1955), [OSR preparation](/home/dspfac/aosp-art-15/runtime/jit/jit.cc:564).

The switch path rejects bootstrap OSR under app-only policy. Nterp calls `PrepareForOsr` directly, bypassing that check, and `MaybeEnqueueCompilation` can enqueue OSR for allowlisted boot methods that already have JIT code. The current runtime actively promotes boot methods to nterp before fork, so this is relevant to the enabled execution path.

This is inconsistent policy and missing validation, not a finding that upstream OSR is inherently incorrect. Fix direction: define whether allowlisted boot methods may OSR, enforce that decision at the shared enqueue/entry points, and test both interpreters with an already-running boot loop.

### R6 — P2: the readiness gate can report a crashed child as launched

Location: [ttwalk.sh readiness function](/home/dspfac/bridge-build-arm64/ttwalk.sh:195).

The function checks historical view-tree text and a visible buffered surface, then returns success at line 229. Fatal-marker inspection happens afterward. There is no required live-process check before success, and the surface is selected by activity name rather than bound to the child PID. Stale view/surface evidence can therefore satisfy the gate after a fatal event.

An isolated execution of the actual function with mocked device reads supplied a ready hierarchy/surface and a fatal count of 1. It printed `LAUNCHED child=999999 ...` without ever consulting the fatal count. No device commands were executed.

Fix direction: validate PID/start identity and fatal/process state before accepting readiness; bind the surface and a fresh view response to that child; retain a short post-gate liveness check. Existing independently captured control evidence remains useful, but this gate alone is not a correctness oracle.

### R7 — P2: changing assembly constants does not rebuild quick/JNI entrypoints

Location: [assembly build rules](/home/dspfac/art-latest/Makefile.ohos-arm64:715).

Quick and JNI assembly targets depend only on their top-level `.S` file. They neither list the included constants/macros as dependencies nor emit dependency files. The newer C++ dependency tracking does not cover these targets.

Read-only reproduction:

```sh
make -f Makefile.ohos-arm64 -n -W stubs/asm_defines.h \
  BUILDDIR=build-ohos-arm64-v150-repro3-20260904 \
  build-ohos-arm64-v150-repro3-20260904/asm_arm64/jni_entrypoints_arm64.o \
  build-ohos-arm64-v150-repro3-20260904/asm_arm64/quick_entrypoints_arm64.o
```

Both objects were reported up to date. A developer can fix R2 in source and still link the old instructions. Fix direction: add assembler dependency generation and make the generated layout header an explicit prerequisite for all consumers. Include an incremental rebuild test.

### R8 — P2: a compiler failure can produce a successful make result

Location: [general ART compilation recipe](/home/dspfac/art-latest/Makefile.ohos-arm64:1011); similar recipes appear throughout the file.

The error branch logs `FAIL` and removes the target, but does not return failure. Successful `rm -f` becomes the recipe's exit status. An isolated temporary build with `CXX=false` targeting `compiler/jit/jit_compiler.o` produced:

```text
FAIL: jit_compiler.cc
make_exit=0
object_exists=no
```

This invalidates make success as a build-completeness check. Both link scripts collect existing objects with `find`; the clean linker's explicit required-object list is only partial. A later link may catch some missing definitions, but must not be the primary error detector, especially with compatibility fallbacks present.

Fix direction: propagate compiler failures, use the declared object list for linking, and verify expected objects. The temporary test directories were empty afterward and removed; no runtime artifacts were changed.

### R9 — P2: the pinned runner can validate one framework directory and run another

Locations: [v160 framework hash checks](/home/dspfac/bridge-build-arm64/run-jit-v160-reproducible-osc-adapter-verify-20260905.sh:7), [exports/delegation](/home/dspfac/bridge-build-arm64/run-jit-v160-reproducible-osc-adapter-verify-20260905.sh:80), [common runner override](/home/dspfac/bridge-build-arm64/run-jit-aot-candidate-20260902.sh:6).

The pinned wrapper verifies jars under its local `FW=$ASX/fw`, but does not set or clear `ASX_FW_DIR`. The delegated runner prefers an inherited `ASX_FW_DIR`, then constructs the boot class path from it. An inherited override therefore escapes the wrapper's claimed input lock. The same pattern exists in the v142 wrapper. ART may reject an incompatible image later, but that does not make the experiment's validation contract correct.

The wrappers also do not hash the selected APK, WebView APK, or delegated launch script. These remain additional reproducibility gaps when claiming identical application inputs across runs.

Fix direction: resolve the complete effective launch configuration first, validate those actual files, and pass it unchanged to launch. Record intentional JIT-off/baseline/profile overrides as part of the experiment identity.

## Remaining gaps requiring runtime evidence

| Area | Evidence and limitation | Required closure |
| --- | --- | --- |
| GC and JNI root integrity — high priority | Saved v142 profiling run aborted while marking `0x1`; an unprofiled v137 control also aborted. Current code still contains JNI state recovery and a stack-walker fallback for missing native frame metadata. No causal link to R1/R2 is established. | Repeated GC stress across GenericJNI, compiled JNI, exceptions, recursion, deoptimization, and native-to-Java callbacks; identify the writer/owner of the bad root. |
| General boot/framework compilation | App-only mode still permits a hand-selected set of JSON, collection, lock, string, and native methods. Most boot methods remain excluded from separate JIT compilation. | Repair and validate the common runtime/metadata contract, then broaden coverage with unchanged apps and independent controls. |
| DexCache and cross-version metadata | The current dedicated `ResolveMethodForNterp` limits trust to boot DexFile identities, which improves containment. Saved evidence still records bad-type repairs; method-cache success does not prove type/field layout correctness. | Resolve image/core-library identity and layout issues, retain type validation, and test app/plugin/multidex caches plus mismatched-image rejection. |
| JIT observability | The launcher disables full and mini JIT debug ELF generation, and compiler defaults disable mini debug info globally. This limits method-level attribution and debugging. | A bounded, opt-in symbol/metadata path that survives repeated compilation and code deletion; queue time, compile duration, code size, and deoptimization counters. |
| Regression coverage | The port makefile excludes upstream compiler/runtime tests. No tests referencing the new boot-sampling policy or dedicated nterp resolver were found in the searched ART test trees. Saved app controls are smoke tests, not exhaustive compiler validation. | Run a selected ART regression suite on this port, including inline/pattern substitution, JNI lookup/returns, OSR, GC roots, reflection, class unloading, volatile/Unsafe operations, and concurrent cache publication. |
| Source-to-baseline reproducibility | The v142 linker substitutes frozen objects and allows duplicate definitions; the newer clean linker sorts objects and weakens fallbacks. R1 demonstrates that the clean rebuild does not preserve v142 behavior automatically. | One source/toolchain/configuration manifest, complete object ownership, generated ABI checks, and repeated functional/performance comparison before declaring a replacement baseline. |

Specific GC evidence: [v142 bad root](/home/dspfac/bridge-build-arm64/jit-profile-20260904/v142-nterp-trust/profile/failed-run/adapter_child_1021.stderr:8580), [v137 control bad root](/home/dspfac/bridge-build-arm64/jit-profile-20260904/v142-nterp-trust/v137-content-control-failed/adapter_child_3748.stderr:9631). Relevant recovery code: [JNI return state](/home/dspfac/aosp-art-15/runtime/entrypoints/quick/quick_jni_entrypoints.cc:135), [stack-frame fallback](/home/dspfac/aosp-art-15/runtime/stack.cc:891). These are investigation targets, not diagnosed causes.

## Performance interpretation

The saved [v142 A/B report](/home/dspfac/bridge-build-arm64/jit-profile-20260904/V142-RESULTS.md:44) records 7.7% lower new-parent CPU ticks and 6.8% lower child-respawn CPU ticks than v137. The [JIT-off comparison](/home/dspfac/bridge-build-arm64/jit-profile-20260904/V148-V149-AND-JIT-OFF-RESULTS.md:49) records child medians of 4680 ticks with JIT and 6153 without it: 23.9% less CPU. These are prior measurements on the pinned v142 artifact.

Both modes reported a 14-second child wall median. That does not prove all remaining latency is outside JIT: the harness sleeps four seconds between polls, waits two more seconds for view collection, and adds device-command overhead. New-parent timing also starts before parent teardown/startup and spawn. Fine-grained monotonic timestamps are needed for spawn, first draw, content-ready, and first responsive input, alongside CPU/scheduler/GC data.

The partial profiling run shows substantial nterp work, but it is from a run that aborted under profiling; it is not a clean end-to-end critical-path measurement. The report's native-speed parity claim correctly remains unmade. There is no native Android reference measurement in the reviewed evidence.

Lower thresholds, global baseline compilation, direct optimized app compilation, and the folded interpreter-cache hash already have negative experimental results. Those should not be presented as unimplemented easy wins. The newly confirmed clean-build offset regression is a different issue from the rejected cache-hash experiment.

## Checks performed and suggested order

Performed during this review: source/diff and call-path inspection; MD5 checks of the three named runtime artifacts; disassembly of JNI lookup, nterp invoke, allocation, and cache-fill code; effective assembly macro preprocessing; incremental make dry-run; isolated forced compiler failure; isolated execution of the readiness function with mocked device reads; shell syntax checks for the selected linkers, runners, and harness. Shell syntax checks passed. No full ART suite, new device launch, throughput benchmark, or end-to-end rebuild was run.

Recommended implementation order:

1. Fix the generated/manual assembly-header conflict and CriticalNative constant; rebuild all affected assembly and check binary offsets/masks.
2. Fix build dependency and failure propagation, plus the readiness/input-lock defects, so subsequent comparisons are trustworthy.
3. Make boot-method admission, inlining, OSR, and retry behavior consistent; add focused regressions for those paths.
4. Close the GC/JNI root-integrity issue with controlled stress before expanding the supported boot-code set.
5. Re-measure the corrected clean runtime against pinned v142 with finer timing and identical application state, then target measured remaining work.

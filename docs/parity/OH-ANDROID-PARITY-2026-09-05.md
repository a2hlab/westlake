# Android reference follow-up: JIT stalls, native streams and window power policy

This follow-up fixes the native worker abort, process CPU-time API and screen-on window flags, and removes the repeated page-load tail in a controlled JIT memory-backing comparison. It follows `OH-GAP-RESUME-2026-09-05.md`. Evidence and runnable probes are in `/home/dspfac/bridge-build-arm64/gap-repair-20260904`. The Android reference is the connected OnePlus 6, with its validated Windows USB VPN. All OH app probes retain the ordinary `normal_hap` domain, zero process capabilities and isolated copies of application data. Candidate26 and its helpers are opt-in; the accepted global runtime and profile are preserved.

## Native worker abort

The baseline trace identifies the abort in a forked `libumeng-spy.so` worker, not the main Toutiao process. Its call chain reaches OH libc `fgets` with a null stream returned by `popen`. The native library queries Android properties with shell commands. Independent probes show that this app domain cannot execute `/system/bin/sh`: OH musl reports the `posix_spawn` exec denial immediately as a null `popen` result.

Android bionic instead returns the opened stream after a successful fork. If the child cannot execute its shell, reads reach EOF and closing the stream returns exit status 127. The generic `oh_popen_boundary.cpp` restores that lifecycle at the OH boundary. It retains the caller's credentials and exec restrictions, returns actual command output and child status, and does not fabricate property values. Source reference: [bionic stdio implementation](https://fuchsia.googlesource.com/third_party/android.googlesource.com/platform/bionic/+/01e7576d8ba146a73fe9b1c3eb67130c471e06f0/libc/stdio/stdio.cpp).

The same native contract probe passes on Android and OH for reading, writing, bidirectional streams, nonzero exits, signal termination, `pclose` and Android's wait-status-returning `fclose`. An exec-denial seccomp control produces a valid EOF stream and status 127 on both. The concurrent stress probe completes 64 streams across four threads and 12 concurrent fork children without leaked descriptors or unreaped children. `liboh_popen_v24.so` rebuilds byte for byte; SHA-256 is `d171bfa603b42a6e0c6e896665392ac1c839a1a8ec06ba8518c4213759286fda`.

## Process CPU time and screen-on windows

`Process.getElapsedCpuTime()` previously threw `UnsatisfiedLinkError`. `oh_process_cpu_time.cpp` now registers the native and uses `CLOCK_PROCESS_CPUTIME_ID`, truncating to milliseconds just as AOSP does. The overlay preserves the pinned runtime's other Process registrations. A public-API busy/sleep test passes on both devices; OH reports roughly 330 ms of process CPU during a 300 ms busy phase and 2–3 ms during a 300 ms sleep.

Android `FLAG_KEEP_SCREEN_ON` now maps to the owning OH session. Public OH window APIs cannot find the bridge's direct SceneBoard sessions, so `oh_window_policy.cpp` uses the existing session remote and the platform's session-property IPC format. The helper compiles against the bridge/platform declarations, with checked object offsets for the pinned binary. No platform vtable is reconstructed. Asynchronous submission is distinguished from independent server-side verification.

The mapping handles synchronous relayout, asynchronous flag-only relayout, and removal. The first prototype covered only synchronous relayout: clearing a flag changed ViewRoot's attributes but left the native lock active. The corrected candidate26 handles both paths.

The identical window control APK on Android/OH has SHA-256 `d397f7841619984efdb134f528072283427a13575aab672ef4211425f8d15602`. Native power dumps verify main-window on/off, dialog on/removal, and idle hold/release. OH's screen-lock counts are 1, 0, 1, 0, 1, 0 through those phases; the dialog owns a separate session. OH remains awake beyond its 30-second timeout while held, then sleeps after release. Android's window wake lock follows the same flag transitions. Android retains its original 30-minute screen timeout, so its sleep time is not compared.

The control also exposes an open feature gap: OH currently returns null for `Context.getSystemService(POWER_SERVICE)`. Window-flag correctness is verified through the external native power dumps; it does not imply a complete Android PowerManager facade.

## Performance evidence

Two repeats use identical control APK SHA-256 `8a0065888a673add9c0c4027474622d6d6f31c55e537d7985ba59b9cf304a11b`, a locked local HTML page, 12 page loads excluding the first two, and 12 injected tap releases. The display remains on and each measured page reports focus=true and visibility=0.

| Run | Page-ready median / p90 | Release-to-click median | Release-to-frame-callback median |
| --- | ---: | ---: | ---: |
| OH resume26bencha | 102 / 241 ms | 9.5 ms | 19 ms |
| OH resume26benchb | 98 / 229 ms | 9.5 ms | 18 ms |
| Android oneplus24a | 125.5 / 135 ms | 19 ms | 32 ms |
| Android oneplus24b | 121 / 143 ms | 17.5 ms | 29 ms |

These initial runs reproduce the OH tail at loads 10 and 11. The subsequent controlled memory-backing comparison below removes it without disabling JIT. Device hardware, WebView versions and injection protocols differ. Visual-state/Java frame callbacks are not physical presentation or touch-to-photon measurements, and this is not a matched live Toutiao benchmark. Raw samples, phase CPU times and nearest-rank p90 are preserved in `resume26-benchmark-summary.json`.

An initial diagnostic named `resume26benchinterp` still compiled Java methods: unsetting the force-JIT flag did not disable ART's default JIT. It is excluded as an interpreter control. The launcher now explicitly supplies the supported no-JIT VM option; forced interpreter mode also explicitly disables AOT. Requested modes alone are never treated as proof of active runtime behavior.

## JIT memory-backing correction

The explicit no-JIT control repeats at 99/106 ms and 100/112 ms median/p90, without the late-load stalls. This narrows the cause to the JIT-enabled configuration, which also includes profile saving. The recorded CPU profile gives a more specific discriminator: about 93% of sampled JIT-thread work is in code-cache reserve/commit, mostly `mprotect` entering OH XPM reporting and kernel console output, rather than compiler optimization passes.

The existing anonymous fallback changes the cache between RX and RWX while publishing code. An independent probe under the ordinary app domain confirms that memfd RX mapping is denied but an app-owned, unlinked file permits separate shared RW and RX views; 64 code updates execute correctly. The same test passes with `O_TMPFILE`.

`libart-v27.so` adds opt-in `WESTLAKE_OH_JIT_FILE_CACHE_DIR` inside the existing private-cache fallback. It requires an absolute, nonsymlink directory owned by the app and inaccessible to other users. `O_TMPFILE` creates a private 0600 backing file with no pathname. ART probes RX mapping under the existing kernel policy, then uses its ordinary dual-view code/data cache. Failure retains the previous permitted anonymous fallback. Zygote sharing and callers prohibiting the existing fallback retain their prior behavior. No OS policy, kernel reporting setting or compiler threshold changes.

The link inventory contains 454 objects; only `runtime/jit/jit_memory_region.o` differs from v16. Framework candidate26, appspawn v15, APK, JIT threshold, network and other helpers remain fixed. Four alternating runs use the *same v27 binary*, toggling only the explicit file-cache directory:

| Backing / run | Page-ready median / p90 | Process CPU at last page |
| --- | ---: | ---: |
| Private file / resume27filea | 96.5 / 114 ms | 4,634 ms |
| Anonymous / resume27anona | 101 / 309 ms | 5,214 ms |
| Private file / resume27fileb | 95 / 105 ms | 4,579 ms |
| Anonymous / resume27anonb | 102.5 / 261 ms | 5,076 ms |

Median p90 across repeats falls from 285 to 109.5 ms (61.6%); median process CPU through the final page falls from 5,145 to 4,606.5 ms (10.5%). Java JIT compilation succeeds in every run. `/proc` maps independently confirm separate RW and RX views of the same deleted app-cache inode, with no RWX mapping of that file. The private-file runs finish loads 10/11 in 95/100 ms and 103/96 ms. The Android reference remains 121–125.5 ms median and 135–143 ms p90 for the same APK; hardware/version differences still preclude a general speed-parity claim.

Raw samples and phase CPU times are in `resume27-benchmark-summary.json`, with runtime maps and identity snapshots beside each run. The accepted global ART and Toutiao JIT policy remain unchanged; use the new backend explicitly in isolated launches.

## Additional reference result

The same standalone socket-close activity passes all 16 cases on Android and OH, covering two timeout modes and short/long delays before closing a blocked socket. Wakeup after close is 0–4 ms on Android and 0–1 ms on OH. This narrows the earlier Android failures to the combined concurrent stress context; it does not label Android's socket-close implementation broken or close that combined-stress investigation.

## Final regression and remaining work

The final v27 build passes 17 IME edge groups, 10 networking groups, four certificate groups, a four-certificate standard trust chain, fail-closed permission checks, JavaScript/local WebView and live HTTPS loading. Those controls use the new private-file JIT cache and native process-stream boundary.

The unchanged Toutiao APK is SHA-256 `a1112a0c941f865847c2fab9138cf815735268fbfcd41d697412fb80992c7395` on both devices. OH's final traced run has a populated feed, three observed `libumeng-spy.so` calls to the corrected `popen`, successful Java JIT compilation and zero fatal-signal markers. OnePlus also loads the feed without fatal-signal markers in its captured log.

Physical Pinyin input on OH changes the search field from composing `nihao` to committed `你好`. Live search reaches a complete DOM titled `你好-头条搜索` with 1,919 characters; a physical swipe advances scrollY from 0 to 595. The first attempt was interrupted by the screen timeout and committed raw Latin text; its evidence is retained separately. The passing check verifies the actual search-field text, rather than matching unrelated suggestions in the view tree.

The debug run is stopped and the port 9222 forward removed. A fresh launch without popen tracing or WebView debugging is left running: OH child **32680**, parent **32523**, run `resume27ready`. Its feed, successful JIT, separate RW/RX mappings, ordinary app identity and zero capabilities are verified. Android Toutiao PID **24084** and Windows gnirehtet relay PID **2688** are running, with a freshly validated USB VPN. These PIDs describe the final verification snapshot and can change after restart. The OH screen timeout is restored to 30 seconds with no override. The accepted ART MD5 and both original/accepted application-data inodes remain unchanged.

`verify_followup27.py` passes all frozen evidence assertions and writes `followup27-verification.json`, including hashes of the records it consumes. It explicitly records `all_gaps_closed=false`, `global_runtime_replaced=false`, and the limits of the performance measurement. Run the read-only evidence check with:

```sh
python3 /home/dspfac/bridge-build-arm64/gap-repair-20260904/verify_followup27.py
```

Reproduction sources and helpers in that evidence directory include `build_popen_boundary.py`, `build_window_policy.py`, `build_candidate.py`, `stage_candidate.py`, `mirror_candidate.py`, `run_benchmarks27.py`, and the native contract/stress probes. The ART object inventory, source snapshot, build/link logs and hashes are in `resume27-art/`; only the JIT memory-region object was rebuilt against the frozen v16 build. Benchmark run names are immutable and must be changed for a new trial. `stop_probe.py` verifies process identity before stopping a recorded run.

The clean launch uses candidate26, appspawn v15, ART v27, the private runtime, `--jit-anon-fallback --jit-file-cache`, network helper v4 and popen helper v24, with a separate snapshot of the accepted application data. Its exact invocation is reproducible after stopping its recorded process and choosing a fresh run name:

```sh
cd /home/dspfac/bridge-build-arm64/gap-repair-20260904
python3 stop_probe.py resume27ready
python3 launch_probe.py --run parity-next --candidate candidate26 \
  --app toutiao --snapshot-accepted-data --spawn-helper appspawn-x-v15 \
  --art-helper libart-v27.so --private-runtime --jit-anon-fallback --jit-file-cache \
  --network-helper liboh_network_jni_v4.so --popen-helper liboh_popen_v24.so
```

Physical presentation, rotation/floating-keyboard behavior, the incomplete PowerManager facade, the combined Android socket-close stress investigation, and broader live-workload performance validation remain open. No compiler optimization or general native-speed parity is claimed.

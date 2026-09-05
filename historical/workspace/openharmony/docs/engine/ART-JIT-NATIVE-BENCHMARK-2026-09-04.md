# Toutiao: new JIT versus attached OnePlus 6 — 2026-09-04

Native end-to-end performance parity is **not demonstrated**. The candidate runs Toutiao and performs JIT compilation, but home startup is slow under the instrumented surface gate, Recommended content is blank, two tested on-screen return controls do not navigate, and the keyboard is absent on Search. This experiment does **not** isolate JIT as the cause of these gaps.

## Results

| Measurement | Native OnePlus 6 | OHOS / new JIT |
|---|---|---|
| Process-cold launch, five repeats | First-draw median **1,439 ms**; range 1,333–1,453 ms | Visible buffered MainActivity surface: median bracket **8,450–10,190 ms** |
| Recommended feed | Populated headlines and images observed | Blank at launch checks and again approximately **118 s** after the final launch |
| Open one feed article | Title, lead text and image visible within **0–1,890 ms** capture bracket, one sample | Not measurable: no corresponding visible feed article to select |
| Open Search | **0–1,613 ms**, keyboard shown | **0–1,604 ms**, keyboard absent; later network-error/Retry screen |
| Search back-arrow tap | Home visible within **0–1,605 ms** | Intended transition not observed during approximately 3 s capture window |
| Open sign-in from bottom profile tab | **0–1,637 ms** | **771–2,598 ms**, keyboard absent |
| Close sign-in with X | Home visible within **0–619 ms** | Intended transition not observed during approximately 3 s capture window |

The launch endpoints differ: Android's `am start -W TotalTime` is first draw; the OHOS gate polls a PID-owned, visible surface with allocated buffers. The OHOS bracket includes host transport and renderer-inspection overhead and is not a frame-presentation timestamp. **Do not report an exact slowdown ratio from these columns.** Neither launch metric is full page/content completion.

Response intervals are conservative screenshot bounds, **not actual latency estimates, equality evidence, or percentiles**. Each paired action has one sample. A zero lower bound means capture could not resolve the delay. The two Search bounds overlap almost completely and do not establish equivalent response speed. Failure means the intended transition was absent in the saved observation window, not proven permanently impossible. An untimed KEYCODE_BACK subsequently returned OHOS from Search to its home tree.

### Individual launch samples

| Trial | Native first draw (ms) | OHOS lower–upper bound (ms) | OHOS PID |
|---|---:|---:|---:|
| 02 | 1,446 | 6,670–8,278 | 13841 |
| 03 | 1,439 | 8,504–10,195 | 14517 |
| 04 | 1,349 | 8,673–10,370 | 15253 |
| 05 | 1,453 | 6,829–8,840 | 15971 |
| 06 | 1,333 | 8,450–10,190 | 16640 |

The native app also displayed a sign-in prompt in a later settled capture. Dismissing it exposed a populated feed. Thus the five native first-draw results must not be relabeled five verified prompt-free/content-ready launches. The article trial opened the visible headline beginning “人口大迁徙已成定局”; its initial readable viewport was verified, not the entire article or network-idle state.

## Identity and conditions

- Exact unchanged Toutiao APK on both devices: package `com.ss.android.article.news`, version `13.9.0` / code `13900`, MD5 `cc99caaaa3baffbb100118753fa1d130`, local SHA-256 `a1112a0c941f865847c2fab9138cf815735268fbfcd41d697412fb80992c7395`. Both device APK MD5 values were rechecked.
- Candidate ART MD5 `c8b5e7352662109ddcb9ca3b97020894`, SHA-256 `ce8ecb0229bbd4d199adef54aacc532bf2704c42a957f1646ec54d7df12dd3ee`. Each measured OHOS child mapped `/data/local/tmp/asx/out-jit-review-fixes-20260904/libart.so`; the final device hash was checked again.
- Candidate launched through the hash-locked `run-jit-review-fixes-verify-20260904.sh`, separate socket `AppSpawnXJitReview`, parent PID 9623. Its appspawn, bridge and boundary libraries are the runner's pinned v160 inputs. JIT forced on, profile-driven option off, threshold 2000, `WL_TOUCH_ENQUEUE=1`, `WL_TOUCH_FORCE_CLICK=0`.
- OnePlus serial `cfb7c9e3`, model `ONEPLUS A6003`, 1080×2280, density 450. Live release property is **Android 15 / SDK 35** despite an Android-11-looking build fingerprint. This is the attached phone's native baseline, not a claim about stock OnePlus firmware.
- OHOS serial `5cdbf6af00000000000000000923012c`, OpenHarmony `6.1.0.31`, 1200×1920. CPU0/CPU7 maximum frequencies read 2.184/2.704 GHz, CPU0 governor `performance`. Thermal-zone0 was approximately 53–57 °C in later checks. Different hardware and render resolution are not normalized.
- Networks remain unmatched: OHOS `fw-internet` on 5 GHz; OnePlus `Blink` on 2.4 GHz. Link conditions vary. No network/password changes were made by the benchmark.
- Toutiao was installed on the OnePlus for the test; the user handled initial consent. App data and caches were not cleared. OHOS had existing app data/history and pinned OAT inputs; native was a fresh install followed by repeated use. Process-cold is **not** cold storage, cold network, or a controlled compiler-profile state.
- Device clocks were not used for cross-device elapsed comparisons. Host monotonic timestamps bracket captures and OHOS launch queries. Android supplies its own first-draw duration.

## Protocol and evidence

Five paired runs force-stopped only native Toutiao and terminated only the identified candidate OHOS child, then relaunched each. The OHOS parent was retained, as Android's system runtime is retained across native force-stops. The OHOS gate checked child liveness/fatal markers, decoded ownership from the surface ID, required visible MainActivity and nonzero buffer allocation, then collected screenshots and a fresh view dump **after** the timed gate. Earlier `ohos-launch-01` used a different gate ordering and is excluded. The first native install/consent launch is also excluded.

Input uses native `adb shell input tap` versus the existing PID-qualified OHOS input mailbox feeding `ViewRootImpl.enqueueInputEvent`. No forced `performClick` fallback was used to make failed navigation pass. The mailbox polls every 25 ms and synthesizes an approximately 120 ms tap hold; these input paths are not identical physical touch-to-photon paths.

Captures use native `screencap -p` and OHOS `snapshot_display`, with command start/end recorded and original images retained. A crop excluding system bars is downsampled only for pixel-difference analysis; a 1.5% mean absolute difference selects candidate changes. Manual inspection validates intended transitions. Sparse capture cadence, serialization and screen encoding limit precision; a continuous frame trace or high-speed camera is still required for native-scale latency claims. No app-specific matched scroll/jank result is claimed: a populated native feed versus an empty OHOS feed is not the same workload. Exploratory native frame counters include setup activity and are not reported as controlled benchmark results.

Final settled child 16640 remained live and its archived log contains **103 successful boot-JIT result records** and **zero matches** for the checked fatal patterns. These are diagnostic records, not a total compiler count or proof of GC/JNI correctness. The same log has Chromium TLS handshake errors; Search visibly reports a network error. Neither observation establishes the cause of the blank feed. No same-board old/new/interpreter A/B or Noice/Material control was performed in this native comparison, so it cannot establish a new-JIT regression.

Artifacts and reproducibility:

- [Benchmark directory](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904): originals, device logs, view trees, input/capture command journal and per-trial JSON.
- [Measurement harness](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/bench.py), [five-pair batch](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/launch_batch.py), [offline validator/statistics](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/summarize.py).
- [Computed summary](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/summary.json), [semantic annotations](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/semantic-validation.json), [artifact SHA-256 inventory](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/artifact-sha256.json).
- [OHOS settled screenshot](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/ohos-settled-06/0000.jpeg), [matching tree](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/ohos-settled-06/tree.txt), [native populated feed](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/oneplus-dismiss-prompt-02/0004.png), [native readable article](/home/dspfac/bridge-build-arm64/toutiao-native-benchmark-20260904/oneplus-article-01/0003.png).

Run `python3 summarize.py` in the benchmark directory to validate the saved 10 launch records and 10 response records and regenerate statistics/inventory without device access. Python syntax compilation and this validator passed. Live reruns require current device/PID identities, correct starting screens, unique output names, and the staged locked runner; do not blindly reuse the recorded PID in `launch_batch.py`.

## Gaps blocking acceptance

1. Restore populated Recommended content on the tested stack and establish a repeatable, same-content article path. Use the same network, separate network/TLS/framework/rendering evidence, and same-board old-JIT/interpreter controls before assigning cause.
2. Resolve the observed on-screen return-control failures and absent keyboard through generic input/window/IME boundary checks. Successful KEYCODE_BACK does not make a failed arrow tap equivalent to native behavior.
3. Measure startup stages and presented frames on a common endpoint, with instrumentation overhead quantified. Preserve first draw, first readable content and fully interactive readiness as separate metrics.
4. Match cache/profile policy, use controlled local and online workloads, then repeat enough tap/scroll/article trials for median/tail latency and frame-deadline comparisons. Agree on a parity tolerance before accepting results.

No runtime fix was implemented during this test. The pinned v142 files and default AppSpawnX parent (PID 4778) were left intact; its original Toutiao child was stopped to avoid competing app surfaces. Candidate parent 9623 and final child 16640 remain isolated and available. The final untimed Back returned the candidate to its home tree. This report does not promote the candidate to the accepted baseline.

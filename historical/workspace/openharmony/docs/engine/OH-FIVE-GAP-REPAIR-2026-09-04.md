# OHOS five-gap repair — 2026-09-04

Follow-up: [seven-gap implementation and verification](OH-SEVEN-GAP-FOLLOWUP-2026-09-04.md).
This page retains the historical candidate05 evidence. Candidate13 is experimental
and is not approved to replace that baseline.

## Outcome

Candidate05 repairs the tested WebView, docked-keyboard, Chinese editing,
input-latency and networking paths. Unchanged Toutiao now renders search results
with text and images and scrolls them. **This is not a claim that all Android
compatibility gaps are closed or that live Toutiao matches native performance.**

| Requested gap | Implemented and verified | Remaining limits |
| --- | --- | --- |
| 1. Blank search WebView | Restored hardware GL functor recording and real Ganesh FBO/stencil handling; fixed capability-publication race; restored EC certificate algorithms and kernel-backed SecureRandom registration. Toutiao search for 中国 renders and scrolls. | One earlier repeat had an EGL extension-initialization failure. Clean-data Toutiao also exposes a separate missing permission-service facade. |
| 2. Keyboard layout | Window-scoped IME InsetsState survives relayout; bottom editor moves above keyboard and restores after hiding. | Docked full-width keyboard only; floating keyboard, rotation and multiwindow are not certified. |
| 3. Chinese input | Code-point-safe deletion, selection replacement, queued stale-session rejection, selection notifications, serialized show/attach, decoder flush/close and learned-phrase persistence. | Android IMM enumeration/session facade still returns NO_IME; native callback generation, grapheme clusters and large-document cursor behavior remain incomplete or unverified. |
| 4. Native-speed comparison | Removed unconditional tap-release pacing; corrected the input measurement protocol; ran identical offline control APK on OHOS and attached OnePlus 6. | OnePlus has no active default network; no valid live Toutiao comparison. Small-sample callback timings are not touch-to-photon measurements or equivalence proof. |
| 5. Networking | Direct-buffer UDP offsets/positions, abstract Unix datagrams, multicast loopback, interruptible asynchronous close, poll/read/write child re-registration. | IPv6, descriptor reuse/GC stress and comprehensive negative/error-path coverage remain unverified. |

## Reproducible candidate and evidence

Workspace: `/home/dspfac/bridge-build-arm64/gap-repair-20260904`.

Device runner:
`/data/local/tmp/asx/gap-repair-20260904/candidate05/run.sh`.
Handoff run: `toutiao05final`, parent 5530 / child 5675; exact process identity,
private data directory and socket are recorded in `toutiao05final-processes.json`.
Use `python3 stop_probe.py toutiao05final` for a guarded stop of this test run.
It requires the existing ability host/window and validates framework, boot image
and runtime hashes before launch. It is not a global replacement of the accepted
runtime. `candidate05/manifest.json` pins the deployed inputs. The hardware helper
is explicitly tied to the accepted HWUI ABI; do not load it against another build.

Build and stage a **new** candidate name, rather than overwriting candidate05:

```sh
cd /home/dspfac/bridge-build-arm64/gap-repair-20260904
CANDIDATE=candidate06 python3 build_candidate.py
CANDIDATE=candidate06 python3 stage_candidate.py
python3 launch_probe.py --candidate candidate06 --run control06
python3 probe_log.py control06
python3 stop_probe.py control06
```

The scripts preserve the untouched DEX entries of the accepted framework and
rebuild a matching boot image. Changed adapter classes include their nested
classes. WindowSessionAdapter is surgically patched because rebuilding its whole
source would import unrelated source/binary drift. Both SessionStageBridge DEX
definitions are patched. The pinned native bridge uses narrow boundary helpers;
the native source also contains the capability-publication fix for future builds.

Important build/evidence files:

- `build_candidate.py`, `stage_candidate.py`, `build_hwui_probe.py`,
  `hwui_functor.cpp`, `GLFunctorDrawable.cpp`, `SkAndroidFunctorUtils.cpp`.
- `launch_probe.py`, `stop_probe.py`: per-run socket/PID identity checks;
  Toutiao experiments mount a separate app-data copy in a private namespace.
- `control/`, `build_control.py`, `measure_control.py`,
  `measure_without_baseline_cpu.py`, `summarize_results.py`.
- `performance-results.json`: all retained measured runs, including failed runs
  and invalid legacy input timestamps; regenerated directly from raw logs.
- `verify_results.py`, `verification-results.json`: verifies all 58 frozen
  candidate artifacts and explicit control-test assertions; records open gaps.

## Verification

### Actual Toutiao

Unchanged APK MD5: `cc99caaaa3baffbb100118753fa1d130` on both devices.

Candidate05 run `toutiao05copy` used a consistent private copy of the accepted
app state. Original `/data/data/com.ss.android.article.news` remained inode
49251. The baseline was briefly suspended while copying its SQLite/WAL files,
then resumed; no writable app-data directory was shared between the processes.

Observed on the actual OHOS screen and corroborated with process/view evidence:

- Feed text and images: `toutiao05copy-after-start.jpeg`.
- Physical Pinyin composition and candidate selection:
  `toutiao05copy-current.jpeg`, `toutiao05copy-chinese-commit.tree`.
- Committed search editor contains 中国; results show text/images:
  `toutiao05copy-results.jpeg`.
- Real touch scroll advances the results:
  `toutiao05copy-scrolled.jpeg`, `toutiao05copy-results-scroll.tree`.
- Full process log: `toutiao05copy-final-child.log`.

Off-main-thread diagnostic WebView getters can return empty strings even while
the browser is working. They are not treated as a loading oracle. Independent
control checks use main-thread browser callbacks, JavaScript and visible pixels.

### Independent controls

`suite05b-final-child.log` verifies:

- Local hardware WebView, JavaScript and Chinese text.
- HTTPS example.com: page-finished 374 ms, JavaScript callback 401 ms in this
  single functional run; not a network benchmark.
- PASS direct-buffer UDP, abstract Unix datagram, multicast loopback, asynchronous
  DatagramSocket.close. The 113 ms close-test duration includes a deliberate
  100 ms pre-close sleep; it is not 113 ms close latency.
- PASS AES-256 encrypt/decrypt and kernel-backed SecureRandom smoke test.
- PASS valid EC chain; reject untrusted root, expired leaf and tampered signature.

No trust anchors were added and SSL errors are never bypassed. A separate
`TrustProbe` still reports a PKIX failure when it revalidates only the leaf
returned by the existing native HttpsURLConnection wrapper. That wrapper lacks
the full peer-chain API; it is an open API-fidelity gap, not evidence that the
successful Chromium HTTPS navigation accepted an invalid certificate.

`ime05-final-child.log` verifies emoji deletion, selected-text replacement and
rejection of a queued edit after focus moved. Docked keyboard height 826 changes
the root from 1920 to 1094 px and bottom-editor edge from 1890 to 1064 px;
hiding restores the original geometry.

### Chinese learning

Installed HAP SHA-256:
`dc90d9bfe785ba9e4d01ab5319bd2887ead56a3499365ba1a1ccc99f2e496a54`.
The prior HAP remains available in `chinese-ime-20260904/out/pinyin.hap`.

- Four strict decoder tests pass (`pinyin/decoder-tests-strict02.log`).
- Seven controller-test groups pass (`pinyin/controller-tests.log`).
- Physical custom selection learns 泥好. The user dictionary grows from 40 to
  66 bytes; after force-stopping/restarting only the Pinyin bundle, the candidate
  remains available (`ime05-learning-disk.log`, `pinyin-reopened-state.log`,
  `ime05-learning-reopened.jpeg`).

The original persistence test was too weak: an unchanged empty dictionary could
pass. It was replaced with a test that requires newly learned data on disk and
candidate preservation after reopen. The stock decoder learns modified composed
phrases, not arbitrary frequency updates to immutable system single characters.

## Performance interpretation

The identical benchmark uses 10,979 characters of offline HTML and fixed 400 CSS
pixel layout width. Each completed run has 12 page samples, with the first two
excluded, and 12 injected taps. Input samples are not warm-up-filtered. The final
control APK SHA-256 is
`e6a36b385d3dc1b8b07ed2c2afbd250fa450aaf18dd4f01fd4baa7b843c6f2ef`.

On this board, `uinput -T -c` reuses the DOWN timestamp on UP. Valid release
measurements therefore use `uinput -T -d ... -i 30 -u ...`, which creates fresh
events. The bridge's additional tap-timeout-plus-20-ms delay is now opt-in only
through `WL_TOUCH_LEGACY_TAP_PACING=1`. Normal dispatch keeps original timestamps
and runs asynchronously on the Android UI thread.

Representative medians:

| Run | Offline visual-ready | Release event to click callback |
| --- | ---: | ---: |
| Candidate05, baseline still running, run a | 153.0 ms | 7.5 ms |
| Candidate05, baseline still running, run b | 149.5 ms | 18.5 ms |
| Candidate05, baseline briefly suspended, run c | 101.5 ms | 10.0 ms |
| Candidate05, baseline briefly suspended, run d | 97.5 ms | 9.0 ms |
| OnePlus, final identical control APK, run 3 | 112.0 ms | 15.0 ms |

`performance-results.json` contains distributions and all repeats. Earlier
OnePlus runs had visual medians 100 and 106 ms. The board's old baseline Toutiao
process was measured at about 90% of one CPU core, with substantial render-service
activity and a thermal-zone reading of 76833. Runs c/d explicitly record temporary
baseline suspension/resumption with a device-side automatic-resume watchdog.
This materially changes the load and must not be hidden in the comparison.

The close offline medians are encouraging but **do not establish native parity**:
device resolution/density and WebView versions differ, background/thermal state
is not fully controlled, one earlier EGL startup failed, and visual-state /
Choreographer callbacks do not measure actual panel presentation. The OnePlus
must be connected to working internet before live Toutiao page-load comparisons.

## Preserved baseline and unresolved work

`final-baseline-integrity.log` verifies the accepted ART MD5
`c8b5e7352662109ddcb9ca3b97020894`, bridge MD5
`b58ed9c01a93df2601e9c0671afdf2df`, global HWUI MD5
`7962c1a77b8f674bf01dfa612e053481`, unchanged Toutiao APK and original data inode.
No Wi-Fi settings, security policy, system CA store or original app data were
rewritten. Baseline runtime files were not replaced.

Still open: full Android IME session/enumeration fidelity; docked-only insets;
EGL startup reliability; clean-install PermissionManager service availability;
the native HTTPS full-certificate-chain API; network concurrency/error stress;
and a controlled live Toutiao performance comparison on the connected OnePlus.

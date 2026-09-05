# Seven-gap follow-up — 2026-09-04

## Outcome

Candidate13 contains the new fixes and passes the independent controls, but it
is **not approved to replace the known-good runtime**. The final unchanged-APK
Toutiao regression exposed an OH IME attach failure. The OH USB target then
disconnected; the OnePlus remains connected but offline. All seven items are
therefore **not complete**, and native performance parity is **not established**.

| Item | Implemented and verified | Closure status |
| --- | --- | --- |
| 1. EGL startup | Optional damage-extension handling; actual KHR/core dispatch; repeated page runs; forced missing-extension run logs `swap mode=full` and completes all 12 pages. | Targeted failure fixed. This is not an exhaustive EGL lifecycle certification. |
| 2. Fresh-data permissions | Lazy PermissionManager facade; actual OH self-token queries; unknown permission denied; real INTERNET grant accepted. Fresh Toutiao reaches its privacy-consent screen without the old missing-service crash. | Targeted failure fixed. No privacy terms accepted; no complete fresh-user journey claimed. |
| 3. Android IME facade | Actual installed/enabled keyboard and subtype enumeration, current Pinyin identity, real IInputMethodSession binding and picker routing. | Control passes; Toutiao attach failure remains open. |
| 4. Native Java HTTPS chain | Full peer-sent DER chain, standard X509Certificate objects/public keys, defensive array copy, independent PKIX validation. | Modern peer-chain API fixed. Deprecated `SSLSession.getPeerCertificateChain()` remains unsupported. |
| 5. IME edge cases | Native callback generations, stale-event rejection, focused-window ownership, full occupied rectangle, Unicode graphemes and large-document selection. | 17 assertions pass, including a real second Android window. Toutiao regression and full physical floating/rotation matrix remain open. |
| 6. Networking stress | IPv6 heap/direct UDP, truncation/zero datagrams, deadlines/errors, GC/concurrency, async close and observed numeric descriptor reuse. | Ten control groups pass; finite stress is not a proof of every possible race. |
| 7. Native-speed comparison | Two matched offline runs per device with identical APK and page content, explicit background-load handling. | Blocked: OnePlus has no active network; no live Toutiao or physical-presentation comparison. |

## Reproducible artifacts

Evidence root: `/home/dspfac/bridge-build-arm64/gap-repair-20260904`.

- `candidate13/manifest.json`: 64 local artifacts verified; device staging also
  completed the hash-gated `GAP_INPUTS_OK` check before tests.
- `seven-verification-results.json`: machine-readable gates, timings, failures
  and explicit `all_seven_complete=false`, `rollout_approved=false`.
- `verify_seven.py`: non-mutating artifact/test-evidence verification; writes
  only its report. A successful verifier means the recorded evidence is
  consistent, **not** that the real-app IME regression passed.
- `build_candidate.py`, `stage_candidate.py`: build and hash-gated staging.
  Candidate IDs are immutable; use a new ID for changes/replays.
- `final_controls.py`, `toutiao_final.py`: recorded final test sequence. Their
  run IDs already exist; choose fresh IDs before replaying device launches.
- `launch_probe.py`, `stop_probe.py`: isolated per-run sockets/private app data,
  guarded process identity checks and evidence preservation before stopping.

ART/JIT compiler artifacts were not changed for these boundary fixes. No new
compiler optimization or JIT CPU improvement is claimed.

## Verification details

### Graphics

`egl13fallback-final-child.log` contains both the explicit extension-removal
test marker and `[OH_EGL] swap mode=full`, followed by all 12 page completions.
`bench13a-measure.log` and `bench13b-measure.log` complete normal hardware runs.
The independent hardware WebView and IME/dialog controls also initialize EGL
successfully. `egl12fallback-final-child.log` is an additional real core-swap
control with the same fixed geometry DSO used by candidate13.

The pinned HWUI has its own local swap wrapper, which originally bypassed
preload symbol interposition. `patch_egl_optional.py` verifies the original
ELF SHA-256 and exactly two instructions: remove only the optional-extension
assertion branch, and route only that wrapper's damage-symbol lookup through
the guarded boundary. The boundary resolves the real driver through a libEGL
handle, avoiding recursion; unsupported damage uses real core swap. Other EGL
errors are not hidden. Corresponding source changes are in `EglManager.cpp`
and `aosp_patches/libs/hwui/hwui_oh_abi_patch.cpp` under `bridge-build`.

Earlier candidate10 forced-extension tests are retained but are not used as
proof of actual fallback: inspection subsequently found the local swap wrapper
was bypassing the first boundary implementation.

### Permissions, HTTPS and networking

`suite13-final-child.log` verifies:

- Permission service present, unknown permission denied, actual OH INTERNET
  permission granted, no rationale for an already-granted permission.
- HTTPS status 200, four standard peer certificates with public keys;
  independent PKIX validation builds a five-certificate path including its
  trust anchor. No roots are fabricated or added to device trust stores.
- Valid EC fixture accepted; untrusted root, expired leaf and tampered signature
  rejected. Kernel-backed SecureRandom and AES-256 roundtrip still pass.
- Ten networking groups pass. Four concurrent workers exchange 128 payloads
  under GC; 32 async-close/socket-churn iterations pass. A separate test observes
  actual numeric fd reuse in **31/32** iterations and verifies the replacement
  socket's payload was not consumed by the old reader. It never dup2-clobbers an
  unknown descriptor.
- Hardware local WebView/JavaScript and online example.com navigation work;
  online completion and JavaScript result are observed on the app UI thread.

The close fixes block the close signal during the pre-poll flag check and
atomically restore the mask in ppoll. An invalidated Java FileDescriptor now
returns EBADF instead of becoming an ignored negative POSIX poll slot that
waits until timeout. JNI registration refuses an uninitialized helper instead
of proceeding with null JNI field IDs. Arrays are not critically pinned across
blocking network operations.

`fresh13-ui.tree` and `fresh13-final-child.log` verify the fresh-data startup
checkpoint. Consent is still presented, and was not accepted. The late file
`fresh13-late-capture-not-startup.jpeg` was captured after this process stopped
and is explicitly excluded as fresh-startup evidence.

### IME controls and real-app failure

`ime13-final-child.log` contains 17 passing assertions:

- Two installed/enabled OH keyboards, current Westlake Pinyin and real subtype.
- Actual Android session binding, not an unconditional `NO_IME` result.
- Deletion of supplementary emoji, combining marks, a family ZWJ sequence,
  skin-tone sequence and regional-indicator flag without splitting the cluster.
- A 40,000-character document, selected replacement at offset 30,000, and
  absolute cursor 30,002. Native full-document selection notifications are
  recorded; the large-text path never labels a truncated window as the document.
- Rejection of both already-queued and late old-generation edits after focus
  moves; independent generation and text delivery in a second Android dialog.
- Portrait, landscape, floating and split-intersection geometry controls.

The device sends the complete rectangle `(0,1094,1200,826)`, not merely a height.
Floating panels retain visibility without generating a full-window bottom
resize. Retired window state is discarded. Geometry-only landscape/floating
tests are **not** physical device rotation/floating-mode certification.

The metadata crash during development came from older plain Property structs
crossing a newer Parcelable ABI. The boundary uses the matching
[OpenHarmony 6 property declarations](https://raw.githubusercontent.com/openharmony/inputmethod_imf/OpenHarmony-6.0-Release/frameworks/native/inputmethod_controller/include/input_method_property.h)
and compile-time size checks with the platform `std::__h` toolchain. The geometry
overlay checks the pinned callback's data/subobject offsets. It must not depend
on the entire bridge through DT_NEEDED: that loaded HWUI/Skia too early in the
failed candidate12 run. The fixed overlay resolves the existing callback only
when invoked; candidate13 contains the successfully tested replacement.

However, `toutiao13-child-current.log` records **three**
`[OH_IME_CAPI] attach/show=12800003` failures for the unchanged Toutiao
SearchActivity. The SDK names this `IME_ERR_IMCLIENT`. SearchAutoCompleteTextView
is focused and an EditableInputConnection was found. The underlying OH service
reason is not yet captured, so focus timing, editor configuration and service
state remain hypotheses, not established causes. No Chinese commit or final
search-results/scroll regression is claimed for candidate13. The black search
capture alone is not treated as proof of a rendering failure because protected
windows can intentionally capture black on this stack.

Android IME APK execution, handwriting, vendor-private commands, and privileged
subtype configuration are not supplied by this OH keyboard facade. Permission
controller mutations likewise remain explicit rejections rather than fake
successful grants.

## Offline performance, not live parity

All four final runs use control APK SHA-256
`44816d9ac706bc94ff2e54780b8a1ad368c5a8c8c71f081232d9dec01b0f7d83`.
The page is 10,979 characters, with fixed 400 CSS-pixel layout width. Each run
has 12 page loads (first two excluded) and 12 timestamped injected taps.

| Run | Page visual-ready median | Release-event to click median | Release-event to frame callback median |
| --- | ---: | ---: | ---: |
| OH bench13a | 99.5 ms | 9.0 ms | 16.0 ms |
| OH bench13b | 99.5 ms | 12.5 ms | 19.0 ms |
| OnePlus13b | 104.0 ms | 14.5 ms | 28.0 ms |
| OnePlus13c | 103.0 ms | 19.0 ms | 31.5 ms |

OH measurements briefly suspend only the identified original Toutiao child
under a 55-second device-side automatic-resume watchdog. Every run verifies
the pause lasted through measurement and that the baseline resumed. Other
background activity, thermal state, WebView versions, resolution and density
are not fully matched. Logs and nearest-rank p90/max values are retained in the
JSON report. Earlier OnePlus12a/13a use the preceding APK hash and are retained,
but are not combined with the final identical-APK comparison.

These are WebView visual-state, click and Choreographer callbacks, not physical
touch-to-photon/panel-presentation measurements. The close medians do not prove
performance equivalence. OnePlus connectivity was checked repeatedly and still
reported `Active default network: none` at handoff.

## Preserved baseline and required next step

The last successful baseline check, `seven-final-baseline-integrity.log`, verifies:

- Accepted ART MD5 `c8b5e7352662109ddcb9ca3b97020894`.
- Bridge MD5 `b58ed9c01a93df2601e9c0671afdf2df`.
- Global HWUI MD5 `7962c1a77b8f674bf01dfa612e053481`.
- Unchanged Toutiao APK MD5 `cc99caaaa3baffbb100118753fa1d130`.
- Original app-data inode 49251 and accepted private-data inode 50270.
- Original child 22641 resumed, and candidate05 child 5675 remained present.

No global runtime, device network setting, security policy, system CA store or
original app data was replaced. The previously installed Chinese keyboard HAP
was not changed in this follow-up. Frozen candidates and failed experiments are
retained; no material evidence or app data was deleted.

At disconnect the isolated candidate13 copied-profile process was parent22259 /
child22425, socket `/dev/unix/socket/AppSpawnXGaptoutiao13`, private data
`/data/app/el2/100/base/org.westlake.imehost/files/android-toutiao-gap-toutiao13`.
Those are **last-observed**, not currently verified-live PIDs. Other diagnostic
children were stopped through the guarded helper. After reconnect, validate the
process record before any cleanup; `stop_probe.py toutiao13` performs those
checks. Do not replace the accepted candidate05 with candidate13 yet.

Reconnect the OH USB device to capture the native IME attach error and finish
the Toutiao regression. Connect the OnePlus to working Wi-Fi for matched live
Toutiao loading/scroll tests. Physical presentation needs a separate validated
measurement, not substitution of a UI callback timestamp.

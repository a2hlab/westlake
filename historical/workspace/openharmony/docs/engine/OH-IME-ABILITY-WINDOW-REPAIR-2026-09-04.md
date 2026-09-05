# OHOS ability-owned IME repair — 2026-09-04

The approved ability/window change is implemented and running on the OHOS board.
Unchanged Toutiao now accepts ordinary screen touches, opens a visible OHOS
keyboard, receives typed text and backspace, and submits Search through its
Android editor. The network control suite still passes. This is **not complete
Android IME compatibility or native-performance parity**: search-result WebViews
remain blank, and Chinese composition and IME resizing are not accepted.

## What changed

1. **Real ownership.** A dedicated, minimally packaged UIAbility,
   `org.westlake.imehost`, owns a genuine main window. Android creates
   `APP_SUB_WINDOW` sessions beneath it. The development launcher reads the
   host's live window ID, BMS UID and access token; it does not borrow another
   app's focus or bypass IMSA's focus checks. The 39,218-byte host contains no
   runtime/system DSOs, requests only `ohos.permission.INTERNET`, and uses
   normal APL with the SDK's public development signing material.
2. **Actual focus propagation.** `SessionStageBridge.onUpdateFocus` now forwards
   the real SceneBoard callback through the existing `WindowCallbackBridge`
   to `ViewRootImpl.windowFocusChanged`. The effective class was unexpectedly
   duplicated in `apache-xml.jar/classes4.dex`, ahead of the adapter JAR. Both
   definitions are patched; all other ZIP entries are byte-preserved.
3. **Keyboard ordering.** The old client `SetPositionZ(9000 + sequence)` placed
   Android's buffer above the keyboard even when the keyboard was visible in
   WMS and accepting touches. Ability-owned windows now leave surface ordering
   to SceneBoard. The source correction is in `oh_window_manager_client.cpp`.
4. **Editor boundary.** The public input-method C-API helper supplies the real
   Android session ID in text configuration. Its backspace callback mapping is
   corrected from after-cursor deletion to before-cursor deletion. Java selects
   only an editor in an actually focused window, snapshots the editor before
   main-thread attachment, and applies preview-region/text updates together.
   The failing legacy focus request and the old 20-second auto-hide path are
   not used. C-API return values are checked; no synthetic successful attach is
   reported.
5. **Screen input.** The hosted input path uses a per-process window consumer
   with target-window checks, not a global input monitor. It is built with
   the real EventHandler headers and OHOS `std::__h` ABI, instead of Rosen's
   portable EventHandler lookalike. Registration returns zero and the handler
   size is 176 bytes on this build. DOWN/MOVE/UP/CANCEL events go through the
   existing Android main-thread input receiver. No app-specific tap remapping
   from the legacy consumer is used.
6. **Data isolation.** Toutiao's existing data were copied into a new directory
   owned by the dedicated host. A root-only development wrapper creates a
   private mount namespace and binds that copy over the Android data path only
   inside the candidate. The unchanged APK still sees its usual path. The
   original directory and its ownership are preserved. This is a test launch
   harness, not a complete production application sandbox or privilege broker.

The earlier crypto, ALPN and POSIX networking changes are retained unchanged;
see the [network repair report](OH-IME-NETWORK-REPAIR-2026-09-04.md).

## Controlled progression

| Candidate | Observation |
|---|---|
| 01 | Real OHOS session exists, but normal Android IMM still sees false focus because the earlier duplicate class was unpatched. |
| 02 | Both focus definitions patched; Android IMM works, but keyboard is covered by Android's forced render Z. Backspace's wrong direction is exposed. |
| 03 | Keyboard visible, typing/backspace/Search work. Ordinary app screen taps still fail because MMI registration is disabled. |
| 04 | Per-window MMI registration succeeds. Control and Toutiao flows work using system screen touches for both app and keyboard. |

The pinned main bridge was deliberately **not** replaced with the unrelated
current aggregate build. Candidate 04 uses two narrow opt-in boundary DSOs:
one suppresses only the legacy high-Z call from the pinned main bridge for
hosted windows; the other replaces only its hosted `subscribeMmi` entry.
Corresponding source changes and full-build include routing are present. The
complete aggregate main bridge rebuild has not been validated in this task.

## Verification and evidence

All evidence below is under
`/home/dspfac/bridge-build-arm64/ime-ability-window-20260904`.

| Check | Result / evidence |
|---|---|
| Real Android IMM show | Screen tap at `(600,180)` yields window 37 DOWN/UP, `IMM_SHOW=true WINDOW_FOCUS=true`, C-API attach/show 0, keyboard status 2; `control04-tap.log`. |
| Actual editor changes | Screen-key sequence `wle`, Backspace produces `wl` in `control04-edited.tree`; Search logs `EDITOR_ACTION=3 TEXT=wl` in `control04-actions.log`. |
| No touch duplication | Keyboard taps do not appear as Android-window pointer events in that control log. No global monitor registration is made. |
| Visibility and lifecycle | `control04-keyboard.jpeg`; control hide/reopen succeeds. Toutiao hide/reopen succeeds twice and keyboard remains visible beyond 20 seconds; `toutiao01-final-ime.log`, `toutiao01-keyboard-stable.jpeg`. |
| Network controls | `control04-initial.log`: SHA-256 known answer; 118 system roots; valid PKIX accepted; untrusted and expired chains rejected; IPv4/IPv6 UDP with receive timeouts; DNS; certificate-verified HTTPS 200. |
| Unchanged Toutiao | APK MD5 `cc99caaaa3baffbb100118753fa1d130`. Screen tap opens Search and the native keyboard. Typing `openaie`, Backspace gives `openai`; suggestions populate. `toutiao01-typed.tree`, `toutiao01-typed.jpeg`. |
| Search submission | The on-screen Search key hides/detaches the keyboard and opens the results UI with query `openai`; `toutiao01-ime-submit.log`, `toutiao01-results.tree`. This is submission evidence, **not** a successful results-page load. |
| Original data preserved | `toutiao01-data-isolation.log`: original inode 49251, root/root 777; private copy inode 50270, UID/GID 20010054, mode 700. Child sees copy inode 50270. Original namespace 4026531841 differs from candidate namespace 4026533009. |
| Offline regressions | `python3 -m unittest -v test_candidate`: 12/12 pass. Includes artifact pins, unchanged JAR entries, duplicate focus definitions, host permissions, request packing, saved runtime evidence, malformed requests and fragmented socket replies. These are not 12 independent runtime repetitions. |
| Rebuild/syntax checks | New native boundary helpers build with warnings-as-errors; rebuilt input helper is byte-identical to the staged one. Python scripts compile; shell build scripts pass syntax checks. |

App and keyboard interaction used `uinput -T -c` through the OHOS input system.
The PID-qualified mailbox was used only to obtain diagnostic view trees in the
final candidate, not to type or click the acceptance flow. The tree helper also
fixes mailbox ownership so a root-created diagnostic command is not replayed
continuously by the normal-UID child.

Network fixtures and the independent native host TextInput were controls.
Earlier Noice and Material Catalog results remain historical controls from the
network report; they were not rerun against candidate 04.

## Remaining gaps

- **Search-result WebView:** the results body is blank. The captured
  `DedicatedSearchWebView` instances report empty URL/title, content height 1,
  and deferred initialization, despite native feed and suggestions rendering.
  This observation does not establish the ultimate WebView root cause. Do not
  count the results page as loaded or assign its delay to JIT/networking alone.
- **Chinese composition and selection:** follow-up [Pinyin acceptance](OH-CHINESE-IME-2026-09-04.md)
  now passes live native OHOS and unchanged Toutiao preview, candidate commits,
  phrase append, backspace, cancel and Chinese search submission. A separate
  offline Pinyin keyboard was installed; Kika remains available. Cursor/selection
  synchronization, grapheme/surrogate
  deletion and cross-editor races still need focused tests. Off-main-thread
  `show()` currently queues the editor snapshot before calling native attach
  without waiting; this is a remaining ordering risk.
- **Insets/resize:** occupied-area/avoid-area callbacks remain placeholders;
  bottom editors and `adjustResize` behavior are not fixed or accepted here.
  Visible keyboard overlay is not proof of correct Android IME insets.
- **General input:** this consumer currently implements single-pointer touch
  and a small navigation-key subset. Multitouch, wheel/axis input and full
  hardware-key mapping are not accepted. Multi-window target-to-ViewRoot
  selection and lifecycle cleanup need wider controls.
- **Android IME service surface:** enumeration, binding/session APIs and other
  `InputMethodManagerAdapter` stubs remain incomplete even though the tested
  Android show/editor flow works through the OHOS boundary.
- **Networking coverage:** the earlier untested direct-buffer, multicast,
  Unix-domain and asynchronous-close cases remain unaccepted.
- **Performance:** this turn is a correctness experiment, with diagnostic
  logging and an isolated copied data set. No new OnePlus 6 comparison or
  native-performance-parity claim is made. Prior timings are in the
  [native benchmark report](ART-JIT-NATIVE-BENCHMARK-2026-09-04.md).

## Artifacts, source and handoff

Runtime manifest: [candidate04/manifest.json](/home/dspfac/bridge-build-arm64/ime-ability-window-20260904/candidate04/manifest.json).

| Artifact | SHA-256 |
|---|---|
| Host HAP | `39913a6b29d239e133399dac0dba36097c5f5c869ea4d4f3c8a4f108dafd5e0a` |
| IME C-API helper | `2f7dbd6d1ef318188ddd2def85a66330eee9278b11406a73db174b8fec159b7d` |
| Owned-window input helper | `99fa2f95182991a0b0ca4d1de9c1306bfedd888c777f76f0e02af2e93587404e` |
| Owned-window Z helper | `fdbbfb5c6faff500fd4223448e53c8739b526fb46f5174e38220dd5bd59763e4` |

Pinned ART MD5 remains `c8b5e7352662109ddcb9ca3b97020894`; pinned main bridge
remains `b58ed9c01a93df2601e9c0671afdf2df`. The accepted v142 artifact is separately
rechecked as `20e48b90b6b01468f155ed7c48097e70` and is not overwritten/promoted.
All staged candidate inputs pass the device runner's SHA-256 checks.

Source entry points:

- [SessionStageBridge.java](/home/dspfac/bridge-build/src/framework/window/java/SessionStageBridge.java), [OhImeBridge.java](/home/dspfac/bridge-build/src/framework/window/java/OhImeBridge.java), [IME C-API helper](/home/dspfac/bridge-build/src/framework/window/jni/oh_ime_helper_capi.cpp).
- [Owned input bridge](/home/dspfac/bridge-build/src/framework/window/jni/oh_owned_input_bridge.cpp), [input integration](/home/dspfac/bridge-build/src/framework/window/jni/oh_input_bridge.cpp), [window ordering](/home/dspfac/bridge-build/src/framework/window/jni/oh_window_manager_client.cpp).
- [Host and reproduction instructions](/home/dspfac/bridge-build-arm64/ime-ability-window-20260904/README.md), [launcher](/home/dspfac/bridge-build-arm64/ime-ability-window-20260904/launch_control.py), [private data wrapper](/home/dspfac/bridge-build-arm64/ime-ability-window-20260904/private_data_launch.c), [regression tests](/home/dspfac/bridge-build-arm64/ime-ability-window-20260904/test_candidate.py).

At handoff the dedicated host is PID 8653 / main window 34, candidate parent
22507 and Toutiao child 22641 on `AppSpawnXImeHost04`. The default parent 4778,
review parent 9623 and network-repair parent 2744 remain alive. Their old
overlapping Toutiao child was stopped after evidence capture; no original APK,
app-data directory, baseline runtime, system service, global mount or SELinux
mode was changed. Stop only the recorded candidate processes to retire this
experiment; process teardown removes its private mount namespace.

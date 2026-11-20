# ADR-0010: Measure completeness by function, and close the runtime gaps in order

- **Status:** proposed 2026-11-20
- **Date:** 2026-11-20
- **Depends on:**
  - [ADR-0002](0002-app-secondary-processes.md) for secondary processes
  - [ADR-0003](0003-authenticated-app-identity-and-isolation.md) for app identity and isolation
  - [ADR-0006](0006-play-installation-through-package-sessions.md) for installs
  - [ADR-0007](0007-play-background-lifecycle-and-validation.md) for background work
  - [ADR-0008](0008-one-oh-process-model-for-apks-and-haps.md) for the process model
  - [ADR-0009](0009-android-system-services-on-oh-services.md) for system services
- **Scope:** the westlake runtime and the westlake-harness test system. Policy and licensing walls are out of
  scope: Play Integrity verdicts, DRM (Widevine), anti-tamper protectors and device certification.

## Context

Westlake runs unmodified APKs as OH apps. There is no second Android system underneath: each app is a real
OH window and ability, and uses OH's keyboard, notifications, location and permission switches.

The corpus rounds count apps that draw a first screen. Most of the HK top-1000 corpus does. The two-path
validation then drove about fifty apps through real flows:
- Gmail opening a message;
- YouTube playback;
- Maps search;
- Google sign-in in dozens of apps;
- account creation in Canva, Trip.com and WEBTOON;
- Facebook's web login in the in-app browser;
- unmodified Play services and Play Store running as ordinary processes.

That work showed two things the first-screen count cannot.

**A drawn first screen hides broken features.** In batch 2 (eight apps, 2026-11-20 and 21) the first screens
were fine, and still:
- Back left a black screen;
- new windows attached late or needed a second tap;
- the Play Store went on drawing at about one CPU while hidden;
- real touch reached no app until a touch forwarder was started by hand.

**Cross-component bugs look like flaky crashes.** Chromium aborted with "Bad file descriptor", and hwui's
render thread with `Looper POLL_ERROR`. Both looked random. The causes were two Westlake bugs that closed
another component's file descriptor:
- the window shim left PixelCopy's fence unwritten, and hwui closed whatever value its stack held;
- the runtime deployed for the stack lacked the input receiver's own copy of the channel descriptor.

The first appeared once in an 11-14 corpus round and was misread as a one-off. Neither was found until a
kernel trace named the closer.

**The second device shows the same pattern** (OnePlus 6 with OpenHarmony 7.0 and SceneBoard, HK top-1000 round
o70b against the D600's round hka, 725 apps scored on both; see the PR evidence below):
- 609 apps reach "drawing" on the phone and 574 on the D600, but apps drawing *cleanly* (no blocker or anomaly
  after the first frame) are 530 against 529.
- The phone's lead is mostly apps that draw a frame and then die: 64 against 30.
- Of the 58 apps clean only on the phone, 41 drew cleanly on the D600 in a later round, so most apparent gains
  came from framework changes rather than the device.
- A package-manager stall was a lock-order deadlock between three threads: a class lock held across a
  permission check, `System.load` and `System.loadLibrary`. Only a thread dump named it.
- A second GPU stack (Mesa freedreno, no Vulkan) shows gaps the D600's Mali stack hides.

Containers (Waydroid and similar) reach near-full parity by running a whole Android system. That costs
hundreds of MB to about 1 GB per device and a second world of windows, notifications and permissions. Westlake
does not take that route, so its completeness has to be built and measured service by service.

## Decision

### 1. Measure function, not first screens (westlake-harness)

| # | Add | Why |
|---|---|---|
| H1 | Scripted core flows for a sample of about 100 corpus apps: sign-in, scroll, search, media playback, receive a notification, background job, share, deep link. Results are judged from view-tree assertions and log signatures, with screenshots only as records. | Functional parity becomes a number per flow. |
| H2 | The same flows, on the same app versions, on stock Android (the Android 15 reference phone). | Separates Westlake gaps from app, server and region behaviour. |
| H3 | Lifecycle steps in every run: Back from each screen, home and relaunch, rotation, process kill and restore, ten minutes in the background. | Black screens after Back and late windows are found by default. |
| H4 | Crash forensics by default: <ul><li>Chromium's FATAL line from the app log;</li><li>native frames symbolized against the crash dump's maps;</li><li>the Play services call trace (`WESTLAKE_PLAY_TRACE`: service ids and callback status);</li><li>an automatic descriptor-ownership trace on a rerun after any "Bad file descriptor" or `POLL_ERROR` abort;</li><li>a thread dump of every launch that stalls, which finds lock-order deadlocks;</li><li>repeat runs that put a number on flakiness.</li></ul> | Root causes arrive with the failure instead of after hours of manual tracing. |
| H5 | Failures attributed as Westlake gap, policy, region or human step: <ul><li>uncertified-device refusals by service id;</li><li>region locks;</li><li>CAPTCHAs.</li></ul> Host-screen references per device: SceneBoard's starting window is plain white, the D600's host screen is black. | The gap map lists only what Westlake can fix. Without per-device references, plain white or black app screens are misread on one device or the other. |
| H6 | Resource metrics: memory per app, CPU while hidden, wakeups, cold-start time, frame pacing. | Regressions in cost are seen, not stumbled on. |
| H7 | A bounded crawler driven by the view tree, counting reachable screens and crash-free depth against H2's baseline. | Covers features beyond the scripted flows across the whole corpus. |
| H8 | Fixtures: <ul><li>managed test accounts;</li><li>automatic renewal of per-app Google grants;</li><li>a Hong Kong network profile;</li><li>a human-step state that routes real touch to the app automatically.</li></ul> | Sign-in and region-dependent flows become repeatable. |
| H9 | A regression gate per build: a pinned subset of flows must pass before a build is deployed, with bisection when it fails. The gate runs on both GPU stacks: Mali with Vulkan, and Mesa without. | A missing fix, like the absent input-descriptor fix, is caught at deploy. Mesa-only failures stay invisible on the D600. |

### 2. Close the runtime gaps in this order (westlake)

| # | Area | Work | Evidence |
|---|---|---|---|
| W1 | Cross-component safety net | <ul><li>Make fdsan real in the Bionic shim: owner tags, with a report naming the caller of any foreign close.</li><li>Rule: every perform, query or ioctl answer writes its output parameters, even on error, and stubs are scanned for violations.</li><li>No framework lock is held across a call that can load a library or re-enter the package manager.</li></ul> | The two descriptor bugs above; the package-manager deadlock on the phone. |
| W2 | Activities, tasks and windows | <ul><li>Android tasks and back stack mapped onto OH abilities, so finishing an activity brings the previous window to the front.</li><li>A frame requested whenever a window becomes visible.</li><li>Results passed between apps.</li><li>Task switching.</li><li>On the managed launch: the manifest's activity theme is applied before `ActivityInfo` is copied, the OH ability hands over between activities, and an app that finishes its only activity at start-up keeps its process, as on Android.</li></ul> | Back black screen in two apps; late windows in three of eight batch-2 apps. On the phone: about 20 `InflateException` failures from the theme order, and hand-off failures in two apps (fixes on `managed-session-handoff`); 4 apps that finish their only activity lose the process. |
| W3 | Process model | ADR-0002 completed, on the managed launch too: <ul><li>components in their declared processes;</li><li>isolated processes (browser renderers);</li><li>providers and receivers across processes;</li><li>death notification and restart.</li></ul> | Play services' processes are folded; WebView renders in-process. On the phone, Firefox (a crash-helper service in its own process) and Didi (waits for its privacy process) fail without secondary processes. |
| W4 | Graphics | <ul><li>Hidden and covered windows stop drawing.</li><li>PixelCopy of a window returns a picture, from OH's last flushed buffer.</li><li>Surface ordering.</li><li>Second GPU stack (Mesa freedreno, no Vulkan): `eglPresentationTimeANDROID` for Unity's frame pacing; Mesa's `discard_framebuffer` bug (its own driconf workaround is `ignore_discard_framebuffer`); YUV (NV12/NV21) native buffers for TextureView video.</li></ul> | The hidden Play Store uses about one CPU. PixelCopy now fails cleanly but returns nothing. On the phone: 4 Unity apps fault in frame pacing (1 more probable), 4 hit the Mesa bug, and 5 abort on TextureView video. |
| W5 | Input | <ul><li>Real touch reaches every app window without a manual forwarder.</li><li>An accessibility bridge.</li><li>Autofill mapped to OH.</li></ul> | A user could not tap a CAPTCHA until the forwarder was started. |
| W6 | Background execution | ADR-0007: <ul><li>jobs and alarms that start a stopped app;</li><li>push delivered to an app that isn't running;</li><li>boot and package broadcasts;</li><li>foreground services; doze.</li></ul> | Not exercised yet. H1 adds the flows. |
| W7 | Identity and isolation | ADR-0003 and ADR-0004: <ul><li>one OH identity per Android app;</li><li>verified identity enforced by default;</li><li>Android user 0 semantics;</li><li>permission answers by protection level.</li></ul> | Checks answer "granted" where a rule says denied. |
| W8 | Packages and intents | ADR-0006: <ul><li>installs, split installs and updates through the Play Store.</li></ul> Also: <ul><li>share and deep links between Westlake apps and OH apps;</li><li>`content://` URIs across apps.</li></ul> | Install sessions work only in part; sharing between apps is not built. |
| W9 | Google services within policy | <ul><li>The chosen account stays visible to the app after the picker closes.</li><li>Grants accept every client id an app uses.</li><li>Tokens renew silently.</li><li>Downloadable modules load in client apps.</li><li>The Play Store answers integrity requests, whatever the verdict.</li></ul> | Play services sees no accounts for an app outside its picker. A grant holds one client id. Strava's two integrity requests were never answered. |
| W10 | Media and hardware (non-DRM) | <ul><li>Hardware decode through OH's codec service.</li><li>Media sessions through OH's AVSession.</li><li>Camera; Bluetooth; NFC; telephony state.</li></ul> | Camera and Bluetooth are not bridged on the reference board. `media.camera` was null for the 124 apps in the phone round that asked for it, and Camera1 reports no cameras. |

Where an Android system component can run unmodified as a Westlake-hosted service, as Play services and the
Play Store already do, prefer that to reimplementing it. Candidates: the download, media and contacts
providers. OH is mapped only at the edges (display, input, audio, camera). This buys fidelity without a
container, at some memory cost, which H6 measures.

### 3. Order of work

1. W1 and H4: cheap, and they turn silent cross-component bugs into named reports.
2. W2 and H3: the most visible failures, and their regression check.
3. H1 and H2: function measured against stock Android.
4. W3, W6 and W5.
5. W7, W8, W9 and W10.

### 4. Owners

Work is split by device (proposed 2026-11-20 by the OnePlus session at the user's request, accepted by the D600
session; for the user's review here).

| Device | Items | Why there |
|---|---|---|
| OnePlus 6 (OpenHarmony 7.0, SceneBoard, managed launch, Adreno on Mesa, real battery, GNSS, camera, SIM, Bluetooth, NFC) | <ul><li>W10 media and hardware;</li><li>W4 on Mesa;</li><li>W2 and W5 on the managed launch;</li><li>W3 on the managed launch;</li><li>the power side of W6;</li><li>H6;</li><li>phone corpus rounds;</li><li>the non-Mali leg of H9.</li></ul> | Phone hardware and the target launch model are there. |
| D600 (OpenHarmony 6.1, Mali with Vulkan, two-path Play setup and test accounts) | <ul><li>W9 and W8;</li><li>W1;</li><li>W7;</li><li>W3 in general (isolated and secondary processes, providers and receivers, Play services processes);</li><li>W6 push delivery;</li><li>W4 on Mali (hidden windows, PixelCopy content);</li><li>H1 to H5 and H7 to H9.</li></ul> | Google services, the Play setup and the descriptor work are there. |

Shared code needs one design. W3 gets one secondary-process mechanism: the managed-launch hook on the phone, the
spawner and Play services side on the D600, behind an interface agreed first. Changes to shared files
(`framework/webview-shim/webview_bionic_shim.c`, `framework/package-manager/`) stay region-local and are announced
between the sessions before merging. Each item has its own branch and needs the user's approval to merge.

## Acceptance criteria

- **H1 and H2:** a published per-flow parity table for the 100-app sample, Westlake against stock Android, with
  every failure attributed under H5.
- **H3:** every run reports Back, relaunch, rotation, kill-restore and background-resume results per app.
- **H4:** a deliberately injected foreign close is reported with the closer's library and offset, without manual
  tracing.
- **W1:** the same injected close is reported by the shim's fdsan in a normal run.
- **W2:** Back returns to the previous screen in every app of the sample whose Android baseline does.
- **W4:** a hidden app's CPU use is under a stated threshold measured by H6. PixelCopy of a visible window
  returns its content.
- **W5:** a CAPTCHA or other human step can be completed by touch on the board in any launch.
- **Status:** each item is recorded as implemented, exercised or passed, not inferred from API success.

## Consequences and alternatives

The harness grows from counting screens to running flows. That needs test accounts, grants that renew, a
reference Android device and longer runs. Results become comparable across builds and with Android. Runtime
work follows measured failures instead of anecdotes.

Alternatives considered:
- **Keep first-screen rounds as the main measure.** Rejected: the descriptor bugs and the Back black screen were
  invisible to it or looked like noise.
- **Run a full Android container.** Rejected for the reasons in the context; containers meet the same integrity
  and DRM walls.
- **Reimplement every Android system service on OH.** A long tail with no natural end; the hosted-component
  path above covers part of it more cheaply.

## Evidence

- The descriptor bugs:
  - the window shim fix [f46ae1b](https://github.com/a2hlab/westlake/commit/f46ae1b9cfb8dc3166761fdc1d59e78216d3594f);
  - the input receiver fix [876034c](https://github.com/a2hlab/westlake/commit/876034c1b375cb9d07f9f406d3fee754c96f4356),
    missing from the deployed two-path runtime until it was rebuilt with it.
- Batch-2 results, including the Back black screen, late windows, Facebook and Google outcomes, the uncertified
  refusal of service 219 and Strava's unanswered integrity requests:
  [claude-20261120-batch2-results.json at e05c71d](https://github.com/a2hlab/westlake/blob/e05c71d0de2f4ae7669419486816d58639a738f1/tests/google-auth/full48/claude-20261120-batch2-results.json).
- Earlier two-path results and the scope changes:
  [7b10fc6](https://github.com/a2hlab/westlake/commit/7b10fc6b6dc6939458df87ea2531420147429621),
  [79657ff](https://github.com/a2hlab/westlake/commit/79657ffbec644ad8bb7ad1b54d5d2d945122197a),
  [326a747](https://github.com/a2hlab/westlake/commit/326a747b2fbbcd1cbe921ac990bb3c56f93ee876).
- OnePlus 6 evidence (OpenHarmony 7.0, round o70b against the D600's hka):
  [PR #177 comment](https://github.com/a2hlab/westlake/pull/177#issuecomment-7053253281).
- SurfaceView ordering fix and remaining surface work:
  [6b80c26](https://github.com/a2hlab/westlake/commit/6b80c2667758b180e78bd7c7027b4d460260c57c).

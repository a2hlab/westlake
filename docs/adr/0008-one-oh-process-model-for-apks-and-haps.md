# ADR-0008: Manage APKs and HAPs under one OH process and security model

- **Status:** accepted 2026-11-13 as the long-term direction (proposed the same day). The feasibility
  spike met the first milestone on the test board on 2026-11-15; see [Spike result](#spike-result-on-the-test-board-2026-11-15).
  Direct launch stays the default.
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md) for identity and confinement,
  [ADR-0004](0004-scoped-play-compatibility-and-permissions.md) for Android permissions
- **Extends:** [ADR-0001](0001-shared-play-services-process.md), [ADR-0002](0002-app-secondary-processes.md)
  and [ADR-0007](0007-play-background-lifecycle-and-validation.md)
- **Implementation baseline:** experimental `play-processes` at `d1dcb27760fc59ccc6f3af8ed8e24302e795b497`

## Context

Westlake launches each APK through appspawn-x, outside OH's application manager. The APK is not
registered with BMS, and every Westlake process runs under one OH uid. Android windows are OH
sub-windows, parented to the host application's main window. OH therefore sees a single application.
It cannot attribute processes, windows or resources to an APK. Focus, input, IME, stacking and cleanup
are all Westlake workarounds.

The HK rounds use this path. Their process logs show `[DIRECT-LAUNCH]` and "skipping real AMS adapter",
and contain no `AttachApplication`. The code at the baseline shows four gaps:

- **Process ownership.** `activity_task_manager_adapter.cpp:1329–1365` keeps the `ASX_DIRECT_LAUNCH`
  bypass. Its note records why OH's own path was reverted. `StartAbility` succeeded for the host
  bundle's EntryAbility, but AMS spawned its own process for that ability. The session and token
  belonged to that pid, so the window call failed with ERR_INVALID_STATE. The note says to re-enable
  the path only together with a process-identity fix.
- **Lifecycle.** `oh_app_mgr_client.cpp:73–99` already creates an `AppSchedulerAdapter` and calls
  `AttachApplication`. `app_scheduler_adapter.cpp` bridges the foreground, background and termination
  callbacks into Android. The rounds' launch path does not use either.
- **Session tokens.** `ability_scheduler_adapter.cpp:864–868` receives `UpdateSessionToken(sessionToken)`
  and drops the token. `AbilitySchedulerBridge.java:515–516` only logs the call.
- **Borrowed windows.** `oh_window_manager_client.cpp:155–176` and `1663–1709` parent Android windows
  under the ArkTS EntryAbility's window. They do not take focus, because taking it killed the parent
  ability with a lifecycle timeout. The session identity comes from `WL_SESSION_BUNDLE`, with a
  hard-coded fallback. A September measurement on the board found that windows stack in creation
  order, and that input arrives through Westlake's tap channel rather than through WMS focus.

## Decision

1. **One management model, two runtimes.** APKs and HAPs share OH's application management:
   - verified package identity, with the runtime type, in BMS;
   - AppMgr process records and lifecycle authority;
   - AbilityManager lifecycle;
   - SceneSessionManager and WindowManager session ownership;
   - resource and background policy.

   Ark and ART stay separate runtime backends with separate spawners: OH's appspawn for Ark and
   appspawn-x for ART. Both spawners implement the same trusted process-management contract.
2. **Each application keeps its own processes and identity.** A shared process model never puts
   unrelated applications in one process or under one kernel identity. Each APK gets its own kernel
   uid, access token, private storage and SELinux confinement, assigned through OH's installation
   records. This is how ADR-0003 decision 4 is delivered. AppMgr also records an APK's secondary and
   isolated processes; once implemented, this amends ADR-0002, whose processes Westlake spawns itself.
3. **AppMgr creates and owns the ART process.** The launch runs:
   `StartAbility` → AppMgr process record → appspawn-x child → `AttachApplication` → OH application and
   ability callbacks → Android `ActivityThread` → completion acknowledgements.
   The process that receives the ability owns its window session. Confinement is applied before any
   application code runs, and the launch fails closed if it cannot be (ADR-0003 decision 6).
   appspawn-x accepts spawn requests only from AppMgr.
4. **Each APK owns its OH main window session.** The native and Java adapters keep and pass on the
   ability and session tokens. Dialogs, popups and rendering surfaces are parented under the
   application's own session. OH controls focus, input routing, IME, stacking, visibility, display
   changes and cleanup. Rendering continues through Westlake's surface adapters.
5. **Translate Android semantics explicitly.** HAPs use Ark and OH APIs; APKs use ART and Android APIs
   through Westlake's adapters. Android permissions map to explicit grants and are enforced
   (ADR-0004). Activities, tasks, the back stack and services get defined compatibility semantics,
   because Activities and UIAbilities have no universal one-to-one mapping. OH sees accurate
   visible-task and process ownership. [ADR-0009](0009-android-system-services-on-oh-services.md)
   applies the same rule to system services.
6. **Comparable boundaries are not equal assurance.** The model aims for isolation boundaries comparable
   to a HAP's. That alone does not give equal security assurance: ART and the adapters must preserve
   authenticated caller identity and enforce permissions and token ownership. Confinement rules may
   differ where the runtimes need different things. Identical policy text or syscall lists are not
   required.

## Current implementation

The table below comes from source inspection at the baseline commit. The "Today" entries for kernel
identity and launch path were also observed on the test board. Every entry in the "Target" column is
proposed: none is implemented or device-verified.

| Contract | Target | Today |
|---|---|---|
| Package identity | APK registered with BMS: verified signer, runtime type | Not registered; Westlake's package manager answers from staged APKs |
| Process records | AppMgr creates and tracks every process | appspawn-x forks outside AppMgr, ADR-0002's secondary processes included |
| Kernel identity | Per-application uid and token from OH | One OH uid for every Westlake process; the launcher's token (ADR-0003) |
| Lifecycle | AppMgr and AbilityManager callbacks | `AppSchedulerAdapter` exists but is unused; the rounds launch directly |
| Window session | The APK's own main session | Sub-windows under the host ability's window; `UpdateSessionToken` dropped |
| Focus, input, IME | OH | Westlake's tap channel; WMS focus avoided |
| Cleanup | OH removes an application's windows and processes | Westlake and the harness end processes |
| Background and resources | OH policy | Westlake adapters (ADR-0007) |

## Feasibility and host changes

The spike must answer these questions before any work is scheduled:

1. **The native bridge build (answered 2026-11-13).** Session-token propagation and window parenting
   live in the native ability and window bridge. In September, 11 of its 50 sources failed for lack of
   OH headers. Two inputs fix that:
   - jsoncpp 1.9.6, unpatched, from OpenHarmony's `third_party_jsoncpp` at OpenHarmony-v6.1-Release;
   - stand-ins for seven headers that OpenHarmony generates from public IDL.

   With them, all 50 sources compile, and the linked `liboh_adapter_bridge.so` is byte-identical to the
   deployed library. The bridge can be changed again.
2. **Runtime selection in AppMgr (answered 2026-11-13).** OpenHarmony-v6.1-Release's AppMgr already
   picks a spawner from installed bundle metadata (`AppMgrServiceInner` at process start):
   - Cangjie bundles go to cjappspawn;
   - ArkTS 1.2 and hybrid bundles go to hybridspawn;
   - every other bundle goes to appspawn.

   `AppSpawnClient` accepts only those service names, plus nwebspawn and nativespawn, and the test
   board's init provides all their sockets. ART is a fourth case: one runtime check and one spawner
   name in AppMgr, plus an init service and socket for appspawn-x. It is a host change, but a contained
   one. The spike still evaluates the alternative in the baseline's note: run ART inside the process AMS
   starts for the ability, or adopt that process's ability token.
3. **Declared processes (answered 2026-11-13).** AppMgr names processes per ability, per module and
   per application, and it also tracks child processes. BMS honors those names only for a bundle that
   holds the `AllowAppMultiProcess` privilege. The privilege comes from the bundle's signing profile or
   the device's pre-install capability list. Without it, BMS sets every process name to the bundle name
   (`BundleInstallChecker::ProcessBundleInfoByPrivilegeCapability`, OpenHarmony-v6.1-Release).
   - Each APK bundle therefore needs that privilege to keep the processes it declares; Play services
     declares eight.
   - Android services and providers that are not abilities map to named extension abilities or to
     AppMgr child processes.
   - On the test board the privilege is configurable. For a product it is a signing decision.
4. **Per-APK bundles.** Each APK needs a signed OH bundle. Debug signing is enough on the board; product
   signing is a separate decision. The earlier per-APK install path
   (`framework/package-manager/jni/apk_installer.cpp`, with its template entry HAP) is a starting point.

OH itself may be changed on the test board for this work (decided 2026-11-13). That covers AppMgr's
runtime selection, an init service for appspawn-x, the multi-process privilege and SELinux policy.
Every change keeps the original files so that they can be restored, and none is made while a test round
runs. A product still needs the platform owner's agreement.

## Spike result on the test board (2026-11-15)

The first milestone was met, then extended to two unrelated APKs, without changing OH. The procedure is
the manifest's `docs/managed-launch.md`.

- **Route.**
  - Each APK gets its own OH bundle, made from the Westlake host HAP with a new `module.json`
    (manifest `tools/make_managed_bundle.py`). The bundle name is the Android package, the entry
    `srcEntry` is a dotted class name, and the ability is named after the launcher activity.
  - OH 6.1's AppMgr sends such a bundle to the Cangjie spawner's socket (`IsCjApplication`), so
    AppMgr needed no change.
  - `appspawn-x --route` serves that socket. It passes each request to the bundle's own appspawn-x,
    which it finds in a root-only registry.
  - The spawner admits AppMgr's request (foundation's uid and SELinux context) only for its own bundle.
    Its socket carries appspawn's SELinux context, because foundation may not connect to the shell's.
- **Attach and bind.** OH's `ScheduleLaunchApplication` arrives on an IPC thread. The bind runs on the
  main thread, and the first ability waits for it. The bind prepares the staged APK as a direct launch
  does.
- **Window.**
  - The activity's window connects to its ability's own session (`ISession::Connect`) as the main
    window.
  - The app's other main-type windows, such as dialogs, become sub-windows of the main window.
  - A SurfaceView's window stays below its activity's and takes no focus.
  - A closed sub-window is destroyed through SceneSessionManager, so focus moves on.
  - OH's avoid areas become Android's status-bar and navigation-bar insets.
  - SceneBoard removes its starting window at the app's first frame, because the client reports its
    frame layout when it connects.
- **Task.** One OH ability hosts the Android task (decision 5).
  - The app's own activities start in its process.
  - Finishing the activity that holds the OH token hands the token to the top of the task.
  - Finishing the last activity ends the ability.
- **Input.** OH delivers taps and keys to the managed window. BACK behaves as on Android 12 and later:
  - a shown keyboard closes first;
  - then the activity handles BACK;
  - BACK at the task root moves the app to the background (`RequestSessionBack`).
- **Lifecycle.** OH moves the app to the background and back. In the background, ART's homogeneous
  space compaction, a moving collection, crashed apps on objects in the protected old space. A
  managed launch therefore uses a non-moving background collector (`APPSPAWNX_BACKGROUND_GC=CMS`).

Observed on the test board, in builds up to 274, with Telegram X and The Guardian running at the same
time, each under its own uid:
- `aa dump` and the window manager listed each app's ability and main window.
- Neither uid could read the other's app data (EACCES) or signal its process (EPERM).
- `aa force-stop` removed an app's process, its windows and its ability record.
- These worked:
  - splash to home;
  - dialogs, centred and dismissed by taps;
  - BACK;
  - the system bars;
  - repeated moves to the background and back.

**Against the acceptance criteria.**
- The first milestone passed, except that BMS does not record the APK's signer or runtime type. The
  bundle carries the package name and its own uid and token.
- Of the second criterion, these were exercised with two unrelated APKs:
  - attribution and kernel credentials;
  - window ownership and focus;
  - BACK, and showing and hiding the keyboard;
  - lifecycle and cleanup;
  - private-file isolation.
- These were not exercised: rotation, a HAP alongside, secondary and isolated processes under AppMgr,
  memory-pressure termination, and the corpus comparison.

**Limits.**
- The Cangjie route only works on the test board: while the router holds `CJAppSpawn`, Cangjie apps
  cannot start. A product needs an ART branch in AppMgr (question 2).
- The bundle requests only the network permissions (ADR-0004).
- Android's input method manager binds no IME yet, because the IME metadata is not supplied; the
  keyboard shows on `showSoftInput`. Typing into a managed app is not verified.
- A SurfaceView's window is placed 54 px off.

## Implementation order

1. **Register APKs as real OH application identities.** This means a verified APK installation and
   package adapter that records the signing identity, runtime type, component mappings, private data
   directories, kernel uid and access token. Unrelated packages get separate security principals.
   Virtual Android uids supplement this mapping; they cannot replace kernel filesystem isolation.
2. **Make AppMgr authorize and track every application process.** AppMgr selects the runtime from
   trusted installation metadata, creates the process record, and asks the matching worker to spawn the
   process. OH's specialization machinery is reused where practical.
3. **Attach the ART child to the process record OH created.** The ART child completes the path in
   decision 3. The direct-launch bypass stays until the managed path passes acceptance.
4. **Give each APK its own OH-authorized main window session**, as decision 4 describes.
5. **Complete common lifecycle and resource policy.** This covers foreground, background, memory
   pressure, termination and configuration callbacks, each with correct acknowledgements.

Until this ADR replaces direct launch, ADR-0003's stages 1 and 2 still apply: spawner hardening, and
the Play hosts under their own principal. Its stages 3 and 4, the per-application identities, come
from OH registration under this ADR instead of a Westlake-reserved uid range.

## Acceptance criteria

- **First milestone:** one APK launches entirely through OH, with its own package identity, process
  record, ability and main window session.
- **Then two unrelated APKs and one HAP:**
  - OH application records attribute each package correctly, and the processes carry the matching
    kernel credentials.
  - Each application owns its windows independently. Focus, input, IME, rotation and lifecycle
    events behave normally.
  - Stopping or killing one application cleans up its windows without affecting the others.
  - Access to another application's private files fails, and so does use of its window token.
  - OH can see and control secondary and isolated processes, background transitions and
    memory-pressure termination.
- **Before the bypass leaves production:** the HK corpus draws no fewer apps on the managed path than
  on the direct-launch path.
- Record each property as implemented, exercised or passed. Source inspection alone never counts as
  passed.

## Consequences and alternatives

**Gains.** APKs get OH's own focus, IME, multi-window, task switching, cleanup and per-application
attribution. Per-application kernel identity comes from OH rather than from a Westlake registry.

**Costs.**
- host changes: runtime selection in AppMgr, and possibly support for named processes;
- a signed bundle for each APK;
- a migration period during which the direct-launch path remains.

**Alternatives considered.**
- **Keep direct launch, with Westlake-managed identities** (ADR-0003 stages 1–3). This is cheaper,
  but OH never sees the applications, and their windows stay borrowed.
- **One host bundle for every APK.** Every APK would share one identity, so this is rejected.
- **Share only the compositor.** This does not establish window ownership, focus, lifecycle or
  isolation.

## Evidence

- [Direct-launch bypass and the reverted OH path](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/jni/activity_task_manager_adapter.cpp#L1329)
- [AppMgr attachment](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/jni/oh_app_mgr_client.cpp#L73)
- [AppMgr lifecycle callbacks](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/jni/app_scheduler_adapter.cpp#L203)
- [Session token dropped](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/jni/ability_scheduler_adapter.cpp#L864)
  and [only logged](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/java/AbilitySchedulerBridge.java#L515)
- [Windows parented under the host ability](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/window/jni/oh_window_manager_client.cpp#L155)
  and [its focus and identity workarounds](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/window/jni/oh_window_manager_client.cpp#L1663)
- [OH AppSpawnClient](https://github.com/openharmony/ability_ability_runtime/blob/master/services/appmgr/src/app_spawn_client.cpp#L50)
  and [OH WindowScene](https://github.com/openharmony/window_window_manager/blob/master/wm/src/window_scene.cpp#L86):
  match these against Westlake's OH baseline before implementing

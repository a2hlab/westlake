# ADR-0008: Manage APKs and HAPs under one OH process and security model

- **Status:** accepted 2026-11-13 as the long-term direction (proposed the same day). A feasibility spike
  comes first. Nothing here is implemented or device-verified.
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md) for identity and confinement,
  [ADR-0004](0004-scoped-play-compatibility-and-permissions.md) for Android permissions
- **Extends:** [ADR-0001](0001-shared-play-services-process.md), [ADR-0002](https://github.com/a2hlab/westlake/pull/107)
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
   (proposed) applies the same rule to system services.
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

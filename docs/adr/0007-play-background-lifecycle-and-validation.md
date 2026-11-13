# ADR-0007: Supervise Play processes and validate background delivery end to end

- **Status:** accepted 2026-11-13 (proposed the same day); not implemented
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md), [ADR-0004](0004-scoped-play-compatibility-and-permissions.md), and [ADR-0002](https://github.com/a2hlab/westlake/pull/107)
- **Extends:** [ADR-0001](0001-shared-play-services-process.md), milestones 1, 6 and 7

## Context

Push to a running app is recorded working. The merged closed-app work queues broadcasts and leaves a
start request, but its PR does not demonstrate the complete cold-start delivery path. The latest
experimental code starts declared Google processes and repairs several PendingIntent and restart
failures. A short successful foreground sample does not establish survival across screen-off,
suspend, network changes, low memory or prolonged idle.

GrapheneOS adapts background execution with its compatibility helper and framework hooks; its usage
guide asks for a battery-optimization exception for Play services. Westlake must express the intended
behavior through OpenHarmony's process, power and notification services. An adapter returning success
for a wake lock does not by itself keep a process or connection alive.

## Decision

1. **Give a trusted supervisor responsibility for process lifetime.** Track authenticated app/process
   records, launch generations, bindings and death. Start only declared, authorized components through
   the spawner. Restart eligible services with bounded backoff, reconnect clients and propagate Binder
   death/disconnection correctly. Prevent restart storms and PID-reuse signals. Under
   [ADR-0008](0008-one-oh-process-model-for-apks-and-haps.md), OH's AppMgr is that supervisor, and
   Westlake translates Android's service and restart semantics onto it.
2. **Implement Android background semantics through real host mechanisms.** Map necessary alarms,
   jobs, foreground services, notification delivery and permitted temporary execution windows to OH.
   Make user-visible background exceptions explicit and revocable. Keep the helper's authority narrow;
   Google processes retain ordinary application privileges.
3. **Complete cold-app push delivery.** Authenticate the sender and target, validate receiver export
   and permission rules, start an eligible receiver process, deliver the message and finish its work
   window. Define bounded queues, expiry and retry behavior. Distinguish a process killed by resource
   pressure from an application the user force-stopped; respect the platform's stopped-app semantics.
4. **Preserve permission attribution across Google APIs.** Test provider access, PendingIntent creator
   identity and calls relayed through multiple Google processes. Port GrapheneOS-style location
   redirection to the host-backed location service where its contract is implemented, preserving each
   requesting application's permissions and attribution. Mark unsupported operations explicitly.
5. **Publish feature results with distinct completion levels.** Record service connection, callback,
   successful result and user-visible outcome separately. Track deployment revision and process
   health. Callbacks carrying errors are useful evidence of transport, not proof of feature success.

## Implementation boundaries

Build on `AppProcesses`, `PlayServicesChannel`, jobs, receivers, PendingIntents and existing OH adapters.
The November 13 process branch already repairs multi-hop sender metadata, selected activity routing
and the module-config deletion loop. Revalidate those paths instead of reintroducing the earlier
single-process design.

The new `.device_protected` directory separates storage paths. It does not prove credential-encryption
key handling or locked-user/direct-boot behavior. Define those capabilities explicitly before claiming
Android CE/DE security semantics, and include them in the lifecycle test matrix when supported.

## Acceptance criteria

- Deliver test pushes to foreground, background and eligible process-dead apps; verify the intended
  app displays or handles the payload. Test reboot, screen-off, suspend/resume, network loss/recovery,
  host death and user force-stop separately, after establishing what screen-off and suspend do on the
  test board. Record delivery latency, expiry and duplicate behavior.
- Kill individual Google processes and fixture clients. Recover eligible bindings and work without
  impersonation, losing final callbacks or repeatedly spawning failed components.
- Run the ADR-0001 24-hour host-stability test with periodic exercised APIs. Measure idle and active
  RSS/PSS, CPU time, wakeups and API latency against the same workload on the existing backend. The
  test board has a dummy battery, so energy needs an external meter or another device. Set regression
  budgets before interpreting the run.
- Validate sign-in/token use, location with grant/revocation, background notifications and any claimed
  billing/Store capability independently. Do not infer billing or integrity success from app drawing.
- Run the app corpus with healthy hosts and a windowless Store during screenshot measurement. Exclude
  captures covered by the Store UI and runs where a required host was dead; retain the reason.
- Produce a capability matrix distinguishing implemented, exercised, passed and unsupported behavior.
  Production readiness also requires ADR-0003's adversarial isolation tests and ADR-0004's grant tests.

## Consequences and alternatives

Reliable background behavior consumes resources that foreground-only compatibility can avoid. A
supervisor and honest power integration make those costs measurable and controllable. Indefinitely
keeping every app active or reporting fictitious wake locks would conceal the problem and is not
selected. A broad performance claim waits for comparable measurements.

## Evidence

- [Closed-app broadcast implementation and its validation limit](https://github.com/a2hlab/westlake/pull/72)
- [Latest multiprocess results](https://github.com/a2hlab/westlake/pull/157)
- [Device-protected directory implementation](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/package-manager/java/DataDirs.java)
- [GrapheneOS background hooks](https://github.com/GrapheneOS/platform_frameworks_base/blob/865521d49280706d6877d0a0636de2cdd8210c36/core/java/com/android/internal/gmscompat/GmsHooks.java)
- [GrapheneOS Play configuration](https://grapheneos.org/usage#sandboxed-google-play)

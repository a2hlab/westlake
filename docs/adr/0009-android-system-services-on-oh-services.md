# ADR-0009: Back Android system services with the OH services that HAPs use

- **Status:** proposed 2026-11-13
- **Date:** 2026-11-13
- **Depends on:**
  - [ADR-0003](0003-authenticated-app-identity-and-isolation.md) for authenticated caller identity
  - [ADR-0004](0004-scoped-play-compatibility-and-permissions.md) for permission grants
  - [ADR-0008](0008-one-oh-process-model-for-apks-and-haps.md) for the shared process and security model
- **Implementation baseline:** experimental `play-processes` at `d1dcb27760fc59ccc6f3af8ed8e24302e795b497`. The mappings
  below are proposed; none of them is a claim of implemented parity.

## Context

ADR-0008 gives APKs the same process, lifecycle, window and security management that HAPs have. The system
services an app calls are the other half of that picture. When an APK and a HAP use the same resource, both
should reach the same OH service:

```text
APK -> Android API -> Westlake adapter -> OH service
HAP -> OH application API -------------> same OH service
```

At the baseline, several Westlake services still answer from state held inside the app's own process:

- **Location.** `LocationManagerAdapter` reports that providers exist but none is enabled, and gives no fixes.
  Its own comment calls this "the compatibility floor, not a substitute" for routing to OH's location
  service.
- **Sensors.** The `SystemSensorManager` boundary reports an empty sensor list.
- **Clipboard.** Each process keeps one clip of its own. Another app or a HAP never sees it.
- **Notifications.** `LocalNotifications` keeps an app's channels and posts and plays their sound. Nothing
  reaches OH's notification centre.
- **Audio.** Volume, ringer mode and audio focus are kept per process, and focus is always granted.
  Playback itself already reaches OH's audio output.
- **Power and battery.** The power-save state is answered. Every other PowerManager call returns its type's
  default value, so a wake lock is accepted but not held. Battery properties are invented constants: full,
  at 100%.
- **Connectivity.** `ConnectivityManagerAdapter` reads OH's network state, but it delivers callbacks from a
  poll that runs once a second.

FuriLabs' FuriOS (Andromeda) bridges Android services to Linux host services, and its design shows forms
that work:
- location from GeoClue, tracked only while someone asks for it;
- a sensor list, activation and event conversion;
- desktop notifications with actions, and media controls that reconnect after a failure;
- a clipboard bridge.

It also shows what to avoid. Its sensor `BATCH` call reads its parameters and returns success without
applying them, and its vibrator `OFF` call reports success without stopping the vibrator. A success
answer that does not do what the API promises is worse than a defined failure.

## Decision

1. **Back each Android system service with the OH service a HAP uses for the same resource.**

   | Android API | OH integration |
   |---|---|
   | LocationManager | OH location service |
   | SensorManager | OH sensor service |
   | ClipboardManager | OH pasteboard |
   | NotificationManager | OH notification service |
   | AudioManager | OH volume, routing and audio-interruption management |
   | PowerManager | OH power state, and the background-execution mechanisms OH authorizes |

2. **Translate behavior, not method signatures.** Each adapter translates permission checks, callbacks,
   cancellation, errors, resource ownership and lifecycle. For example, a location request must deliver
   updates, stop when cancelled, and stop when the location permission is revoked.
3. **Authorize at the host service.** The host service authorizes each caller by its authenticated
   identity (ADR-0003). Android permissions map to explicit grants (ADR-0004).
4. **Write compatibility logic where OH has no equivalent.** PendingIntent, task stacks and job
   constraints have no direct OH counterpart and need explicit compatibility logic. Where OH's public
   APIs cannot supply the behavior, use a trusted platform service.
5. **Keep what works.** The Java libraries, ART and the UI framework stay as they are. Replace the
   process-local answers one service at a time.
6. **Fail honestly until a service is backed.** An operation that is not implemented returns a defined
   failure or "unavailable". It never returns a success that does nothing.

## Acceptance criteria

Test each service on its own:

- **Effect and callbacks.** The real host effect happens and can be read back, from OH and from a HAP
  where possible. Callbacks arrive.
- **Cancellation and revocation.** Cancelling stops the effect, and revoking the permission stops
  delivery.
- **Process death.** When the app's process dies, everything it held is released.
- **Service restart.** A restart of the OH service, or of the Westlake host, is recovered from.
- **Concurrent HAP use.** A HAP that uses the same resource at the same time sees consistent behavior:
  - text copied in one pastes in the other;
  - they share volume levels;
  - audio interruptions work between them.
- **Power.** Measure wakeups and idle consumption instead of inferring them from API success. On the test
  board, which has a dummy battery, this needs an external meter.
- **Status.** Record each service as implemented, exercised or passed.

## Consequences and alternatives

Each service becomes host-integration work that follows OH's permission and lifecycle rules. Some
services need a trusted platform service. In return, the app's answers stop disagreeing with what the
user sees in OH: copied text pastes in a HAP, and the volume keys and an app's volume agree.

Alternatives considered:
- **Keep the process-local answers.** This is cheap, but OH and other apps never see the app's state.
- **Use success stubs, as some FuriOS calls do.** Rejected, because they hide missing behavior.

## Evidence

- Westlake at the baseline:
  - [location](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocationManagerAdapter.java#L31)
  - [sensors](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/android-runtime/src/AndroidRuntime.cpp#L3610)
  - [power](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L51)
  - [notifications](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L726)
  - [audio](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L750)
  - [battery](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L935)
  - [clipboard](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L1069)
  - [connectivity polling](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/ConnectivityManagerAdapter.java#L37)
- FuriOS (FuriLabs), inspected without device tests:
  - [location](https://github.com/FuriLabs/andromeda/blob/9311694/tools/services/gnss_manager.py)
  - [notifications and media](https://github.com/FuriLabs/andromeda/blob/9311694/tools/services/notification_client.py)
  - [clipboard](https://github.com/FuriLabs/andromeda/blob/9311694/tools/services/clipboard_manager.py)
  - [sensors](https://github.com/FuriLabs/andromeda-sensors/blob/0cec4e1/Sensors.cpp), and its
    [BATCH that applies nothing](https://github.com/FuriLabs/andromeda-sensors/blob/0cec4e1/service.cpp#L252)
  - the [vibrator OFF call that stops nothing](https://github.com/FuriLabs/android-binder-services/blob/cf4029c/vibrator/vibratorhal.c#L123)

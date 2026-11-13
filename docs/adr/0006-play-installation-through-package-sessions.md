# ADR-0006: Complete Play installation through verified package sessions

- **Status:** proposed
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md), [ADR-0004](0004-scoped-play-compatibility-and-permissions.md), and [ADR-0002](https://github.com/a2hlab/westlake/pull/107)
- **Extends:** [ADR-0001](0001-shared-play-services-process.md)

## Context

The genuine Play Store home page loads and native platform Cronet works in the experimental branch.
Store requests for asset and feature delivery receive callbacks. Declared secondary processes now
allow components that previously failed during dependency injection to start. Those are useful
prerequisites, but they do not establish successful installation.

At `d1dcb27760fc59ccc6f3af8ed8e24302e795b497`, `LocalServiceBinders.packageInstaller()` throws for
`createSession`. Merely returning `com.android.vending` as an app's installer also does not establish
an actual installation record or the Google account's server-side entitlement. The Play Store may
correctly refuse a license or key for a sideloaded app it does not recognize as acquired.

## Decision

1. **Provide a real PackageInstaller session service.** Authenticate the installer and session owner.
   Implement the session operations exercised by Play, including staging, progress, abandon, commit,
   result delivery and restart recovery. Keep the state in a trusted service outside app-controlled
   adapter processes. Return explicit unsupported errors for unimplemented session modes.
2. **Verify packages before publishing them.** Validate APK signing identity and supported rotation,
   package/version consistency, split relationships, ABI and SDK constraints. Enforce update ownership
   and downgrade policy. Stage into installer-owned storage and commit atomically, leaving the
   previous version usable on failure. Protect installed artifacts from client mutation.
3. **Keep Play unprivileged.** Require the applicable source authorization and user confirmation for
   installation. Support unattended updates only when their actual eligibility conditions are met.
   Cancellation and refusal must reach the genuine installer through the documented callbacks.
4. **Record what actually happened.** Publish installed packages, splits, versions, signing identity,
   installer attribution and update ownership from committed transactions. Remove unconditional
   installer attribution from the production path. Report Google account entitlement only through
   Google's genuine responses; local package metadata cannot manufacture it.
5. **Complete component and result routing.** Route the Store's download/install services, jobs,
   receivers and PendingIntents to their declared processes. Deliver package changes and session
   status to authorized recipients, including callbacks after a process restart. Use the lifecycle
   contracts in [ADR-0007](0007-play-background-lifecycle-and-validation.md).

## Implementation boundaries

Begin with one free application acquired through the genuine Store using the existing disposable
test account, once that account action is authorized. Establish its real install record and usable
first launch. Then cover update, dynamic-feature splits and Play Asset Delivery separately: an asset
pack is not automatically an APK session, and a download callback is not evidence that an asset or
split became usable.

Reuse the current Cronet and multiprocess work. The staged experimental dependency chain includes
[PR #152](https://github.com/a2hlab/westlake/pull/152),
[PR #153](https://github.com/a2hlab/westlake/pull/153),
[PR #154](https://github.com/a2hlab/westlake/pull/154), and
[PR #157](https://github.com/a2hlab/westlake/pull/157); all were open when this proposal was written.
Their implementation is not being proposed a second time.

## Acceptance criteria

- A user confirms a genuine Store acquisition; bytes download, verification passes, the session
  commits, the correct package/splits appear, the application launches, and the caller receives the
  final result. Record each step rather than counting a successful bind or callback as installation.
- Rejection, cancellation, interrupted download, disk exhaustion, process death and reboot leave a
  consistent package registry and no partially published version. Retry does not duplicate commits.
- Invalid signatures, incompatible splits, unauthorized updates and attempts to access another
  installer's session fail without modifying the installed package.
- A permitted update preserves data and attribution. An unauthorized unattended update cannot bypass
  consent. Split delivery produces a loadable feature; asset delivery produces consumable pack data.
- Distinguish Store licensing/ownership failures from transport failures. A Play Integrity callback
  is recorded separately from its decoded verdict and is never treated as certification.

## Consequences and alternatives

This adds an installation subsystem and persistent transaction state, with host launch and filesystem
integration. Broadly faking installer attribution or returning successful empty sessions is cheaper
but cannot provide correct installs, account entitlements or recovery. Keeping explicit failure is
preferable until each supported session path is complete.

## Evidence

- [Current installer stub](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L692)
- [Latest Google process results](https://github.com/a2hlab/westlake/pull/157)
- [GrapheneOS Store installation behavior](https://grapheneos.org/usage#sandboxed-google-play)
- [Google account licensing and integrity verdicts](https://developer.android.com/google/play/integrity/verdicts)

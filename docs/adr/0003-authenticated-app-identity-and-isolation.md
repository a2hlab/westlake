# ADR-0003: Authenticate app identity and enforce isolation between applications

- **Status:** proposed
- **Date:** 2026-11-13
- **Extends:** [ADR-0001](0001-shared-play-services-process.md) and [ADR-0002](https://github.com/a2hlab/westlake/pull/107)
- **Implementation baseline:** experimental `play-processes` at `d1dcb27760fc59ccc6f3af8ed8e24302e795b497`; this proposal is not an implementation claim

## Context

Westlake runs genuine Google Play services and the Play Store and now supports their declared secondary
processes. Separate processes have fixed functional failures, but every application still runs under
one OpenHarmony UID. The shared package UID table in [PR #155](https://github.com/a2hlab/westlake/pull/155)
makes Android-facing identities consistent; it does not change kernel credentials.

`PlayServicesChannel.hello()` accepts a package name and `PackageInfo` supplied by the client. A
same-package claim is treated as another process of the host application. The account-service gate
therefore depends on an unauthenticated identity. `LocalAccounts` stores account data inside a file
sandbox shared with other Westlake applications. This is unsuitable for real user credentials.

The isolation gap has an observed consequence: imo sent SIGKILL to the Play services host.
[PR #156](https://github.com/a2hlab/westlake/pull/156) prevents that Java API path; native `kill()` remains
able to signal other Westlake applications. A process name or a Java permission check cannot be the
authority for a kernel operation.

## Decision

1. **Use a trusted installation and launch registry.** A service outside application-controlled
   processes owns package names, verified signing identities, stable app IDs, profile membership,
   installed versions, grants and permitted process names. Clients cannot write this registry.
   Android-facing app IDs map explicitly to host identities; dividing an OH UID by Android's user
   range is not the definition of an Android profile.
2. **Authenticate channels through the spawner and kernel.** The trusted spawner establishes or
   registers each channel and binds it to the verified package, actual process and launch generation.
   Validate peer credentials against that record, including process lifetime to prevent PID reuse.
   Obtain certificates and package metadata from the trusted registry. A client HELLO may negotiate
   protocol features, but cannot assign its own authority. Inherited descriptors alone do not prove
   the identity of arbitrary processes to which those descriptors might later be transferred.
3. **Assign a distinct OS principal to each application.** Give each package/profile pair its own
   kernel UID and appropriate OH access token and SELinux confinement. Ordinary secondary processes
   share their application's principal. Components declared as isolated processes require a separate,
   restricted principal with no access to the parent application's private data. This replaces
   ADR-0002's temporary ordinary-UID treatment of isolated processes when this ADR is implemented.
4. **Protect files and process operations at the OS boundary.** Establish private data mounts and
   ownership, read-only installed code, restricted descriptor inheritance, and policy for signals,
   ptrace, process memory and IPC. Review supplementary groups and capabilities. Namespace separation
   is an additional control; path visibility alone is not an authorization model. Account data and
   module code must remain protected when an application makes direct native syscalls.
5. **Fail launch closed when required confinement cannot be installed.** UID/GID, mounts, access token,
   SELinux and required syscall restrictions must succeed before application code runs. Reject debug
   bypasses in a production launch. Authentication or UID-registry failures must reject the channel,
   rather than fall back to caller claims or process-local numbering.

## Implementation boundaries

Reuse `SocketBinder` and the stable app-ID allocation concept. Move allocation and identity verification
behind the trusted registry. Audit every authorization consumer of `Binder.getCallingUid`, including
providers, account operations, PendingIntents and same-app process routing. Relaying a Binder object
must preserve defined Android caller/creator semantics without trusting caller-supplied metadata.

`ChildMain::applyDac` and `applySELinux` provide useful existing mechanisms. At the baseline above,
`applySandbox` still contains the filesystem-isolation TODO, and `applyAccessToken` can return success
after a missing token or failed token assignment. The host integration must provision legitimate OH
identities and policy, rather than substitute arbitrary numeric tokens.

The Java signal guard remains useful defense in depth. It is not the acceptance criterion for this
ADR. The same applies to owner-only file modes while every app shares the owner.

## Acceptance criteria

- Two hostile fixture apps cannot impersonate each other, Play services, the Play Store or another
  process of their target, including by forging HELLO signatures and transferring a channel descriptor.
- With the real production policy enforcing, direct native reads/writes, signals, ptrace and process
  memory access across app boundaries are denied. Use synthetic account/token fixtures for these tests.
- Normal secondary processes retain intended same-app access; isolated services cannot read parent
  data or inherit parent authority. Cross-profile calls remain separated.
- Killing/restarting a client, reusing its PID, corrupting/unavailable registry storage, and failing
  each launch-security step do not restore another application's identity or inherited privileges.
- Genuine sign-in, tokens, providers and Play callbacks still work after isolation, with module-file
  sharing supplied through [ADR-0005](0005-read-only-dynamite-module-sharing.md).

## Consequences and alternatives

This requires host OS integration and a small trusted control plane. UID provisioning, OH bundle/token
integration and filesystem policy are implementation work, not solved by the existing virtual UID
table. A feasibility spike must demonstrate those host mechanisms before scheduling broad rollout.

Keeping one UID and intercepting selected Java methods is cheaper but leaves native bypasses. Encrypting
an account file with a key accessible to the same applications does not establish isolation. A VM or
container is outside this proposal; retain Westlake's direct execution model while building its actual
application boundary.

## Evidence

- [Channel introduction and account gate](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/java/PlayServicesChannel.java#L1522)
- [Shared account storage](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalAccounts.java#L37)
- [Spawner isolation mechanisms and gaps](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/appspawn-x/src/child_main.cpp#L1575)
- [GrapheneOS sandboxed Play](https://grapheneos.org/features#sandboxed-google-play)

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

The spawner that ADR-0001 names as the source of identity does not yet authenticate its own requests.
On the test board, appspawn-x runs as root in the `su` domain because a debug shell starts it. Its spawn
sockets are mode 0666, and it does not check the connecting peer. A request supplies the child's uid,
gid, groups, APL, hapFlags, access token, bundle and process name, and in the text form the APK path;
the spawner forks with those values. `applyDac` accepts any uid, `PR_SET_NO_NEW_PRIVS` is set only for
a non-zero uid, and `applyAccessToken` succeeds without a token. ADR-0002's `AppProcesses` sends these
requests from application processes, so applications reach the socket under the enforcing policy. Any
process that can connect can therefore request credentials it does not hold, including an unrelated OH
application's uid. Only these preconditions have been checked; no such request was attempted.

SELinux does not separate applications from each other on this OH build: two separately installed OH
applications both run as `u:r:normal_hap:s0`, without per-application categories. OH separates
applications by uid, per-application mount namespaces and access tokens, so Westlake's boundary must
come from the same mechanisms.

## Decision

1. **Use a trusted installation and launch registry.** A service outside application-controlled
   processes owns package names, verified signing identities, stable app IDs, profile membership,
   installed versions, grants and permitted process names. Clients cannot write this registry.
   Android-facing app IDs map explicitly to host identities; dividing an OH UID by Android's user
   range is not the definition of an Android profile. The shared OH uid currently reads as Android
   user 200, and 67 of 982 scanned HK apps call `isSystemUser`.
2. **Make the spawner an authority before relying on it.** Accept initial launches only from the
   launcher, checked with `SO_PEERCRED`. A running application may ask only for a process its own
   package declares. The spawner identifies the requester from its own record of what it forked, by
   pid and process start time, and takes every credential (uid, groups, APL, access token, APK path)
   from that record, never from the request. It rejects uids outside the application range, an APL
   above the application's, and a missing token. Each child receives its spawn channel as an inherited
   descriptor, so no filesystem socket is exposed to applications. The spawner runs in its own SELinux
   domain, not a debug shell's.
3. **Authenticate channels through the spawner and kernel.** The trusted spawner establishes or
   registers each channel and binds it to the verified package, actual process and launch generation.
   Validate peer credentials against that record, including process lifetime to prevent PID reuse.
   Obtain certificates and package metadata from the trusted registry. A client HELLO may negotiate
   protocol features, but cannot assign its own authority. A channel an application passes to another
   process carries no more than that application's authority, as a Binder object does on Android; the
   host never upgrades it. On a socketpair the kernel reports the creator, the spawner, for both ends,
   so a spawner-made channel is identified by what the spawner bound when it made it. On a `connect()`
   the kernel reports the connecting process, which becomes authoritative once applications have
   distinct uids.
4. **Assign a distinct OS principal to each application.** Give each package/profile pair its own
   kernel UID and appropriate OH access token and SELinux confinement. Ordinary secondary processes
   share their application's principal. Components declared as isolated processes require a separate,
   restricted principal with no access to the parent application's private data. This replaces
   ADR-0002's temporary ordinary-UID treatment of isolated processes when this ADR is implemented.
   SELinux confines Westlake applications as a class; the separation between applications comes from
   their uids, mounts and tokens, as it does for OH applications.
5. **Protect files and process operations at the OS boundary.** Establish private data mounts and
   ownership, read-only installed code, restricted descriptor inheritance, and policy for signals,
   ptrace, process memory and IPC. Review supplementary groups and capabilities. Namespace separation
   is an additional control; path visibility alone is not an authorization model. Account data and
   module code must remain protected when an application makes direct native syscalls.
6. **Fail launch closed when required confinement cannot be installed.** UID/GID, mounts, access token,
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

The package-uid table from PR #155 is kept under the hosts' files, where every application can write
it. Move it behind the spawner with the rest of the registry.

Stage the work so that each step is useful on its own:

1. Spawner hardening (decision 2), which needs no OH change.
2. The Play services and Play Store hosts under a principal that applications do not share, ideally a
   dedicated OH bundle so that OH knows its uid and token. This protects the account store and stops
   native signals to the hosts. Clients that load Play services' modules then need
   [ADR-0005](0005-read-only-dynamite-module-sharing.md).
3. Per-application uids from a reserved range, with per-application mount namespaces (`applySandbox`),
   sharing the host's access token.
4. Per-application OH access tokens. Allocating one needs AccessTokenManager's system API, which BMS
   calls at install, so this means one OH bundle per Android application. The earlier per-APK install
   path (`framework/package-manager/jni/apk_installer.cpp` with its template entry HAP) is a starting
   point.

The feasibility spike's first question is whether OH's window, input, rendering, audio and sensor
services serve a process whose uid BMS did not allocate. The netsys socket permission is already
granted per uid at spawn.

## Acceptance criteria

- A spawn request naming another uid, APL, token or APK, or a process its package does not declare,
  is rejected, and an application reaches the spawner only through its own channel.
- Two hostile fixture apps cannot impersonate each other, Play services, the Play Store or another
  process of their target, including by forging HELLO signatures or taking another application's
  channel descriptor without its cooperation.
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

Real user credentials remain prohibited, as ADR-0001 requires, until this ADR's isolation tests and
ADR-0004's grant tests pass under the production policy.

Keeping one UID and intercepting selected Java methods is cheaper but leaves native bypasses. Encrypting
an account file with a key accessible to the same applications does not establish isolation. A VM or
container is outside this proposal; retain Westlake's direct execution model while building its actual
application boundary.

## Evidence

- [Channel introduction and account gate](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/java/PlayServicesChannel.java#L1522)
- [Shared account storage](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalAccounts.java#L37)
- [Spawner isolation mechanisms and gaps](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/appspawn-x/src/child_main.cpp#L1575)
- [Spawn socket mode](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/appspawn-x/src/spawn_server.cpp#L212)
- [Spawn requests sent from application processes](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/java/AppProcesses.java#L232)
- [Earlier per-APK install path](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/package-manager/jni/apk_installer.cpp)
- [GrapheneOS sandboxed Play](https://grapheneos.org/features#sandboxed-google-play)

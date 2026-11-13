# ADR-0005: Share approved Dynamite modules through read-only file descriptors

- **Status:** proposed
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md) and the hook scoping in [ADR-0004](0004-scoped-play-compatibility-and-permissions.md)
- **Extends:** [ADR-0001](0001-shared-play-services-process.md)

## Context

Google Play services downloads Dynamite modules that client applications load as Java code, native
libraries and resources. Protecting Play services' private storage must not prevent supported clients
from loading the modules they need. Making its entire data directory readable or writable by clients
would defeat the new isolation boundary.

GrapheneOS uses a file service and framework/libcore/loader hooks to replace eligible module paths
with file-descriptor references. Westlake already transports descriptors through SocketBinder and
passes client APKs to the Play Store as read-only descriptors. That transport is useful infrastructure,
but client-APK sharing is not a complete authorization model for Google module files.

## Decision

1. **Expose a narrow module-file service.** An authenticated client requests an approved module artifact
   and version. A compatibility adapter may translate a legacy path into that request, but the service
   resolves it against a module inventory. It must not implement arbitrary reads of Play services' data.
2. **Serve only validated, immutable artifacts.** Constrain resolution to approved module roots and
   inventory entries. Reject traversal, symlink escapes, procfs paths and unexpected file types. Bind
   validation and opening to the same object to avoid replacement races. Record the verified origin
   and integrity mechanism for each module format; a caller-supplied hash alone is not provenance.
   Publish complete artifacts atomically after download validation.
3. **Give consumers read-only descriptors.** Consumers cannot modify module files or the inventory.
   Keep artifacts immutable for the lifetime of open descriptors and mappings, including across a
   module update. Where a format requires a local copy, use an integrity-checked copy inside the
   receiving app's private storage and the required read-only dynamic-code modes before loading.
4. **Support the complete loading path.** Adapt Java class loading, resources, ART dex handling and
   native library loading consistently. A rewritten Java path is insufficient if resource or native
   loading later reopens the old pathname. Keep module-version identity aligned across all loaders.
5. **Bound service resources.** Specify descriptor ownership, duplication, death cleanup, cache limits
   and version retirement. Authenticate clients using ADR-0003 and preserve the app/profile boundary.
   Module sharing never exposes account files, tokens, general preferences or mutable code caches.

## Implementation boundaries

Reuse SocketBinder's SCM_RIGHTS transport and death notifications. Inventory the paths used by actual
Google module loads before choosing the smallest necessary ART/libcore/native-loader changes. Port
GrapheneOS's behavior against Westlake's linker and runtime, rather than assuming its special FD path
syntax is already understood here.

Initially enable the service for pinned, exercised module families. Keep the old shared-storage
development path explicitly non-isolated while bringing up the broker. It cannot be a fallback for a
production-isolated launch. A missing required module must produce a diagnosable failure.

## Acceptance criteria

- A genuine client loads a module's Java classes, resources and native library while direct access to
  Play services' private filesystem is denied.
- Client writes, traversal, symlink swaps and requests for account/token files fail. An authenticated
  module request cannot be changed into an arbitrary file request.
- Concurrent clients continue using one immutable version while another version is installed. A
  consumer cannot cause another consumer to execute a modified artifact.
- Client death, host restart, update and cancellation do not leak descriptors or leave writable
  artifacts accepted as executable code. Cold and warm loads have recorded latency and memory costs.
- Validate the affected Play APIs end to end; successful descriptor transfer alone is not a pass.

## Consequences and alternatives

The broker and loader integration add complexity and require explicit cache/lifetime management.
They preserve private storage while permitting the specific code sharing Play needs. Broad shared
mounts or relaxed private-directory permissions would be simpler but would expose data or mutable
code to unrelated applications and are not selected.

## Evidence

- [GrapheneOS Dynamite client hooks](https://github.com/GrapheneOS/platform_frameworks_base/blob/865521d49280706d6877d0a0636de2cdd8210c36/core/java/com/android/internal/gmscompat/dynamite/GmsDynamiteClientHooks.java)
- [Westlake client-APK descriptor transport](https://github.com/a2hlab/westlake/pull/152)
- [SocketBinder](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/SocketBinder.java)

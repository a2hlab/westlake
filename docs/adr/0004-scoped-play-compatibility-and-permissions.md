# ADR-0004: Scope Play compatibility hooks and enforce real permission grants

- **Status:** proposed
- **Date:** 2026-11-13
- **Depends on:** [ADR-0003](0003-authenticated-app-identity-and-isolation.md) for trusted caller identity and grant ownership
- **Extends:** [ADR-0001](0001-shared-play-services-process.md)

## Context

GrapheneOS runs Google's unmodified applications without their usual system privileges and adapts
specific framework behavior to that environment. Its package parser verifies names, signing
certificates and version constraints before recognizing Google packages. Its framework hooks and
GmsCompat configuration cooperate with Android's actual permission enforcement.

Westlake already follows parts of that approach. `ServiceAnswers` explicitly references
`gmscompat_config`, and the Play hosts' settings workaround avoids a crash when Google reads a
restricted setting. However, the current cross-UID `permission_checker` grants any permission a
package requests in its manifest, and its AppOps check always allows. Requested permissions are not
equivalent to granted permissions, especially for signature and privileged access.

Westlake supplies in-process framework adapters instead of Android's system_server. Installing a
GmsCompat APK alone cannot provide the missing service contracts or authorization authority.

## Decision

1. **Separate compatibility answers from authorization.** Trusted services decide actual permission
   grants, component export access, account visibility and AppOps. Enforce normal, dangerous, signature
   and privileged protection levels, user revocation, and relevant foreground/background conditions.
   Requesting a permission in a manifest must never grant signature or privileged access by itself.
2. **Scope hooks to verified identities and explicit client contracts.** Use ADR-0003's registry to
   identify Google packages by verified signing identity and name. Enable individual hooks only for
   their intended package/process and supported runtime/API versions. Client-side Dynamite or service
   hooks use their own defined eligibility; an application's declaration that it uses Google libraries
   must not give it Google privileges. Preserve signature-rotation semantics where supported.
3. **Port behavior into Westlake's framework and adapters.** Start with the mechanisms exercised by
   existing workloads: safe restricted-settings answers, selected privileged-operation failures,
   service lifecycle and foreground-call handling, and module loading. Preserve genuine denial of
   protected operations. A client-visible compatibility answer may avoid a crash without changing
   what the service or kernel permits. Avoid blanket exception suppression and type-default success.
4. **Make compatibility configuration versioned and independently updateable.** Define a typed schema
   for selected stubs, feature flags and component behavior, with package/process and version scope,
   reasons and test references. Validate updates and authenticate their provenance. Publish immutable
   snapshots atomically; retain the last known good compatible configuration when validation fails.
   Configuration cannot grant OS permissions, modify APK signatures or install arbitrary code.
5. **Track upstream deliberately.** Record the GrapheneOS revision and license for reused code/data.
   Adapt each behavior to Westlake's AOSP source revision and advertised API/extension contracts.
   Current upstream branch 17 is not a drop-in patch for Westlake's mixed framework/runtime baseline.
   Advertise an API or extension only after its required behavior is supported and tested.

## Implementation boundaries

Replace manifest-based grants in `LocalServiceBinders.permissionChecker` with decisions from the
trusted grant service. Apply the same source of truth to package-manager queries, providers and
account operations, so a caller cannot bypass a denial by choosing another API. Bound any grant cache
to a revocable policy generation. Secure the service endpoint using ADR-0003.

Evolve `ServiceAnswers` into explicit, validated compatibility records. Keep stateful services as code
with defined semantics. Restrict `readRestrictedSettingsAsUnset` to verified hosts and test that private
settings cannot leak through its provider. Separate diagnostics for unsupported behavior from a
legitimate empty result; redact credentials and private values from both.

## Acceptance criteria

- An unrelated app requesting a Google signature permission or a platform privileged permission is
  denied across package queries, providers, services and account operations.
- A repackaged APK using Google's package name receives no Google-specific identity or privileged
  access. Tests cover signing rotation, malformed identities and supported version boundaries.
- Revoking a runtime grant or AppOp changes subsequent operations, including those through a cached
  Binder, according to the API contract. An unprivileged app cannot update the grant store or config.
- Supported Play workflows survive expected restricted-operation failures without exposing the data
  or privilege being denied. Unrelated applications retain their ordinary framework behavior.
- Invalid, unauthenticated and incompatible configuration updates do not activate. A valid update and
  rollback preserve active-call consistency and pass the affected feature regression tests.

## Consequences and alternatives

This creates an ongoing compatibility-maintenance obligation as Google changes its APKs. It also makes
permissions a real subsystem rather than a collection of adapter defaults. Keeping hard-coded stubs
is acceptable during migration, but each needs the same scope and denial semantics.

Granting all requested permissions would hide compatibility failures while expanding app authority.
Modifying or re-signing Google's APKs changes the existing ADR-0001 decision and is not selected.

## Evidence

- [Current permission checker](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/LocalServiceBinders.java#L310)
- [ServiceAnswers](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/core/java/ServiceAnswers.java)
- [Restricted settings workaround](https://github.com/a2hlab/westlake/blob/d1dcb27760fc59ccc6f3af8ed8e24302e795b497/framework/activity/java/PlayServicesChannel.java#L446)
- [GrapheneOS package validation](https://github.com/GrapheneOS/platform_frameworks_base/blob/865521d49280706d6877d0a0636de2cdd8210c36/services/core/java/com/android/server/pm/ext/PackageExtInit.java)
- [GrapheneOS hooks](https://github.com/GrapheneOS/platform_frameworks_base/blob/865521d49280706d6877d0a0636de2cdd8210c36/core/java/com/android/internal/gmscompat/GmsHooks.java)
- [GmsCompat configuration and helper](https://github.com/GrapheneOS/platform_packages_apps_GmsCompat/tree/dfa825dc74281ff8d6db90731f8facb44b3238c9)

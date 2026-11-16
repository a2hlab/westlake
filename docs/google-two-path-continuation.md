# Two-path continuation: creator-owned activities and local discovery

This iteration continues the existing microG account broker plus official Play services design.
The product target remains an uncertified OpenHarmony device with an existing Google account,
official services in an OS-enforced sandbox, and independently replaceable services such as location.
These changes do not establish certification, remove Google's policy restrictions, or complete that sandbox.

## Implemented behavior

### Execute a foreign activity PendingIntent at its creator

FotMob previously received a genuine Google identity PendingIntent but could not execute it:
Westlake only supported preparing a stored Intent inside the same application.

The new path sends the capability back through the connection that delivered it. The verified
creator prepares and consumes it, then launches its destination in the declared process. The holder
never receives the stored Intent. Execution keeps the original app's package, signer, live PID,
spawner, calling activity, referrer and result recipient. A destination in another creator process
rechecks the original caller against its root-owned spawner and signed launch APK.

Immutable fill-in restrictions, one-shot consumption, enabled-component checks and the prohibition
on unimplemented URI grants remain. A real capability can authorize a private creator activity;
a fabricated sender claiming the creator's package cannot. Raw stored-Intent reads remain denied
at every foreign forwarding hop. Creator-side preparation uses two workers, a queue of 32 and a
15-second startup deadline. This bound applies to this operation, not to every SocketBinder request.

A relay forwards results, cancellation and process death across intermediate processes. Caller death
closes the launched activity; target-process death sends one cancellation result. An activity that
finishes after forwarding its result does not cancel the new result owner.

The destination also needs the calling app's package record and readable APKs to load its label/icon.
Forwarding the intermediate process's `/proc/self/fd/...` paths is invalid. Build 18 duplicates the
original caller's APK descriptors from the trusted spawner, rechecks its signer, exposes destination-local
paths, and holds a temporary package registration for the operation. It preserves existing HELLO
registrations and other active operations. Completion/cancellation releases only that operation's
metadata and descriptors; it does not migrate credentials or grant permissions.

Supported scope is a single activity in the creator's package. General multi-activity stacks,
arbitrary cross-package destinations, URI-grant transfer and arbitrary consent protocols are not implemented.

### Stop package discovery from recursing between GMS and Store

The longer real-app run found a separate blocker. Both hosts could answer remote service/provider
discovery through the aggregate PackageManager, which consulted the other host again. FotMob's
install-referrer lookup blocked in `ClientSource.queryServices`; GMS worker stacks repeatedly entered
`Root.onTransact -> PackageManagerAdapter.queryIntentServices -> RemotePackages.queryServices`.
One sample had 10,182 threads in GMS and 10,240 in Store. A restricted GMS process also aborted with
`OutOfMemoryError: pthread_create (32MB stack) failed`.

Remote discovery now terminates at the owner: service replies use its source manifest and permitted
code guests; provider replies use its own manifest. Direct component-info requests are restricted
to locally owned packages. Local binding/start/stop resolution also avoids forwarding to another host.
Ordinary application queries can still aggregate answers from both hosts.

This removes the observed discovery cycle. SocketBinder's general cached executor is still unbounded;
this is not a general denial-of-service or resource-isolation claim.

### Refuse an update when preserved data is missing

The manifest launcher previously treated a missing `--carried-app-data` source as a fresh installation.
Passing the package directory rather than its parent therefore silently lost continuity for that launch.
The continuation adds a fail-closed restoration helper and tests that a missing source leaves existing
data intact, a correct update preserves identity data and clears compiled cache, and source=destination
is rejected before any shell mutation.

Use `--carried-app-data /board/carry`, containing `/board/carry/<package>`. Require a non-null
`carried_app_data` in the resulting report and compare identity files before interpreting Google results.
The incorrectly carried diagnostic launches were stopped and excluded from identity-preserving conclusions.
The original data remained saved and was restored; no fresh-identity result is used as a compatibility success.

## Validation and provenance

The candidate builds on the cleaned `google-auth-broker` branch at `40a365f`, including
`remote-activities` at `5defb5a`. It does not reintroduce the old credential-export bootstrap experiment.

The final candidate, build 18, compiles 124 source files into 259 classes, packages the framework, builds all nine boot JARs
and 27 boot artifacts, and passes framework preload on the OH board. The adapter runtime SHA-256 is
`380587f43d2a6dd0add0702397f2fefafd14e4a112e05848dcc1adda1ba93f24`;
the framework JAR is `0a2b1573150d4aee7040b0126e8a51d7ec2e111ce5188ba05c005d0d31d8f452`.
The native runtime, ART, shim and other platform inputs were reused; this is not full source closure.

The unchanged auth policy tests passed 8 cases, delegation policy 36, private-file handling 6,
and the value-free boundary trace test passed. The manifest data-restoration regression has 3 passing cases.

Build 16's board fixture passed all 24 assertions, including private/disabled/protected target denial,
same-app and foreign one-shot execution, immutable fill-in, original caller/process/referrer, nested
Parcelable delivery, forged-creator denial and target-death cancellation. A separate caller-death run
confirmed that the target activity closes while its process survives. Build 17 additionally tests mutual
GMS/Store discovery with source-only APKs. Both its target-death and caller-death runs passed all 26
checks, including 25 repeated rounds of positive and negative service/provider lookups. Captured
main-process thread counts were 21/16/39 for host/store/client in the first run and 22/16/38 in the second.
These are samples after the discovery loop, not lifetime maxima. The assertions and counts are in
[`tests/google-auth/continuation17`](../tests/google-auth/continuation17).

The live microG provider retained its previously validated build-11 runtime and private account store.
The final real-app replay uses build 18 for the policy-1 official GMS and FotMob, with Store on build 17.
The restricted-identity tests use build 17 for GMS, Store and FotMob. All three source-only fixture APKs
use build 18 for the final contract runs. The provider remains pinned and no credential migration is performed.

Build 18's target-death fixture passed 27 checks and its caller-death fixture passed 28, including
readable caller APK metadata in the destination and release of that registration after caller death.
Host/store/client thread samples were 20/16/38 and 21/16/40 respectively. These are separate test
scenarios, with overlapping assertions, not 55 unique contracts. Final results and runtime provenance
are in [`tests/google-auth/continuation18`](../tests/google-auth/continuation18).

## Google policy observations

The original restricted official GMS identity recorded `uncertified_status=3`. Its complete data was
preserved when deploying the broker and build 16. Both the check-in identity and complete identity-token
file matched the pre-update values; policy remained 3 with no override. A subsequent restart preserved
the same identity file. The existing comparison installation similarly retained its identity and policy 1.

| Direct probe | Restricted before broker deployment | Restricted after deployment |
|---|---|---|
| Phenotype, service 51 | Broker status 0; binder present | Same |
| Ads, service 8 | Broker status 9; no binder | Same |
| Measurement, service 93 | Broker status 9; no binder | Same |
| App Set ID, service 300 | Bind refused | Same |

The initial restricted Store main process was absent. It was restored and a later four-service probe
gave the same results with a live background-only Store. Its first replacement launch had not carried
Store data correctly, so that run alone is not a fully preserved Store comparison. The corrected launch
restored the original Store data. Build 17 repeated the probe with both original data sets preserved
and both hosts verified alive: all four results were unchanged. Do not infer a specific Google server decision from an uncorrelated
bind failure. Broker status 0 is also not full API or business-flow acceptance.

Account brokerage alone did not remove the observed policy-3 service failures. The earlier 26-app
status-1 retest remains a different population of results; it is not evidence that policy-3 devices work.

### FotMob comparison, build 17

The original signed FotMob `239.17834.20261016` was used in both runs. Google sign-in was explicitly
authorized; no application account was created and no permissions/scopes were broadened.

| Existing GMS identity | Observed result |
|---|---|
| Policy 1, identity preserved | Google button visible; identity broker 212 returned status 0. The foreign PendingIntent executed and official `GoogleSignInActivity` launched with FotMob as caller. It returned `RESULT_CANCELED` (0) without a visible picker. No completed sign-in. |
| Policy 3, identity preserved | Google button visible with a live Store. Two identity broker 212 callbacks returned status 9; no PendingIntent launch or account picker. No completed sign-in. |

The policy-1 result proves the original transport blocker was passed, not successful authentication.
Inspection then found the missing destination package/APK registration described above. Google's
activity loads the caller's name/icon before presenting its dialog; the precise build-17 cancellation
code was not captured. The policy-3 refusal occurs before this activity path and is not resolved by
the metadata fix.

### FotMob replay, build 18

With the same policy-1 identity preserved, FotMob again received an identity-service 212 status-0
callback. The creator-owned activity now displayed Google's **Choose an account** dialog, with
FotMob's name and icon. The account list remained empty across captures more than five minutes
apart. There was no completed Google sign-in, app-account creation or successful token result.

The broker reported one account in requests around the attempt. This proves the provider can enumerate
an account; it does not prove that the chooser accepted it or that account selection occurred. A sampled
GMS UI thread dump showed its main looper idle and no blocked account-loading stack. Background GMS
credential/cache reads remained withheld. Those observations do not establish the cause of the empty
list: account eligibility, account metadata and the chooser's fragment/lifecycle integration still need
to be distinguished before changing auth policy. No credential export or extra scope grant was added.

The temporary FotMob process was stopped after capture; provider, official-host data and original
identities were retained. See [the sanitized result](../tests/google-auth/continuation18/fotmob-policy1.json).

The post-flow build-17 thread samples were GMS main/persistent/unstable/UI: 93/85/55/33;
Store main/background/quick-launch/installer: 134/66/41/51. These finite samples contrast with the
observed recursive lookup failure, but are not a soak-test or performance acceptance result.

## Remaining product boundaries

- The official GMS and test applications still share OH UIDs within host bundles. Verified Java channels
  and namespaces do not establish protection against hostile native code sharing a UID. Per-app kernel
  UIDs, SELinux enforcement, data-directory ownership and lifecycle supervision remain prerequisites.
- A diagnostic Store UI launch with incorrectly carried data rendered its catalog, then crashed with
  `This method must be called in primary profile`. That fresh-data launch is excluded from identity-continuity evidence.
  Background-only service testing does not prove Store UI stability. Android/OH user-profile translation
  needs its own fix and validation; do not paper over it by claiming certification or silently changing identity.
- Background GMS credential/cache reads may remain withheld without a live, authorized original-app
  operation. No arbitrary OAuth scope grant or reusable credential forwarding was added here.
- Location replacement, billing, Integrity, payments, DRM, notifications and all 1,000 apps were not
  accepted by this iteration. Google's auth/check-in protocol and service-specific policy remain external dependencies.
- Raw Google logs, screenshots, identities, account data and credentials are private local evidence;
  only allowlisted results and source-only fixture assertions belong in the repository.

## Next acceptance gates

1. Trace the real chooser's account-list and lifecycle path without logging account values. Fix only the
   demonstrated adapter gap, then retest FotMob with the same identity and exact existing grants. A visible
   selectable account and a valid result to FotMob are required; an empty dialog is not sign-in success.
2. Keep the policy-3 identity as the product-target control. Any proposed account/check-in integration must
   demonstrate service availability on that preserved identity without overriding certification policy or
   resetting identifiers. The current broker does not meet this gate.
3. Validate process/data isolation with distinct kernel identities and enforceable policy, then independently
   validate replaceable services such as location. Neither a broker handshake nor the fixture proves these.

## Reproduction

Follow [the activity/discovery fixture instructions](../tests/remote-activity/README.md), using the
same candidate framework on the host, store and client. Run the target-death and caller-death cases
separately. Keep their endpoints separate from the real Google hosts.

For real Google tests, start the pinned microG provider, restore official GMS and Store data with the
manifest launcher, verify identity equality/policy/absence of overrides, then launch the original signed
corpus app. Provider spawner pins must match the live provider. Keep independent results for policy 1
and policy 3, verify every required host is alive, and stop before any new application-account signup.

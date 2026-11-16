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
FotMob's name and icon. The account list appeared empty across captures more than five minutes
apart. There was no completed Google sign-in, app-account creation or successful token result.

The broker reported one account in requests around the attempt. This proves the provider can enumerate
an account; it does not prove that the chooser accepted it or that account selection occurred. A sampled
GMS UI thread dump showed its main looper idle and no blocked account-loading stack. Background GMS
credential/cache reads remained withheld. Those observations do not establish the cause of the empty
list: account eligibility, account metadata and the chooser's fragment/lifecycle integration still need
to be distinguished before changing auth policy. This was the build-18 diagnosis; the subsequent
resize investigation below resolves the apparent empty list. No credential export or extra scope grant was added.

The temporary FotMob process was stopped after capture; provider, official-host data and original
identities were retained. See [the sanitized result](../tests/google-auth/continuation18/fotmob-policy1.json).

The post-flow build-17 thread samples were GMS main/persistent/unstable/UI: 93/85/55/33;
Store main/background/quick-launch/installer: 134/66/41/51. These finite samples contrast with the
observed recursive lookup failure, but are not a soak-test or performance acceptance result.

### Dialog resize investigation, builds 19–23

Value-free, temporary diagnostics on the real chooser found one account in its loaded model and
two adapter rows (account plus add-account) at both 15 and 35 seconds. The dialog grew from
1139 x 414 to 1139 x 669, but its lower content was not visible. This was a rendering defect,
not evidence that microG failed to enumerate the account. The object-graph diagnostics were
removed before the production candidate; no proprietary disassembly or account values are published.

The investigation found three rendering gaps:

1. Android's `SurfaceControl.Transaction.setBufferSize` queues its size until `apply()`, while
   Westlake's native `setPosition` immediately writes RS bounds using the current cached size.
   A combined transaction moved the growing dialog but reused its old height. `applyPosition`
   now applies the buffer size before position, closes its transaction, and handles movement to
   the origin as well as nonzero positions.
2. BLAST updated its bookkeeping and the layer, but did not resize the native buffer producer.
   Correcting layer bounds alone stretched the old, clipped buffer. `BBQ_nativeUpdate` now
   validates dimensions, unwraps the Android window wrapper, and changes OH buffer geometry
   through the C API while preserving buffer usage. Unchanged geometry avoids a redundant set.
3. The real chooser still retained a stretched frame after the producer resize; tapping its
   noninteractive header caused a repaint and revealed the account row. Build 23 schedules one
   invalidation in the next animation phase after a size change, checks that the view remains
   attached, and discards the tracked size on window removal. It does not poll or trigger a
   recurring relayout. The solid-color fixture also passed with a delayed resize on build 22,
   so that fixture alone does not establish this last fix. On build 23, the quiet real chooser
   displayed the account row automatically, without the diagnostic header tap.

The source-only `Fixture$Resize` draws a colored dialog and a yellow marker near its bottom.
The same signed fixture fails on the old build-18 runtime: after growth from 240 to 720 pixels,
the displayed height remains 240. Candidate 22 passes all four screenshot checks: centered
600 x 240, growth to 600 x 720, shrinkage to 600 x 360, and movement to (0,0) at 600 x 480.
The bottom marker is present at its correct, unscaled location in each case. This checks actual
composited pixels, rather than accepting Java's reported layout dimensions. Build 23 also passes
all four cases with the resize delayed until one second after input.

Candidate 23 uses the source-built Java adapter from build 23 and the incrementally rebuilt
`liboh_android_runtime.so`. Framework preload passes on the board. Other native objects, ART,
the bridge, shim and platform inputs are reused; this is not a clean full-source build.
The test provider and Store are unchanged. Official GMS and FotMob data are carried forward,
with the original GMS identity checked for equality; no account reset or grant expansion is used.

With build 22 and the one diagnostic header tap, choosing the existing authorized account returned
`RESULT_OK` (-1) from official Google UI to FotMob. The provider recorded two OAuth token results
and one ID-token result for FotMob, with token and expiry present and no additional consent required;
only those booleans are retained. FotMob removed its sign-in prompt and displayed a profile menu
containing **Sign out**. No separate application signup form appeared. The private grants-file hash
and original GMS identity still matched their pre-test values. Build 23 repeated the Google
`RESULT_OK` handoff with the account row visible automatically. These results establish the
Google token handoff and initial signed-in UI, but not a durable application session.

### FotMob session diagnosis after build 23

FotMob subsequently set its reauthentication flag. Private inspection found an unexpired ID token
whose audience matched the existing approved client and whose subject matched the app's stored
Google identifier, but whose email claim was absent. This is a claim-shape check, not cryptographic
verification of the JWT. The app stored an empty Google account name. Inspection of the original
signed app confirmed that it marks that state as a dead session before attempting token refresh.

The provider honored official GMS's `include_email=0` and `include_profile=0` request. Existing
private grants already allow email/profile, and were not expanded. Build 24 tested support for
GMS's aggregate `^^snowballing^^` cache metadata address; it still returned an email-less ID token
and FotMob requested reauthentication. That unsuccessful adaptation was removed. Inspection of
token-cache property names, excluding credential values on-device, found separate `profile`
and `openid` requests. There was no `email` scope request. Do not attribute this result solely
to a missing aggregate-cache implementation.

Build 26 implements explicit, opt-in identity-claim adaptation, enabled only with
`WESTLAKE_GOOGLE_IDENTITY_CLAIMS=1` on the official host in addition to the existing delegation
guards. For the verified `GoogleSignInActivity` target of an active incoming request, it asks
Google for email/profile claims on the audience-bound ID token. Other activities, OAuth access
tokens and server authorization codes retain their original requests. This follows the GIS
[sign-in API's basic-identity contract](https://developers.google.com/identity/sign-in/android/sign-in-identity);
it is a compatibility experiment, not proof that every version of GMS needs the adaptation.

The provider still verifies original caller signatures, the exact approved client ID, explicit
email/profile grants and the live selection witness before and after network I/O. It requests
claims from Google; it does not edit a JWT, fabricate user information, combine scope grants or
override server consent. Opaque request options remain unchanged and separately pinned. The
original cache key keeps its expiry association, and invalidation uses the adapted provider key.
The feature is disabled by default.

### Board result, build 26

The official chooser again displayed its account row automatically. Selection returned
`RESULT_OK` to the original signed FotMob APK. Exactly the audience-bound ID-token request was
adapted; the two OAuth access-token requests were unchanged. The resulting token's RSA signature
was verified locally with Google's public JWKS, without sending the token to a verification
endpoint. Its issuer, approved audience, issuance/expiry times, subject and verified email claim
passed the checks. FotMob stored the matching, nonempty Google account name. The original GMS
identity and operator-grants hash remained unchanged; policy remained 1 with no certification
override. See [the sanitized result](../tests/google-auth/identity-claims26/fotmob-policy1.json).

FotMob then invoked `com.google.android.auth.IAuthManagerService`, transaction 5, for silent token
refresh. GMS reached the post-picker token-cache/password boundary; no further provider token
was issued after the activity result. The current broker cannot authorize that read once the
live selection has ended. FotMob set its reauthentication flag again even though the stored token
was signed, unexpired and contained email. Thus the missing-email failure is fixed in the opt-in
experiment, while durable application-session acceptance still **fails**. Backend acceptance,
expired-token refresh and session retention across restart are not established.

Supporting refresh requires a separate, explicit authorization path tied to the verified original
app's live request, previously authorized account, exact client ID and existing scope grants.
That path must recheck revocation and caller liveness and retain denial for unrelated GMS
background work. Returning `true` from the existing picker witness after the picker closes, or
exporting a reusable Google account credential, would not satisfy this boundary. Additional
request-option and backend failures may emerge once the refresh path is implemented.

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

1. The chooser/rendering and Google result-handoff gate passes on policy 1; build 26 also verifies
   the opt-in identity-claim repair. Implement and negatively test the separately authorized silent
   refresh path, then validate backend acceptance, token expiry and session retention across restart.
   Initial profile UI or a cryptographically valid Google token alone is insufficient.
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

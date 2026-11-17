# Two-path continuation: creator-owned activities and local discovery

This iteration continues the existing microG account broker plus official Play services design.
The product target remains an uncertified OpenHarmony device with an existing Google account,
official services in an OS-enforced sandbox, and independently replaceable services such as location.
These changes do not establish certification or complete that sandbox. Google server policy remains independent.

Latest result: candidates 41–42 fix the repeated-launch overlay corruption and demonstrate
an opt-in OH host-window bridge. FotMob opens Google's chooser and returns automatically after
success or cancellation. Three in-place FotMob restarts and a restart of both GMS and FotMob pass.
The paired restart yields ten silent token replies without another account selection. The original
policy-3 identity and uncertified status remain unchanged. Candidate 43 additionally verifies an
exact renewed ID token in a silent client reply after the carried session token expired. Only the previously described local
identity-service exceptions are enabled. See [lifecycle validation](#in-place-restart-and-host-window-continuation-candidates-4142).
This is development integration. Production isolation, broad service availability and app/backend
acceptance remain open.

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

The candidate at this stage, build 18, compiles 124 source files into 259 classes, packages the framework, builds all nine boot JARs
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

### Silent-token failure: traced diagnosis, build 27

Build 27 adds opt-in, value-free diagnostics and repeats the authorized FotMob flow on
the preserved policy-1 identity. It does **not** implement a new authorization path.
The original signed FotMob 239.17834.20261016 and official GMS 24.40.34 are unchanged.
The chooser displays the account automatically; selection again returns `RESULT_OK`.
The new ID token passes local Google-JWKS signature, issuer, approved-audience,
expiry, subject and verified-email checks. At the post-flow inspection it is only
29 seconds old, yet FotMob has set `user_must_re_authenticate=true`.

The [sanitized trace result](../tests/google-auth/refresh-diagnosis27/fotmob-policy1.json)
records this sequence, relative to the Google activity result:

| Offset | Observed event |
|---|---|
| -1,072 ms | microG returns an audience-bound ID token with expiry; no new consent required. |
| 0 ms | Official Google UI sends `RESULT_OK`; FotMob receives it. |
| +455 ms | The GMS UI broker reports `no_selection=true live_requests=0` for a token read. |
| +458 ms | GMS's reusable-credential read returns `withheld`. |
| +477 ms | FotMob receives `Error=BadAuthentication` from `IAuthManagerService`, transaction 5. No token is returned; a recovery Intent is present. |
| +881 to +2,267 ms | Four further client replies carry the same error. |

These offsets come from device timestamps across the cooperating processes. The
trace has no end-to-end request identifier, so temporal association alone is not
the whole proof: the source lifecycle, matching GMS stack, client response, and
repeated sequence corroborate it. Other GMS background requests occur concurrently
and must not be counted as FotMob requests. The first five-error burst before the
interactive replay independently shows the same failure after carrying app data
into a new process.

The silent request is an audience-bound token request with `include_email=1`, no
profile option, no opaque request options, and no other options. It reaches
`readDelegatedAuth` and fails its selection lookup. Unsupported-service rejections
also appear for unrelated background requests; those are not the reason this
FotMob request fails. There are three successful FotMob provider token events
during the interactive flow (two OAuth and one ID token) and none after its result
through the recorded observation window.

#### The authorization ends before the client can use the result

`RemoteActivities.Incoming.complete()` first marks the request done, removes it
from `INCOMING`, clears its issued-token/expiry map, and clears interactive account
visibility. Only then does it send the result callback. Its `selected()` witness
also requires `!done`, a live callback, the selected account and an unexpired UI
deadline. The ordering makes the post-result denial deterministic; it is not a
race against token expiry or an unexpectedly short picker timeout.

`RemoteActivities.readDelegatedAuth()` can authorize only a selected `Incoming`.
With no such object, it returns null before contacting microG. The durable operator
allowlist and microG account credential still exist; neither is a substitute for
the live selection witness required by the current provider contract. Merely
preserving the token cache would leave this authorization check unsatisfied.

`PlayServicesChannel.readDelegatedAuth()` forwards only method, account and cache
key to the verified GMS UI process. The original app's verified package/signatures,
PID and spawner belonged to the now-completed interactive request. The current
silent-read relay does not supply an independently verified original-app operation.
A package/signature string embedded in a cache key is input, not caller proof.
Keeping the UI process alive, extending its timeout, or pretending a completed
picker is still selected does not supply the missing authorization contract.

#### Why official GMS reports BadAuthentication

The pinned GMS implementation tries its cached token, then its long-lived account
credential. In the inspected build, `tic.h(Account)` checks its account data store
and falls back to `AccountManager.getPassword()`. Westlake deliberately returns
null for that operation: the reusable Google credential stays with microG.
Here `getPassword()` is being used as GMS's long-lived credential slot; the failure
does not demonstrate that the user entered an incorrect password.

In `uao.a`, an empty long-lived credential takes a local error branch before the
authenticated token request is constructed. The branch's `ywx.i` value maps to
`BadAuthentication`. Build 27 captures that exact error in FotMob's Binder reply,
with both token fields absent. This supports a local credential-boundary failure,
not a fresh Google registration rejection. This is not a network packet capture
and does not claim that unrelated GMS background traffic stopped.

Proprietary DEX inspection was used locally to identify these call paths. The
repository contains the conclusions and APK hashes, not disassembly, credentials,
tokens or raw logs. The diagnostic response parser preserves the Parcel position
and prints only allowlisted error labels and field-presence booleans. Unknown
error strings become `other`.

#### Why a valid token and a reauthentication warning coexist

FotMob calls the legacy `GoogleAuthUtil.getToken(Context, Account, audience, Bundle)`
again during its own token/sync path. This is silent token re-acquisition; it need
not wait for expiry and is not an app-held OAuth `refresh_token` exchange. The
original app's `kqc` coroutine handles auth failures and its caller updates the
reauthentication flag. The client SDK decodes the error/recovery fields rather
than receiving a new token. See the public
[GoogleAuthUtil contract](https://developers.google.com/android/reference/com/google/android/gms/auth/GoogleAuthUtil).

There is a further distinction: `pqc.i()` attempts token acquisition and can fall
back to the saved `google_id_token` when the login method is Google. The app can
therefore retain a usable-looking token while reporting that the session needs
reauthentication. This warning alone proves neither backend rejection nor that
the backend was never contacted. We have not measured FotMob backend acceptance;
cryptographic token verification is not a replacement for that test.

#### Required implementation boundary

The missing component is an account/token lifecycle, not another claim patch:

1. Separate persistent authorization of an app/account/client/scopes from each
   temporary operation capability. Retain the existing operator allowlist and
   Google's consent decisions; do not retain a completed picker witness forever.
2. Establish caller identity at the verified app-to-GMS auth-service boundary.
   Bind each silent operation to the actual app signer, live process/spawner,
   selected account, approved client ID, scopes and allowed request options.
   Propagate an explicit capability across process and asynchronous work; a
   thread-local caller or an app-supplied cache key is insufficient.
3. Let the microG broker satisfy only that authorized operation while keeping its
   reusable credential private. Recheck revocation and liveness before delivery.
   Return the expected token/expiry/error contract through the auth integration;
   keep official GMS available for the other service APIs.
4. Define cache reuse, invalidation and expiry consistently across interactive and
   silent requests. The two request option sets differ; claim normalization must
   preserve audience, approved scopes and security-sensitive options such as a
   nonce. Do not alias arbitrary cache keys or ignore opaque options.
5. Validate immediate silent retrieval, actual expiry/invalidation, process restart,
   logout/revocation, caller death, concurrent apps and rejection of the wrong
   app/signer/client/scope/account. Then measure backend session acceptance.

This preserves the two-path design, but it is additional Westlake engineering.
[GrapheneOS sandboxed Google Play](https://grapheneos.org/usage#sandboxed-google-play)
adapts official Play to a normal application sandbox; its documentation does not
establish this microG/official-GMS split or solve the split credential lifecycle.

The broader goal also needs an explicit policy for legitimate authenticated GMS
background services. This run observes withheld background reads; it does not
establish that all official services work after an app-specific token handoff.
Fixing FotMob's silent path alone would not accept Maps, FCM, Store, location
replacement, or the whole corpus.

Finally, the policy-3 control is separate: build 17 recorded identity-service 212
status 9 before any picker launch. Build 27 does not replay or resolve that refusal.
It uses policy 1, preserves the original identity and grant-file hash, and leaves
certification overrides absent. The product-target restricted identity must pass
its own service-availability gate after the lifecycle work.

### Continuing ID-token authorization, builds 28–29

`WESTLAKE_GOOGLE_SILENT_AUTH=1`, together with the existing delegation, identity
enforcement and private-log guards, enables a separate silent-request path in
the official GMS host. It is off by default. The initial supported contract is
the synchronous legacy `IAuthManagerService.getTokenWithAccount` transaction 5
for audience-bound ID tokens. This is the request the original FotMob APK makes.
It does not enable arbitrary background GMS credentials, OAuth access tokens,
server authorization codes, asynchronous auth APIs or opaque token options.

The verified Google sign-in activity still loses its temporary visibility and
token map on completion. When it returns `RESULT_OK`, a provider-approved ID-token
request and the selected account can instead create a private persistent record.
That record binds package, current and legacy signer, a hash of the account,
exact audience service, allowed email/profile claims and revocation generation.
It contains no account name or token, uses atomic replacement and owner-only
permissions, and supplements rather than replaces the provider's operator grants.

At a foreign app's verified GMS socket connection, `SocketBinder` opens a scope
only around the matching auth transaction. `GoogleAuthOperations` parses the
account, audience and known SDK metadata, checks claimed package names against
the APK/spawner-verified identity, and restores the incoming Parcel position.
Unknown options get a denying scope. A package name in a cache key or request
Bundle is never caller proof. A connection may introduce its identity only once;
concurrent introductions are serialized.

The resulting operation has its own capability, connection liveness and deadline.
A thread-local only locates it during the synchronous service call: a read must
also arrive on that exact verified connection. A nested call from another peer
cannot borrow it. The provider receives an explicit witness bound to this operation,
independently re-verifies the original app's live spawner/APK, and rechecks its
existing operator grants and the witness before and after network I/O. The witness
re-reads the persistent selection and its generation. The scope closes in a
`finally` block and discards token/expiry state when the transaction ends. Work
dispatched elsewhere without this context remains denied.

microG retains the reusable credential and returns only the authorized scoped
token. Official GMS still constructs the normal response consumed by the original
app. Silent calls use their actual validated options; they do not inherit the
interactive claim-rewrite rule. The provider continues the experiment's forced
network refresh, so this is not an optimized cache or battery-life result.

Setting an account's visibility to either not-visible state revokes the app's
saved selection when this feature is enabled. A new generation invalidates old
selections and in-flight witnesses; a picker that began its token work before
revocation cannot reinstate that old generation. A new explicit successful
selection may grant access again. Provider-grant revocation remains independently
enforced. Malformed, public or symlinked selection files fail closed. Account
removal/re-addition and each app's own sign-out UI still need separate acceptance;
a local sign-out is not automatically a Google consent revocation.

Host tests cover 23 cases including missing permission, wrong app/signer/account/
audience, claim expansion, unsupported token types/options, persisted re-read,
revocation and stale re-grant, corrupt generation records and unsafe file modes.
The shared host-UID product limitation below still applies to this experiment.

Board validation used the original signed FotMob 239.17834.20261016, official
GMS 24.40.34 and the unchanged microG provider. Java compilation, framework
packaging, boot-image generation and board framework preload passed. The exact
artifacts and compiled source hashes are in
[`silent-session29/provenance.json`](../tests/google-auth/silent-session29/provenance.json).
Build 29 adds serialized channel identification and corrupt-generation rejection
to build 28; its sources match the deployed adapter. Native runtime, ART and shim
inputs were reused, so this is not full source-closure validation.

| Test | Observed result | Evidence |
| --- | --- | --- |
| Successful picker completion, build 28 | Six subsequent silent requests returned tokens through official GMS; no recovery Intent or reauthentication warning. Saved token signature and identity claims verified locally against Google public keys. | [After picker](../tests/google-auth/silent-session29/fotmob-after-picker28.json) |
| Restart official GMS and FotMob, build 29 | Five successful silent replies without another sign-in. The saved selection was carried with the package data; the warning stayed absent. | [Process restart](../tests/google-auth/silent-session29/fotmob-process-restart29.json) |
| Unrelated probe, build 29 | Own identity with FotMob's client ID received no token; claiming FotMob in extras was denied. Direct UI/provider delegation calls were also denied. No provider token event for the probe. | [Four negative cases](../tests/google-auth/silent-session29/negative-probe29.json) |
| Revoke FotMob selection, build 29 | Five silent requests failed with `BadAuthentication`; no provider token was issued. The warning returned even though the previously saved token was still unexpired. | [Revocation](../tests/google-auth/silent-session29/fotmob-revocation29.json) |
| Explicit account re-selection after revocation, build 29 | A new selection was persisted before `RESULT_OK`; six subsequent silent requests succeeded and the warning disappeared. | [Re-selection](../tests/google-auth/silent-session29/fotmob-reselection29.json) |

The revocation test atomically replaced the app's generation as an operator;
it did **not** exercise the adapter's account-visibility hook or FotMob's sign-out
UI. The process-restart test kept the app's earlier saved token, so that saved
value alone does not prove refresh; the new client replies and matching broker
and provider events establish successful silent calls. No test waited for actual
token expiry, measured FotMob backend acceptance, or exercised concurrent callers
and caller death during a silent token request.

The original official-GMS identity and provider operator-grant hash remained
unchanged, and no separate app signup occurred. The tested host retained policy
1 with no certification override. Those builds did not retest or resolve the policy-3 service refusal; the later
restricted-identity results follow below. Raw logs and account screenshots remain private local evidence.

## Restricted identity: local service eligibility, builds 30–33

The original restricted installation still refused service 212 after upgrading to
build 29. Diagnostic build 30 then traced the immediate refusal to GMS's local
service preprocessor. It reads the saved `uncertified_status=3`, consults the
service exception list and constructs status 9 before the account broker runs.
The trace does not establish why Google originally assigned that saved policy,
and does not show a new server refusal for each service request. See the
[baseline](../tests/google-auth/restricted-policy30/baseline29.json) and
[local gate trace](../tests/google-auth/restricted-policy30/local-refusal-trace.json).

Inspection of the pinned GMS 24.40.34 APK identified two different stores:
`uncertified_status` is read through Gservices, while
`uncertified_exceptions_whitelist` is read from device-protected
`gms_chimera_phenotype_flags` preferences. This flag explicitly skips the
Gservices fallback in that build. Changing a same-named Gservices value therefore
would not change this gate. Raw APK disassembly remains private.

Build 31 added only service 212. The callback changed from 9 to 0 and official
`GoogleSignInActivity` presented the account chooser. Selecting the existing
account exposed another dependency: internal identity service 213 returned 9,
and the activity returned cancellation. This is a partial result, recorded in
[the dependency test](../tests/google-auth/restricted-policy30/chooser-dependency31.json).
Build 33 adds exactly 212 and 213, retaining all existing service exceptions.
It does not add Maps, location, ads, Integrity, billing or a wildcard exemption.

`GoogleIdentityServiceConfig` runs before the main GMS application binds, only
for the pinned version and with `WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT=1` plus
the existing delegation/identity/private-log guards. The feature defaults off.
It atomically updates the one preference and keeps a private rollback journal.
Upgrading from the 212-only configuration preserves the original rollback value.
Disabling the switch and restarting GMS restores that value when it still matches
the adapter's last write; a newer Google value is left intact. The check-in
identity, certification status and provider token grants are not rewritten.
Unsupported versions and malformed configuration with the feature active withhold
application bind. A live Google flag refresh may replace the startup adaptation.

This is an explicit local compatibility policy alongside the two-path broker.
The broker supplies authorized tokens; the separate policy lets the tested
identity services reach that broker. It is neither Google registration nor
certification, and cannot guarantee that a Google backend accepts a request.
It is narrower than general GrapheneOS-like Play services availability.

Build 33 passed Java compilation, framework packaging, boot-image generation,
board framework preload and 15 policy host checks. Its exact source and artifact
pins are in [provenance33.json](../tests/google-auth/restricted-policy30/provenance33.json).
Native runtime, ART and shim inputs were reused; this is not full source closure.

| Test | Result | Evidence |
| --- | --- | --- |
| Restricted identity, real FotMob chooser | Services 212 and 213 returned 0; selecting the existing account produced `RESULT_OK`. Eleven subsequent silent token replies matched broker and provider events. The saved ID-token signature and claims verified using Google public keys, and no reauthentication warning was visible. | [Sign-in](../tests/google-auth/restricted-policy30/fotmob-policy3-signin33.json) |
| Unrelated signed probe | Requesting FotMob's audience under the probe's identity returned no token; claiming FotMob in extras and directly calling UI/provider delegation were denied. No provider token event for that probe in the test window. | [Four negative cases](../tests/google-auth/restricted-policy30/negative-probe33.json) |
| Disable and restart GMS | Both added exceptions disappeared and the original preference value was restored, including across the upgrade from the 212-only journal. Other preferences, identity, policy 3 and grants remained unchanged. No new interactive sign-in was attempted during this rollback check. | [Rollback](../tests/google-auth/restricted-policy30/rollback33.json) |
| Restart FotMob in the same staged runtime | Both off/on attempts aborted in native `RenderThread`, with an earlier Chromium `check.cc:297` NOTREACHED failure. Root cause is not established; this is not an authentication restart pass. | [In-place failure](../tests/google-auth/restricted-policy30/in-place-restart33.json) |
| Restart GMS, normally redeploy FotMob with carried package data | Ten new silent replies matched broker/provider events without another account selection. The warning remained absent. The launcher clears app code cache and uses fresh WebView runtime data, so this does not resolve the in-place failure. | [Preserved-data relaunch](../tests/google-auth/restricted-policy30/preserved-data-relaunch33.json) |

The saved token after relaunch may predate that relaunch; the new request/reply
and broker/provider events establish continued authorization. Actual token expiry,
FotMob backend acceptance and fresh Google account enrollment were not tested.
No new application signup form was encountered. Policy remained 3 without a
certification-status override, the original check-in identity matched, and provider
grants were unchanged. Other Google service callbacks still included status 9.
Raw logs, account screenshots, token values and proprietary disassembly remain private.

The two OH host bundles in this restricted test have different kernel UIDs.
The chooser was created under the Play host but stayed behind FotMob's source
host. Bringing the Play host forward exposed the chooser; bringing the source
host forward returned to FotMob. These were manual test-harness foreground
operations. They must not be counted as automatic cross-host window handoff,
or as per-APK kernel isolation within either bundle.

## In-place restart and host-window continuation, candidates 41–42

The staged launcher was not restart-safe. Its original system overlay merged OH files by making
empty mount targets inside the runtime's `etc` and `fonts` inputs. When the mount namespace exited,
those empty targets remained. Later launches treated them as intentional Android overrides. The
failed FotMob runtime's `sehap_contexts` was zero bytes; the firmware's file was 2,510 bytes. This
explains the later pre-ART `HapDomainSetcontext` failures without weakening SELinux.

`native/source_app_namespace.c` now constructs a fresh merged directory per launch and leaves both
input trees untouched. The optional Android install-path cover is also unique per launch, and its
ownership check accepts either the initial app-owned directory or the system-owned directory left
by a successful prior installation. Existing polluted runtime inputs must be restaged from verified
artifacts; carry the app data, not the old overlay placeholders. Per-launch mount directories remain
with the disposable runtime until harness cleanup. This remains a development launcher.

| Check | Observed result |
| --- | --- |
| Original launcher, repeated mount fixture | Modifies its input tree on the first run and hides the real OH policy on later runs. |
| Revised launcher, three launches of the same fixture | Preserves the OH policy, runtime override and Android install-path payload every time; input trees and host policy remain unchanged. |
| FotMob, three restarts of the same candidate-41 runtime | All remain alive, pass the normal SELinux transition, render, and retain app/WebView data; no cache or account reset between restarts. |
| Chromium `check.cc:297` message | Also appears in surviving runs. It is not sufficient evidence of the earlier RenderThread abort's cause; no usable abort backtrace was obtained. |
| Restricted GMS control | Original identity preserved, status remains 3, no certification override, service exceptions remain exactly as configured, and provider grants remain unchanged. |
| Token expiry observation | FotMob's saved token expired. The provider cache contains a Google-signed token for the approved FotMob audience and same subject, issued after that expiry and still valid. Silent replies contained token details. The exact token bytes in those replies and backend acceptance were not verified. |

The initial window prototype called OH StartAbility from the standalone Android process and received
`ERR_INVALID_CALLER` (2097205). Candidate 42 instead routes a small window-only request to the
registered HAP. Its `UIAbilityContext.startAbility()` supplies the real ability token and retains OH's
ordinary permission checks. The caller's foreground host starts the chooser host; the chooser's
foreground host returns to the caller after completion. The mailbox contains only a request ID,
destination and timestamp. Google accounts, tokens, Intents and result payloads stay on the existing
verified activity/auth channels.

This is opt-in development integration, restricted to `org.westlake.authclienthost` and
`org.westlake.authservicehost`, installed with distinct OH UIDs. It requires the manifest launcher's
`--host-window-bridge --host-build ...`, plus an explicit `WESTLAKE_REMOTE_HOST_ROUTES` mapping.
The HAP checks its foreground lifecycle state, destination, size, age and recent request IDs. The
Java caller uses a bounded wait. Mailboxes share the host UID's trust boundary; this is not a
production per-APK policy, a general activity-task implementation, or proof of atomic foreground
ownership through every transition. Production integration needs installer-owned routing and
lifecycle IPC rather than this polling development mailbox.

On the board, FotMob opens the chooser and returns after both cancellation and successful sign-in
without manual host activation within those flows. The success result is `RESULT_OK`; FotMob
shows its signed-in profile, its saved Google JWT passes signature/audience/expiry/identity checks,
and 11 subsequent silent token replies match 11 broker token returns. No separate application
account signup was performed. Restarting both candidate-42 runtimes in place, without clearing
app data or caches, also succeeds: ten silent token replies match ten broker returns, with no
account chooser. This does not independently verify FotMob backend acceptance.

The host policy unit test covers opt-in, background denial, wrong destination, expiry, replay,
malformed requests and OH rejection. Board tests also deny background, wrong-destination and
expired requests. After explicitly verifying another host owns WindowManager's focused window,
cancelling the chooser delivers cancellation while keeping that third host focused. A first attempt
at that test was invalid: the root-owned input file replayed the cancel tap repeatedly. Correcting
its ownership and checking actual focus removed that test artifact.

Sanitized evidence and build pins are under [lifecycle41](../tests/google-auth/lifecycle41/).
Raw logs, account stores and screenshots remain private. The native runtime, ART and Bionic shim
were reused; these results do not establish a complete fresh-source build.

### Exact expired-session reply verification, candidate 43

Candidate 43 restores the preserved FotMob-41 application data whose saved Google ID token had
already expired before launch. Official GMS uses the preserved candidate-42 identity/data; the
microG provider and its grants are unchanged. Neither an account chooser nor a new sign-in is used.
FotMob renders its matches screen and receives five observed silent auth replies with parseable
fingerprints. One of those exact reply tokens matches the provider's current token at verification.
That token passes Google's public-key signature check, issuer, approved FotMob audience, same
account subject/email, issue time after the saved token expired, and validity at both reply and
verification time. The other four reply tokens were not independently signature-verified in this
snapshot; a cache entry can be replaced by a later request.

The opt-in diagnostic hashes the token from the client's actual `tokenDetails.TokenData` response,
using the serializer inspected in the pinned FotMob APK. It does not modify the response or make an
authorization decision. Fingerprints remain private, alongside the existing private logs; repository
evidence contains only counts and validation booleans. Malformed encodings fail to an unavailable
marker, and the new host tests cover truncation, duplicate fields and invalid lengths. Candidate 43
passes Java compilation, framework packaging, boot-image construction and framework preload on the
OH board. A further in-place FotMob restart with fingerprinting disabled produces five silent
replies and no fingerprint markers, without clearing data/cache or selecting an account again.
This closes the narrower exact-reply expiry gap; it does not establish FotMob backend
acceptance, account enrollment, arbitrary OAuth scopes or general Play services compatibility.

See [exact reply evidence](../tests/google-auth/lifecycle41/exact-expiry43-result.json) and
[candidate-43 pins](../tests/google-auth/lifecycle41/artifacts43.json).

## Remaining product boundaries

- Candidate 42 gives official GMS and FotMob separate dedicated HAP UIDs. Packages placed inside
  one host still share its UID. This test does not establish a production installer or hostile-native-code
  containment. Per-app kernel identities, SELinux/data-directory policy and lifecycle supervision
  still need independent acceptance.
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

1. Policy-1 chooser/result handoff, identity-claim repair and the narrow synchronous silent ID-token
   path now pass on the board, including process restart, operator revocation, explicit re-selection
   and unrelated-app denial. Candidate 43 also verifies an exact renewed client-reply token after the carried session token expires.
   Still validate backend acceptance, account removal,
   app sign-out, the account-visibility revocation hook, and concurrent/caller-death behavior during
   silent requests. OAuth access tokens, server codes and asynchronous APIs need separate contracts
   and tests. Initial profile UI or a cryptographically valid Google token alone is insufficient.
2. Keep the policy-3 identity as the product-target control. Build 33 demonstrates the narrow identity
   flow with explicit local service exceptions while preserving certification status and identifiers.
   Validate other services separately, fresh account enrollment, later Google flag updates and GMS upgrades.
   Candidate 41 passes three in-place FotMob restarts. Candidate 42 adds a development host-window
   bridge with successful sign-in and cancellation; production lifecycle/isolation acceptance remains separate.
   The full GrapheneOS-like product target is not yet accepted.
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

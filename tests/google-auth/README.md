# Official Google authentication probe

The active [full application validation](../../docs/google-two-path-full-validation.md)
tracks the remaining Google and Facebook flows from the live batch. Candidates
48 and 51 add legacy sign-in service 91 and its internal service 44 dependency.
Spotify and Wattpad now receive successful Google activity returns. Spotify stops
at separate signup. Candidate 52 also delivers Wattpad's subsequent OAuth access
token; Wattpad reaches its separate signup form. This required manual recovery
from an older saved Google account choice. Wattpad subsequently reuses that
authorization after restarting GMS and the client. Candidate 54 preserves ART's
inherited signal blocks in the native OpenHarmony watchdog after tracing a
SIGPIPE startup exit there. Its controlled board test and live startup/auth check
pass. Neither app has a validated application account session or full application
pass. See the linked validation report for the exact evidence boundaries.
Historical results below retain their original build boundaries.

Candidate 72 fixes native ABI classification for GMS libraries loaded through
verified APK descriptors. FotMob initializes Cronet 129 and retains its local
Facebook signed-in menu after a client restart. Cloud synchronization and token
renewal were unproven in that candidate. See [candidate72.json](full48/candidate72.json).
Candidate 73 now observes an in-app favorite change reaching FotMob's server,
then restores both favorites after clearing their local files and app cache
while retaining the Facebook login. The dataset GET also succeeds with an
invalid token, so its status alone does not prove credential validation.
Google delegation and token renewal are not retested by this Facebook flow.
See [candidate73.json](full48/candidate73.json).

Candidate 74 validates FotMob's Google cloud favorite round trip and silent
replacement-token delivery after its saved login token expired. A client-side
reply fingerprint matches a fresh, unexpired ID token verified with Google's
signing keys. The app restores both cloud favorites after local cache removal.
The provider account store changes during renewal; certification stays 3,
check-in identity and grants stay unchanged. This is feature validation, not a
full application or production-isolation pass. See [candidate74.json](full48/candidate74.json).
Candidate 75 adds fresh FotMob Google app sign-in through the official chooser,
verification of the token saved by the app, and another successful client
restart. Existing Google account authorization is reused; no new microG account
registration or separate app signup is performed. See [candidate75.json](full48/candidate75.json).
Candidate 76 completes fresh Facebook login and restart on the same repaired
runtime. A read-only Facebook Graph identity check accepts the original app's
new token and returns the matching account; an invalid-token control is rejected
before and after restart. This independently validates that endpoint's token
acceptance, while Facebook renewal and full application acceptance remain open.
See [candidate76.json](full48/candidate76.json).

Candidate 71 fixes bounded parsing of Gmail's native 14-scope OAuth request.
The board now accepts the request into mediation while continuing to deny
ungranted token access. No Gmail OAuth grant or mailbox acceptance is claimed;
see [candidate71.json](full48/candidate71.json) and the full validation report.
Candidate 77 applies the operator-approved Gmail provider scopes and fixes the
Contacts-client status-9 dialog by adding People (5) and Feedback (29) to the
existing opt-in service eligibility policy. Two fresh Gmail processes reach
IPeopleService and display no incompatible-device dialog. Native account-scoped
token access and mailbox sync remain unvalidated; certification stays 3.
See [candidate77.json](full48/candidate77.json).

Candidate 59 separately grants the verified official GMS broker access to the
existing account's Google ID. Drive receives account-ID replies after the GMS
and microG restart, while its OAuth requests remain denied and its file list
does not load. This metadata result is not an OAuth or application acceptance
pass. See [candidate59.json](full48/candidate59.json) for the exact boundaries.
The [Wattpad follow-up](full48/candidate59-followup.json) after this provider
restart still returns sign-in-required before token issuance, including after
manual cached-choice recovery. The earlier Wattpad successes are historical
results, not current full-stack restart acceptance.

This probe tests the official GMS token implementation and a separate direct
microG positive control. It does not fake a result or claim full OAuth login.
See [board findings](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-official-auth-validation.md) and
[validation.json](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/tests/google-auth/validation.json).

Latest lifecycle tests and artifact pins are under [lifecycle41](lifecycle41/), with the
[restart and host-window report](../../docs/google-two-path-continuation.md#in-place-restart-and-host-window-continuation-candidates-4142).
The underlying [restricted-identity service eligibility, builds 30–33](../../docs/google-two-path-continuation.md#restricted-identity-local-service-eligibility-builds-3033)
has results under `restricted-policy30/`. Build 33 adds only identity services
212 and 213 to the pinned GMS build's local exception list, keeps policy 3 and
its check-in identity, and exercises the real FotMob account chooser and silent
token requests. This is an explicit compatibility policy, not certification.
Earlier [silent ID-token authorization, builds 28–29](../../docs/google-two-path-continuation.md#continuing-id-token-authorization-builds-2829)
is preserved in `silent-session29/`, and [delegation validation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-delegation-validation.md)
is preserved in `delegation-validation/`. The build-04 setup below describes the
historical baseline; use the exact artifacts and flags in each later provenance record.

The subsequent [26-app service retest](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-service-retest.md)
uses the same build 11 and adds an authorized FotMob sign-in attempt. Its
`service-retest11/` evidence keeps Google connection results separate from
visible app behavior and full feature acceptance.

The current source includes nine buttons. The `spoof-package` button calls the official
token Binder with `com.spotify.music` in both package options while retaining
the probe's actual APK, signer and verified channel identity. This is a negative
identity test, not a Spotify login or a delegated token grant. The manifest
input key for the earlier eight-button artifact is `google-official-auth-probe-v5`;
that pinned artifact is not changed by the candidate-78 test build. Earlier build
records below retain their original v1/v2 identities.

The `delegation-ui`, `delegation-provider` and `delegation-native-provider`
buttons attempt wire operations 24, 23 and 28 while retaining the probe's real
APK identity. Each must return
`SecurityException`; a missing channel or unsupported transaction is not a pass.
These are denial tests, not delegated positive controls. Actual Spotify supplied
the positive interactive flow. Build 11 returned `RESULT_OK` and reached its
new-account form, where the test stopped without signup.

The `silent-ungranted` and `silent-spoof` buttons address the new silent ID-token
boundary using the probe's actual verified signer. Supply a private test file
`files/silent-auth-test.properties` with `client_id` and `claimed_package` for an
existing authorized app. These are identifiers, not credentials. The first test
uses the probe's real package with the other app's client ID; the second also
claims the other app in the request extras. Neither may receive a token. Require
the expected bound service and a completed response or explicit denial; a missing
account, configuration, process or channel is not a pass. The response check covers
both `authtoken` and `tokenDetails.TokenData`.

## Build

Prerequisites: Android SDK platform/build-tools 34, JDK 21, Python 3, the exact
Maven artifacts in `sdk-dependencies.json`, and the throwaway development
signing key from the earlier bootstrap probe. Do not use an account password
as a signing password. The development key is not in git.

Download each `url` in the lock into a local dependency directory under its
`file` name. `tools/build_google_auth_probe.py` verifies every artifact hash
before consuming it. It builds resources from the original AARs, generates
their R classes, compiles the Java probe, runs D8, and aligns/signs/verifies the
APK. It requires a fresh output directory:

```sh
python3 tools/build_google_auth_probe.py \
  --sdk /path/to/android-sdk \
  --jdk /path/to/jdk-21 \
  --dependencies /path/to/downloaded-sdk-artifacts \
  --signing-key /path/to/earlier-probe-debug.p12 \
  --out /path/to/new-build-directory
```

The historical tested APK hash assumes the exact SDK/toolchain, source and
signing key in the build record. A newly signed APK needs its own manifest
input hash and an explicitly authorized provider certificate grant. Do not
silently replace an existing client's signer grant.

## Board setup

Use the [two-path fixture](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-two-path-validation.md) with live
microG and official GMS hosts, the private-log native runtime, and framework
build 04. Launch `org.westlake.googlebootstrap.probe.OfficialAuthActivity` from
the new APK through manifest `probe_source_app.py`. The manifest input key for
the three-button version is `google-official-auth-probe-v2`.

Use `--touch-mode isolated` and these runtime settings, with the actual verified
provider endpoint/spawner and APK pin:

```text
WESTLAKE_GOOGLE_PRIVATE_LOGS=1
WESTLAKE_CHANNEL_IDENTITY=enforce
WESTLAKE_GOOGLE_AUTH_CONNECT=<microG auth endpoint>
WESTLAKE_GOOGLE_AUTH_SPAWNER=<verified microG spawner socket>
WESTLAKE_GOOGLE_AUTH_APK_SHA256=<pinned microG APK hash>
WESTLAKE_PLAY_SERVICES_CONNECT=<official GMS endpoint>
```

The existing probe grant permits only `ac2dm` for the matching package/signer.
The account list is supplied by the microG proxy so that both token operations
address the same existing account without recording its value. No new account
or separate OAuth client registration is created by this probe.

Run the buttons sequentially:

1. **Control:** `AccountManager.getAuthToken` with `westlake.forceRefresh=true`.
   This deliberately bypasses the official GMS implementation.
2. **SDK:** Google's actual `GoogleAuthUtil.getToken(context, account, "ac2dm",
   extras)` implementation.
3. **Binder:** explicitly bind official GMS `com.google.android.gms.auth.GetToken`,
   check `com.google.android.auth.IAuthManagerService`, and invoke transaction 5
   with the `Account`, scope and package options.

Reports are private files named `official-auth-<mode>-<timestamp>.json` in the
probe's files directory. Require a start timestamp newer than the button
action; a stale report is not a result. Compare provider request IDs and event
timestamps, including GMS's persistent process. The probe only writes token
presence, counts, numeric IDs, class names and allowlisted public error codes;
it never writes account/token values or arbitrary exception messages.

For cache-boundary diagnosis, enable `WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE=1`
with `WESTLAKE_GOOGLE_PRIVATE_LOGS=1` on the official GMS host. The adapter emits
`[WL-AUTH-BOUNDARY]` records for withheld credential/cache reads. Each record
contains fixed cache/metadata categories, Binder PID/UID, whether a SocketBinder
peer is active, and class/method stack frames. Raw account names, cache keys,
OAuth client IDs, scopes, tokens and metadata values are omitted. Records are
deduplicated and capped at 512 per process; absence of a duplicate line is not
absence of a repeated call. Classification is diagnostic and never authorizes
a request. A separate host Java test exercises redaction with synthetic private
strings in malformed keys and OAuth parameters.

For exact silent ID-token reply correlation, additionally enable
`WESTLAKE_GOOGLE_TOKEN_FINGERPRINT=1` on the test client, with
`WESTLAKE_PLAY_TRACE=1`, `WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE=1` and
`WESTLAKE_GOOGLE_PRIVATE_LOGS=1`. This diagnostic defaults off. It hashes the
token in `tokenDetails.TokenData` after a completed official auth transaction;
it leaves the original reply position and contents intact. The serializer was
checked in the pinned FotMob APK: SafeParcelable object 20293, string field 2.
Unknown classes, non-JWT strings and malformed encodings produce only an
unavailable marker. No credential or claim value is printed. Fingerprints can
correlate private sessions, so keep them in private logs too.

An expiry test must establish that the carried client session's token has already
expired before launch, then match a client reply fingerprint against a privately
held provider token. Verify that exact token against Google's public signing keys,
the approved audience, issuer, account subject, issue time and expiry. A token merely
present in the provider cache is insufficient. Publish only validation booleans and
counts, and report application backend acceptance separately. The host trace test
also covers fingerprint encoding, truncation, duplicate fields and malformed lengths.

A control success with an SDK failure is an experiment result, not a test-suite
pass for the hybrid. A recovery Intent's presence does not prove its activity
can be launched. Do not publish unrestricted logs or follow recovery/signup
flows beyond the user's authorization.

## Preserved run

`results/control.json`, `sdk.json` and `binder.json` came from one v2 client
launch. `first-sdk.json` and `first-binder.json` record an earlier two-button
build; they are corroborating history, not artifacts built from the current
three-button Java source. The v2 build record pins the current sources.

Spotify was tested separately using its existing signed corpus APK and official
GMS/Store connections, with no direct microG auth connection. The reviewed
public error screenshot contains no account data. The source-confirmed
activity/result transport gap and the measured official token failure remain
separate findings; no unpublished Google status code is inferred for Spotify.

The subsequent [Spotify diagnostic replay](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/spotify-google-signin-diagnosis.md)
records the actual numeric Google status 17 after external activity start error
-91. Its evidence lives in `spotify-diagnosis/`, separately from the initial
run. Enable `WESTLAKE_GOOGLE_SIGNIN_DIAGNOSTICS=1` together with private logs
in a rebuilt adapter to reproduce the value-free activity diagnostics; this
flag does not implement activity/result transport or change authentication.

The current adapter also traces numeric legacy `ISignInCallbacks` (transaction
101) and modern `IBeginSignInCallback` (transaction 1) statuses under that flag,
`WESTLAKE_GOOGLE_PRIVATE_LOGS=1` and `WESTLAKE_PLAY_TRACE=1`. It records only
status and account/result presence, skips account payloads, and restores the
incoming Parcel position. Malformed replies produce an unreadable marker.
These statuses describe the sign-in operation; a successful service-broker
connection alone does not establish sign-in success.

## Silent-token diagnosis, build 27

[`refresh-diagnosis27/fotmob-policy1.json`](refresh-diagnosis27/fotmob-policy1.json)
records a successful Google activity result followed 477 ms later by an actual
`BadAuthentication` reply to the original signed FotMob client. The broker had
already lost its active selection; the corresponding GMS credential read was
withheld. This result diagnoses the existing interactive-only authorization
boundary. It does not implement silent authorization or accept the policy-3
identity. See [the lifecycle analysis](../../docs/google-two-path-continuation.md#silent-token-failure-traced-diagnosis-build-27).

For the value-free reply trace, rebuild and enable
`WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE=1`, `WESTLAKE_GOOGLE_PRIVATE_LOGS=1`
and `WESTLAKE_PLAY_TRACE=1` on the client. Enable the same diagnostics on official
GMS with its existing delegation/identity guards. `[WL-AUTH-REPLY]` decodes only
transaction 5 of `com.google.android.auth.IAuthManagerService`, restores the
Parcel position, and records allowlisted errors and field-presence booleans.
Unknown response text is redacted. Boundary classifications are deduplicated;
their absence on a repeated request is not evidence that the method was skipped.

The general Play trace may contain private data. Keep raw traces, screenshots,
account data and proprietary disassembly outside the repository. The checked-in
record contains only reviewed metadata, relative timing and artifact hashes.

## Silent ID-token authorization, builds 28–29

The original signed FotMob APK received six silent token replies after its picker
finished on build 28. Restarting official GMS and FotMob on build 29 produced five
more successful replies without another sign-in. An operator generation change
then denied five requests; explicitly selecting the account again restored six
successful replies. The warning followed this transition: absent, present after
revocation, absent after re-selection. See [the results and limitations](../../docs/google-two-path-continuation.md#continuing-id-token-authorization-builds-2829).

Enable `WESTLAKE_GOOGLE_SILENT_AUTH=1` only in the official-GMS host alongside
`WESTLAKE_GOOGLE_AUTH_DELEGATION=1`, `WESTLAKE_CHANNEL_IDENTITY=enforce` and
`WESTLAKE_GOOGLE_PRIVATE_LOGS=1`, keeping the existing provider identity pins and
operator grants. The feature is off by default. Use build 29's adapter in GMS and
the client; the microG provider remains unchanged. Preserve package data when
restarting processes, including `westlake-auth-sessions` in the official-GMS data
directory. Its records contain selection bindings, not account names or tokens.

The [negative probe record](silent-session29/negative-probe29.json) covers all four
denial cases. For `silent-spoof`, the completed `SecurityException` is corroborated
by the GMS auth-scope denial trace: the probe's generic exception handler does not
retain the earlier descriptor-check fields. Never count an unbound or timed-out
request as an authorization denial. The [provenance](silent-session29/provenance.json)
pins the current probe source/APK and manifest input `google-official-auth-probe-v5`.

This run validates the synchronous audience ID-token path on policy 1. Backend
acceptance, actual token expiry, app sign-out/account removal, the visibility-API
revocation hook, silent-request caller death/concurrency remain open. Policy 3 is tested separately below.
The revocation test was an operator generation update, not an app logout test.

## Restricted identity, builds 30–33

Use the provenance in `restricted-policy30/provenance33.json` for official GMS
and the client. The microG provider remains unchanged. Preserve the original GMS
package data, including its check-in identity, Gservices state, Phenotype preferences
and the `westlake-identity-service-compat` rollback journal. Enable
`WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT=1` only in the official-GMS host with the
existing guarded delegation, identity-claims and silent-auth features. It adds
212 and 213 to one local service exception list; it never changes certification
status. Disable it and restart to restore the previous value when still owned
by the adaptation. The [broker setup](../../docs/google-auth-broker.md#switches)
describes version pinning and failure behavior.

Run the original signed FotMob APK against that restricted host and its live Store.
Record the identity-service callbacks, actual activity result, subsequent client
token replies and matching broker/provider events. Verify token claims and signature
locally using Google's public keys, without sending the token to a verification
service. Keep account screenshots and raw logs private. The four v5 negative probe
cases use the same existing grants and signer as build 29. Their exact outcomes
are in `restricted-policy30/negative-probe33.json`.

This fixture required manual foreground transfer between the Play host and source
host. It validates the authentication path with that intervention, not automatic
window handoff or per-APK isolation. A valid token and an absent reauthentication
warning do not establish FotMob backend acceptance or behavior after actual expiry.
The preserved-data redeployment succeeded without another selection; direct in-place
restarts aborted in native WebView/rendering and remain unaccepted. See both records
in `restricted-policy30/` rather than treating redeployment as proof of restart stability.

## Host tests

The broker's policies and file handling are plain Java. From the repository root:

```sh
out=$(mktemp -d)
javac -d $out/policy framework/core/java/GoogleAuthPolicy.java tests/google-auth/host/GoogleAuthPolicyTest.java
java -cp $out/policy adapter.core.GoogleAuthPolicyTest                         # GOOGLE_AUTH_POLICY_PASS 16
javac -d $out/delegation framework/core/java/GoogleAuthDelegationPolicy.java tests/google-auth/host/GoogleAuthDelegationPolicyTest.java
java -cp $out/delegation adapter.core.GoogleAuthDelegationPolicyTest           # ..._PASS 50
javac -d $out/delegation framework/core/java/GoogleAuthDelegationPolicy.java framework/core/java/GoogleIdentityClaims.java tests/google-auth/host/GoogleIdentityClaimsTest.java
java -cp $out/delegation adapter.core.GoogleIdentityClaimsTest                 # ..._PASS 17
javac -d $out/trace framework/core/java/GoogleAuthBoundaryTrace.java tests/google-auth/host/GoogleAuthBoundaryTraceTest.java
java -cp $out/trace GoogleAuthBoundaryTraceTest                                # ..._TEST_PASS
javac -d $out/sessions framework/core/java/PrivateFiles.java framework/core/java/GoogleAuthDelegationPolicy.java framework/core/java/GoogleAuthSessions.java tests/google-auth/host/GoogleAuthSessionsTest.java
java -cp $out/sessions adapter.core.GoogleAuthSessionsTest                     # GOOGLE_AUTH_SESSIONS_PASS 23
javac -cp $out/sessions -d $out/sessions tests/google-auth/host/GoogleAuthOAuthSessionsTest.java
java -cp $out/sessions adapter.core.GoogleAuthOAuthSessionsTest                # GOOGLE_AUTH_OAUTH_SESSIONS_PASS 50
javac -d $out/identity-services framework/core/java/GoogleIdentityServicePolicy.java tests/google-auth/host/GoogleIdentityServicePolicyTest.java
java -cp $out/identity-services adapter.core.GoogleIdentityServicePolicyTest   # GOOGLE_IDENTITY_SERVICE_POLICY_PASS 20
javac -d $out/signin-trace framework/core/java/GoogleSignInDiagnostics.java tests/google-auth/host/GoogleSignInDiagnosticsTest.java
java -cp $out/signin-trace adapter.core.GoogleSignInDiagnosticsTest            # GOOGLE_SIGNIN_DIAGNOSTICS_PASS 14
javac -d $out/signin-options framework/core/java/GoogleSignInOptionsReader.java tests/google-auth/host/GoogleSignInOptionsReaderTest.java
java -cp $out/signin-options adapter.core.GoogleSignInOptionsReaderTest        # GOOGLE_SIGNIN_OPTIONS_PASS 21
javac -cp $out/sessions -d $out/sessions framework/core/java/GoogleAsyncAuthLease.java tests/google-auth/host/GoogleAsyncAuthLeaseTest.java
java -cp $out/sessions adapter.core.GoogleAsyncAuthLeaseTest                   # GOOGLE_ASYNC_AUTH_LEASE_PASS 19
javac -d $out/sync framework/core/java/SyncDispatchState.java tests/google-auth/host/SyncDispatchStateTest.java
java -cp $out/sync adapter.core.SyncDispatchStateTest                         # SYNC_DISPATCH_STATE_PASS 14
javac -d $out/files framework/core/java/PrivateFiles.java tests/google-auth/host/PrivateFilesTest.java
java -cp $out/files adapter.core.PrivateFilesTest                              # PRIVATE_FILES_PASS 6
```

## App-owned sync dispatcher, candidate 64

`WESTLAKE_LOCAL_SYNC=1` enables one-off sync execution in the client process.
It is off by default. The dispatcher discovers the client's declared
`android.content.SyncAdapter` services, reads their metadata, binds the selected
service through the existing service manager and invokes `ISyncAdapter.startSync`.
The account must remain visible to that client. Existing token authorization
still applies independently; enabling sync adds no account or OAuth grants.

Initialization and actual sync use separate callback tickets. The host test above
covers duplicate, stale, foreign and concurrent callback tickets, cancellation,
and closure. It does not exercise Android binding, device cancellation or Google
backend acceptance. A successful adapter callback alone is insufficient to pass
the Gmail mailbox test.

This is a process-local dispatcher for immediate requests. It persists the
client's sync settings, limits the queue, serializes work for each service and
tracks real callbacks, failures and cancellation. Periodic and delayed scheduling,
durable jobs, retry/backoff, system-wide cross-process state, and destruction of
services retained by the existing binding cache remain unimplemented. Cancellation
has a deadline but cannot forcibly stop app code that ignores it. Keep the feature
opt-in until those lifecycle and scheduling gaps are addressed.

## Private input diagnostics, candidate 65

In the client, set `WESTLAKE_INPUT_DIAGNOSTICS=1` together with
`WESTLAKE_GOOGLE_PRIVATE_LOGS=1` to trace at most 128 input dispatches and 16
window roots per dispatch. `[WL-INPUT]` covers both the receiver bridge and
`OHTouchInjector`, which the deployed native `i` command prefers despite its
receiver-tap label. The trace records main-thread delivery, handled/unhandled
results, exception types/stack frames and structural window state. It omits
entered text, key codes, coordinates, titles, view text and exception messages.

A handled touch is not a completed click listener or an accepted backend action.
Keep the trace disabled for ordinary use; the candidate-65 record separates
input delivery from the still-unvalidated Gmail click and sync work.

Candidate 67 adds `WESTLAKE_INPUT_QUEUE_TRACE=1` to sample at most eight releases,
with up to 32 queued messages per snapshot and bounded structural hit candidates.
It reads message target/callback classes, not payloads. Optionally set
`WESTLAKE_INPUT_METHOD_TRACE_PRESS=2` or `WESTLAKE_INPUT_METHOD_TRACE_RELEASE=2`
to instrument the selected press or release once per process. The ART trace uses
a private application directory and requests a stop after three seconds; actual
shutdown can be delayed and execution timing is perturbed. Keep the raw trace
private and do not treat it as a performance measurement.

This exposed empty native `MotionEvent` transforms beneath Gmail's translated
content view. The fixed native implementation passes 40 host checks and board
preload. The same sync tap now runs the real button listener and internal sync
entry; mailbox sync remains incomplete. See [candidate67.json](full48/candidate67.json)
and the [native input test instructions](../parity/native/README-input-transform.md).

Candidate 68 adds an opt-in native Android log capture for local debugging:
`WESTLAKE_GOOGLE_PRIVATE_LOGS=1` plus `WESTLAKE_ANDROID_LOG_CAPTURE_DIR` naming
a pre-created app-owned 0700 directory. It writes only `android.log` (0600),
refuses shared/symlink/hardlink sinks, caps the file at 8 MiB and drops contended
writes. Treat the raw file as credential-bearing test data; do not commit it.
Without this opt-in, private-mode Android logging stays muted.

Host verification:

```sh
c++ -std=c++17 -pthread -Wall -Wextra -Werror \
  -I framework/android-runtime/include \
  tests/parity/native/private_log_capture_test.cpp -o /tmp/private-log-test
/tmp/private-log-test
```

See [candidate68.json](full48/candidate68.json) for the Gmail loading/version
failures found with this capture; it is not a full Gmail pass.

Candidate 70 adds optional verified public GMS APK descriptors. The client
launcher sets `WESTLAKE_PLAY_CODE_SPAWNER` to the official GMS root spawner and
`WESTLAKE_PLAY_CODE_APK_SHA256` to that installation's verified base APK hash.
These pins are separate from the microG authentication-provider pins. The
connected socket peer must be vouched for by the selected root spawner. Only
public APK paths are mapped; private GMS data and native-library paths retain
their existing boundaries. A code verification failure is cached until client
restart, and a changed code set on reconnect also requires a client restart.

The board's positive/wrong-hash/restored-pin controls and remaining Gmail/GMS
version gaps are recorded in [candidate70.json](full48/candidate70.json).

Descriptor-backed native-library ABI classification is covered by:

```sh
cc -std=c11 -Wall -Wextra -Werror -I framework/webview-shim \
  tests/parity/native/bionic_apk_path_test.c -o /tmp/bionic-apk-path-test
/tmp/bionic-apk-path-test
```

This test uses real `/proc/self/fd` links, including closure and descriptor reuse.
It checks ABI routing only; package-code trust remains the separate verified
descriptor protocol.

Candidate 78 adds explicit operator consent for native account OAuth, separate
from saved Google Sign-In selections. Its policy test is
`tests/google-auth/host/GoogleNativeAuthConsentTest.java` (66 checks). The real
Gmail absent-consent / approved / withdrawn controls and remaining Cronet DNS
failure are recorded in the [full validation report](../../docs/google-two-path-full-validation.md#candidate-78-explicit-native-account-consent-and-gmail-token-delivery).
Token delivery is an intermediate result; mailbox sync and production consent UI
remain incomplete.

Candidate 79 repairs the native networking selector for APK-descriptor callers.
Its real resolver/release test passes 24 checks on host and OH; the old source
fails the APK layout case. Gmail's DNS lookups succeed and independent read-only
Gmail API controls accept its native OAuth token (200) and reject an invalid
one (401). After enabling Sync Gmail through the app's settings, its inbox
loads after restart and an existing message body renders. Sustained sync,
incremental refresh and periodic scheduling remain incomplete. See
[candidate79.json](full48/candidate79.json).

Candidate 80 adds persisted app-owned periodic sync and an opt-in same-process
source adaptation for the standard Android adapter. The local fixture completes
normal and periodic sync callbacks and passes 22 device checks across restart;
26 timing, 14 dispatch-state and 28 source-guard checks pass on the host. Gmail's
daily registration succeeds on the new framework. Stopped-app wake and multiple
elapsed cycles remain open. See [candidate80.json](full48/candidate80.json).

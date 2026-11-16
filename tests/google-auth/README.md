# Official Google authentication probe

This probe tests the official GMS token implementation and a separate direct
microG positive control. It does not fake a result or claim full OAuth login.
See [board findings](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-official-auth-validation.md) and
[validation.json](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/tests/google-auth/validation.json).

Latest run: [silent ID-token authorization, builds 28–29](../../docs/google-two-path-continuation.md#continuing-id-token-authorization-builds-2829),
with results under `silent-session29/`. Earlier [delegation validation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-delegation-validation.md)
is preserved under `delegation-validation/`. The build-04 setup below describes
the historical baseline; current runs use the pinned adapter in their provenance
record with the additional feature and trace flags described below.

The subsequent [26-app service retest](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-service-retest.md)
uses the same build 11 and adds an authorized FotMob sign-in attempt. Its
`service-retest11/` evidence keeps Google connection results separate from
visible app behavior and full feature acceptance.

The current source includes eight buttons. The `spoof-package` button calls the official
token Binder with `com.spotify.music` in both package options while retaining
the probe's actual APK, signer and verified channel identity. This is a negative
identity test, not a Spotify login or a delegated token grant. The manifest
input key for this source is `google-official-auth-probe-v5`; earlier build
records below retain their original v1/v2 identities.

The `delegation-ui` and `delegation-provider` buttons attempt the new wire
operations while retaining the probe's real APK identity. Both must return
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
revocation hook, silent-request caller death/concurrency and policy 3 remain open.
The revocation test was an operator generation update, not an app logout test.

## Host tests

The broker's policies and file handling are plain Java. From the repository root:

```sh
out=$(mktemp -d)
javac -d $out/policy framework/core/java/GoogleAuthPolicy.java tests/google-auth/host/GoogleAuthPolicyTest.java
java -cp $out/policy adapter.core.GoogleAuthPolicyTest                         # GOOGLE_AUTH_POLICY_PASS 8
javac -d $out/delegation framework/core/java/GoogleAuthDelegationPolicy.java tests/google-auth/host/GoogleAuthDelegationPolicyTest.java
java -cp $out/delegation adapter.core.GoogleAuthDelegationPolicyTest           # ..._PASS 36
javac -d $out/delegation framework/core/java/GoogleAuthDelegationPolicy.java framework/core/java/GoogleIdentityClaims.java tests/google-auth/host/GoogleIdentityClaimsTest.java
java -cp $out/delegation adapter.core.GoogleIdentityClaimsTest                 # ..._PASS 17
javac -d $out/trace framework/core/java/GoogleAuthBoundaryTrace.java tests/google-auth/host/GoogleAuthBoundaryTraceTest.java
java -cp $out/trace GoogleAuthBoundaryTraceTest                                # ..._TEST_PASS
javac -d $out/sessions framework/core/java/PrivateFiles.java framework/core/java/GoogleAuthDelegationPolicy.java framework/core/java/GoogleAuthSessions.java tests/google-auth/host/GoogleAuthSessionsTest.java
java -cp $out/sessions adapter.core.GoogleAuthSessionsTest                     # GOOGLE_AUTH_SESSIONS_PASS 23
javac -d $out/files framework/core/java/PrivateFiles.java tests/google-auth/host/PrivateFilesTest.java
java -cp $out/files adapter.core.PrivateFilesTest                              # PRIVATE_FILES_PASS 6
```

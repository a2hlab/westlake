# Official Google authentication probe

This probe tests the official GMS token implementation and a separate direct
microG positive control. It does not fake a result or claim full OAuth login.
See [board findings](../../docs/google-official-auth-validation.md) and
[validation.json](validation.json).

Latest run: [delegation validation](../../docs/google-auth-delegation-validation.md),
with results under `delegation-validation/`. The build-04 setup and preserved
results below describe the historical baseline; the latest diagnostic host uses
the current adapter build with the additional trace flag described below.

The current source includes six buttons. The `spoof-package` button calls the official
token Binder with `com.spotify.music` in both package options while retaining
the probe's actual APK, signer and verified channel identity. This is a negative
identity test, not a Spotify login or a delegated token grant. The manifest
input key for this source is `google-official-auth-probe-v4`; earlier build
records below retain their original v1/v2 identities.

The `delegation-ui` and `delegation-provider` buttons attempt the new wire
operations while retaining the probe's real APK identity. Both must return
`SecurityException`; a missing channel or unsupported transaction is not a pass.
These are denial tests, not delegated positive controls. Actual Spotify supplied
the positive interactive flow. Build 11 returned `RESULT_OK` and reached its
new-account form, where the test stopped without signup.

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

Use the [two-path fixture](../../docs/google-two-path-validation.md) with live
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

The subsequent [Spotify diagnostic replay](../../docs/spotify-google-signin-diagnosis.md)
records the actual numeric Google status 17 after external activity start error
-91. Its evidence lives in `spotify-diagnosis/`, separately from the initial
run. Enable `WESTLAKE_GOOGLE_SIGNIN_DIAGNOSTICS=1` together with private logs
in a rebuilt adapter to reproduce the value-free activity diagnostics; this
flag does not implement activity/result transport or change authentication.

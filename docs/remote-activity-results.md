# Cross-process activity results for the Google sign-in experiment

The subsequent [auth-boundary validation](google-auth-boundary-validation.md)
repeats this flow after a GMS host restart, traces Spotify's OAuth cache reads,
and tests rejection of a probe claiming Spotify's package.

**Board result:** Spotify now opens official GMS's account picker, accepts the
existing authorized account, and passes Google's PendingIntent and
result-forwarding redirects. The original start error `-91` / Google status 17,
the picker visibility crash, and premature redirect cancellation are fixed in
this replay. **Spotify is not logged in:** the flow reaches Google's
"Verify it's you" screen, asking the account to sign in again. The token-only
credential contract remains unresolved.

This change addresses the local launch failure diagnosed in
[Spotify Google login](spotify-google-signin-diagnosis.md). It adds an opt-in
activity/result path between explicitly connected package hosts. It does not
change Google's authentication policy or give official GMS microG's password
or reusable account credential.

## Route and caller identity

Enable `WESTLAKE_REMOTE_ACTIVITIES=1` on both the client and the target host,
with `WESTLAKE_CHANNEL_IDENTITY=enforce`. Existing `WESTLAKE_PLAY_SERVICES_CONNECT`
and the target's matching `WESTLAKE_PLAY_SERVICES_SERVE` choose the endpoint.
The auth-provider namespace remains token-only and does not expose this route.

The package adapter queries the target's parsed Android manifest. The target
must be enabled; foreign callers can resolve only exported activities without
an activity permission. Permission-bearing foreign activities fail closed
until a trusted permission-grant service is implemented. The host rechecks
the target at launch, rejects URI grant flags, and checks that the request was
sent to the manifest's declared process. Caller identity comes from the
channel's existing APK, signer and spawner checks, not the native UID shared
by the experiment's processes or permission claims supplied by the app.

The original Parcelable Intent crosses SocketBinder. The host creates the
activity through AppSchedulerBridge and the stock Android client transaction,
using Westlake's existing OH window adapter. `getCallingActivity`,
`getCallingPackage`, launched-from identity and referrer describe the verified
caller. The result returns to the original local `resultTo`, `resultWho` and
`requestCode`; those local tokens are not interpreted in the other process.

Each launch has a result Binder and a cancellation Binder. Each is bound to
its connection, and completion is atomic and one-shot. Target death cancels
the caller's request. Caller destruction or connection death closes the
target activity. Each process limits outstanding incoming and outgoing
requests to 32. A windowless host accepts an explicit remote request and its
nested UI while that request remains active.

## Nested PendingIntent handling

The first real Spotify replay reached GMS's exported SignInActivity, then
GMS's GoogleApiActivity. The latter sent a PendingIntent created in GMS's main
process. The old adapter sent it with `Application.startActivity`, losing
the result request and trying to display the screen in a windowless process.

For this experimental mode, a single-activity PendingIntent can be prepared
by its creator and executed by another verified process of the **same app**
when that is the target activity's declared process. It keeps the local
activity result request, immutable fill-in behavior, one-shot consumption,
cancellation, and the caller's allowed flag changes. URI grant flags cannot
be added through the flag mask.

This operation reveals a stored Intent, so it is restricted more tightly than
ordinary PendingIntent sending: the creator requires a verified same-app
channel, and SocketBinder requires every forwarding hop to be verified as
same-app, including any enclosing inbound call on the thread. Otherwise a
foreign app holding a PendingIntent through an intermediate GMS process could
make the last hop appear same-app. Legitimate GMS-to-GMS forwarding remains
available. Cross-package PendingIntent preparation,
multi-activity stacks and targets in a different process fail closed in this
mode; this is not a complete PendingIntent broker.

Google's redirect screens also use `FLAG_ACTIVITY_FORWARD_RESULT`. The adapter
now transfers the redirect's existing reply destination to the next activity,
so finishing the redirect does not send premature cancellation. This preserves
remote reply ownership, caller identity and referrer across the redirect, and
restores the source's reply destination if an external start fails. Combining
the forward flag with a new result request returns Android's conflict error.
The fixture exercises both a forwarded external entry and a forwarded nested
PendingIntent destination.

## Account-picker visibility metadata

The next replay displayed Google's actual account picker for Spotify. Selecting
the authorized existing account called `AccountManager.getAccountVisibility`,
which the token proxy did not implement. The resulting SecurityException killed
GMS's UI process; the bridge returned cancellation and Spotify received Google
status 8. [Replay record](../tests/remote-activity/results/spotify04-result.json).

The follow-up implements visibility reads, writes and access checks as metadata
separate from token grants. The account must already be known through the
provider's existing grant. The host's own package sees its granted account;
another package starts user-managed not-visible. Changes for another package
require that package to be the verified caller of a live incoming UI request.
Selections are process-local, cleared when the request ends, and do not add
token scopes, delegate token issuance, or permit credential/cache reads.
This limited picker contract is not a durable, system-wide account permission
database or a replacement for production account-access enforcement.

## Final Spotify replay

The final build ran unmodified Spotify `9.1.86.2432` / `146555520` (base SHA-256
`8ae192315399a972623f56c7e9322fd2fb625720f3167137655a1b9c29e202c7`), official
GMS `244034029`, and the existing microG provider concurrently. Spotify was
PID 26968; official GMS's UI was PID 28466 and its persistent process PID 26561.
No direct microG auth connection or token grant was added to Spotify.

| Step | Observed result |
| --- | --- |
| Cross-package Google sign-in activity | Launched in the verified GMS UI process |
| Google's account picker | Rendered; selecting the authorized account changed visibility from 4 to 2 without a token grant |
| Nested PendingIntents | Reached SignInActivity, FilteringRedirectActivity and ControllerLauncherActivity |
| Result forwarding | Three `result_forwarded present=true` events; no cancellation while the recovery UI remained open |
| Token recovery | Reached GetTokenActivity and MinuteMaidActivity; Google's WebView displayed "Verify it's you" and requested sign-in again |
| Explicit Back from recovery | One canceled result reached Spotify; its SDK reported status 12501 after that deliberate cancellation |
| Authenticated Spotify session | Not established; no new account created |

[Replay record](../tests/remote-activity/results/spotify07-result.json),
[Spotify events](../tests/remote-activity/results/spotify07.events.txt),
[GMS UI events](../tests/remote-activity/results/official-ui07.events.txt),
[GMS persistent-process events](../tests/remote-activity/results/official-persistent07.events.txt).
The account-bearing screenshots remain private and are not included.

During this flow, official GMS still called `peekAuthToken`, `getPassword` and
`getUserData`; the proxy withheld those reads. This is consistent with the
independently confirmed [official token-contract gap](google-official-auth-validation.md).
The recovery screen alone does not prove a server rejection, certification
failure, or a successful native GMS token exchange. Entering the credential into
official GMS would change the token-only experiment, so this run used Back to
validate cancellation instead.

Completing this architecture still requires explicit scoped mediation at the
official auth/cache boundary, preserving the original app identity, package and
signer, scope, expiry, invalidation and consent. Account visibility is only
metadata; the activity bridge does not implement that token protocol.

## Validation and limits

The source-only fixture under `tests/remote-activity` uses two throwaway APKs
on a dedicated `wl.remote.fixture...` endpoint. Its host package is deliberately
`com.google.android.gms` to exercise the configured route, but contains no
Google code, credentials or network authentication. Do not run it on the real
Google experiment endpoint or count its results as Google login success.

The fixture checks exported/enabled/permission filtering; launch in `:ui`;
caller identity and referrer; nested Parcelable data; activity results;
same-app nested PendingIntents; immutable and one-shot behavior; denial of a
foreign forwarded stored-Intent read; cancellation; and host-process death.

The final `remote-activity07` board run passed all recorded fixture checks,
including same-app forwarding from `:ui` through the main process to `:worker`
and rejection when the foreign client tries the same operation. The host was
PID 27794, its UI process PID 28804, its worker PID 28913, and client PID 28751.
Both result-forwarding redirects passed. Killing only the verified UI process delivered one
cancellation to the surviving client. The client also received a
SecurityException when attempting the forwarded stored-Intent read.
[Checks](../tests/remote-activity/results/fixture07-checks.json),
[source/build hashes](../tests/remote-activity/results/build07-inputs.json).

A separate build-05 run killed the client while its target waited. The target process
stayed alive and destroyed the waiting activity, validating caller-death
cleanup independently of target-process death.
[Caller-death evidence](../tests/remote-activity/results/caller-death05.json).

Validation also passed the Java build (122 sources / 254 classes), runtime
packaging, boot generation, board framework preload and the six existing
bootstrap report tests. The manifest's `test_app_input.py` has one pre-existing
failure: modified-APK rejection leaves an output directory. The same failure
was reproduced from the unchanged HEAD versions of both the test and
`prepare_app.py`; neither file is changed here.

The route covers the ordinary `startActivity`/`startActivityForResult` path.
Other activity-manager entry points, general Android task/foreground
coordination, physical touch acceptance, complete permission grants and
production UID/SELinux separation are outside this implementation's validated
scope. The existing fixture still shares a native host UID. A successful
activity handoff alone does not prove Google account consent, token exchange,
Spotify backend acceptance, Play certification or full hybrid feasibility.

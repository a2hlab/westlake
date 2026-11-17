# Two-path live application batch 44

This batch is in progress. The merged implementation reproduces FotMob's Google
account selection and follow-up token replies, but the broader checks have found
an uncovered legacy Google sign-in path. No complete application acceptance is
claimed by this batch yet.

## Build and test boundary

- Westlake main: `61209fc78dc0728193167f73a3abb688f2b466a7` (PR 172,
  after dependency PR 171).
- Manifest main: `d0fe141c6002bba64bb6a2a6be010564742a36dd` (PR 69,
  after dependency PR 68). A subsequent local input pin adds Google Drive.
- Framework/native artifact provenance remains
  [candidate 43](../tests/google-auth/lifecycle41/artifacts43.json). The merged
  launcher was deployed for this batch; this is not a clean full-source rebuild.
- Official GMS remains build `244034029`, with recorded uncertified status `3`
  and no certification-status override. The local identity exception adds only
  services `212` and `213`. The microG provider's operator grants were unchanged
  at the batch preservation check.
- Official GMS and the client host have separate HAP UIDs. All sequentially
  tested clients still use the same development client HAP UID. This does not
  establish production isolation between applications.
- Original APKs and splits are checked against exact hashes and signing
  certificates. Raw logs, account data and screen captures stay private.

## Results so far

| Application | Live interaction | Result and limit |
| --- | --- | --- |
| FotMob | Followed a team in the previously signed-in session; launched fresh application data; selected the existing Google account through official GMS UI. | Interactive `RESULT_OK` and follow-up token-details replies were observed. The fresh session entered default onboarding instead of visibly restoring the followed team. Cloud preference round trip is unproven. |
| Spotify | Launched the newer corpus APK, opened Log in, then Google. | The window handoff was accepted, but legacy sign-in service `91` returned status `9`. The result was cancelled and the app displayed “Something went wrong.” No successful account login or playback. |
| Wattpad | Opened the existing-account login page and selected Google. | Service `91` returned status `9`; the app displayed that Play services are unsupported. Service `212` also returned a successful broker handshake, which did not make the login succeed. |
| Wattpad / Facebook | Opened Log in with Facebook. | The Facebook SDK activity and embedded Facebook web login rendered. Candidate 46 fixes input into the Facebook HTML field. The corrected authorized login was accepted by Facebook and reached Wattpad consent. After consent, candidate 46 created a duplicate SDK activity and returned cancellation, leaving a blank window. Candidate 47 fixes direct-launch activity reuse and passes three board lifecycle checks. The real retry delivers `RESULT_OK` through FacebookActivity to Wattpad, which displays Create an Account. Stopped there without completing signup. |
| YouTube, Gmail, Maps, Uber | Original corpus inputs prepared. | Live acceptance flows pending. |
| Google Drive | Downloaded and pinned an original signed APK and 19 splits. | Signature verification includes the rotated Android 33+ key and the older key published in Google's Drive Digital Asset Links file. Live acceptance pending. |

The Google service status attribution uses a process-global `afterService`
diagnostic and is approximate when requests overlap. Status `0` establishes a
broker connection, not token issuance, authorization or backend acceptance.
Token-detail marker counts are observations, not counts of distinct renewed
tokens. Machine-readable observations are in
[live44 evidence](../tests/google-auth/live44/batch-results.json).

## Additional failures and limits

The preflight found the earlier FotMob 43 process had exited with `SIGTRAP`,
while GMS and microG remained alive. This does not invalidate the previously
captured short restart and exact-token checks, but it prevents a long-run
stability claim. The cause is not yet established.

The board initially ran out of storage while staging GMS 44. That launch failed
before app start and was not an authentication failure. Reproducible artifacts
from our inactive test runtimes were retired while preserving application and
account data. GMS 44b and the subsequent client runs then launched successfully.

Location is disabled in the authentication configuration. Maps/Uber location
acceptance needs an explicit location configuration and an observed position;
map rendering alone cannot pass that test. No purchases, bookings, public posts
or separate application-account signups are part of this run.

## What the failures imply

The current local service adaptation explicitly adds `212` and `213` in
`GoogleIdentityServicePolicy`. Spotify and Wattpad exercise the separate legacy
`91` service. Their failures establish a missing path under restricted policy;
they do not show that a token successfully issued by the two-path provider was
rejected by either application's backend.

A follow-up legacy-service change needs its own before/after live comparison,
preservation of status `3`, rollback coverage, and checks of the actual requested
caller, signer and scopes. Wattpad's APK also requests a birthday scope. That is
a static observation; this batch has not reached its consent or token exchange,
and no additional provider grant has been issued.

Facebook testing covers the shared runtime, SDK/WebView login and return flow.
It is a separate acceptance track from Google's two-path token delegation.

## Facebook input investigation

The original helper edited only Android `TextView` fields. Candidate 44 adds
`InputConnection` support, but the first live HTML-field attempt stays empty.
Candidate 45 adds optional diagnostics restricted to editor classes and outcomes;
it reports `AssertionError` during that operation. Inspection of the installed
WebView finds that its input connection checks its own handler's looper before
`beginBatchEdit()` and throws `AssertionError` for another thread.

Candidate 46 creates the connection on the view thread and delivers edits through
`InputConnection.getHandler()`. Both the Wattpad Compose login field and the
Facebook WebView field visibly accept the harmless test marker. The checked
application and system logs contain zero occurrences of that marker. The
helper no longer stores committed TextView contents in `lastTextCommit` and
reports exception classes instead of exception text for the edit operation.
The original native TextView edit path is otherwise unchanged; it was not
separately exercised in this run.

Compilation, boot-image generation and board preload pass. The unchanged
framework outputs were reused from candidate 45 after checking source inputs,
compiler arguments and file hashes; the changed adapter was compiled and packaged
again. Native artifacts remain reused. See [candidate 46 evidence](../tests/google-auth/live44/input-candidate46.json).
This verifies the private test input mailbox. It does not establish physical
keyboard or production OH IME acceptance.

The user supplied a corrected login identifier. Facebook accepted that login and
showed its ordinary Wattpad consent page. After consent, the window stayed blank.
No account identifiers, credentials, cookies, redirect URLs or screenshots are
included in the public evidence.

## Facebook return investigation

The installed Facebook SDK launches its existing `CustomTabMainActivity` with
`CLEAR_TOP | SINGLE_TOP` (`0x24000000`). Android should clear the redirect activity
above it and deliver the redirect through `onNewIntent()`. Westlake's direct
launch instead created another `CustomTabMainActivity`. Its `onCreate()` handles
this unexpected redirect by cancelling, while the original controller still
owns the callback to `FacebookActivity`.

Candidate 47 reads launch flags from the Intent instead of confusing them with
Binder start flags. For a local task with no individual OH ability tokens, it
finds the existing target, destroys activities above it when requested, and
sends a `NewIntentItem` while preserving the original result destination. The
code has no Facebook-specific branch. Cross-task launches, manifest launch modes
and activities with real OH tokens remain outside this local implementation.

A source-only fixture reproduces the failure on candidate 46. Candidate 47
passes all three board checks: clear-top redirect with the original result
recipient, single-top reuse, and ordinary launch creating a new instance. The
pass screen was also observed. See
[activity reuse evidence](../tests/google-auth/live44/activity-reuse-candidate47.json).
The real retry also passes the return path: `CustomTabMainActivity` is reused,
`FacebookActivity` receives `RESULT_OK` with data, and Wattpad
`wp.wattpad.onboarding.MainActivity` receives `RESULT_OK` with data. Wattpad then
displays Create an Account, requesting a username and birthday. No signup
fields were filled and no account was created. This passes Facebook
authentication and SDK-to-application return, but does not establish an existing
Wattpad session, a completed backend account, restart persistence or full
application acceptance.

## Startup failures during the input checks

The first candidate-44 child exited with SIGPIPE before the field test. A single
respawn reached Facebook. Candidate 45 reached the field without a recorded
startup exit. Candidate 46 initially exited with SIGPIPE twice, also before
text entry.

The launcher source contains a child SIGPIPE-ignore policy, and the deployed
binary contains its diagnostic string. It would be incorrect to conclude that
the reused binary simply lacks that fix. A subsequent controlled experiment
sets SIGPIPE ignored in the launching shell before ART starts. The child reports
SIGPIPE ignored immediately after spawn but no longer reports it ignored after
startup. That run reaches the field and login-submission checks, but this does
not establish a fix for the intermittent exit. The reset location and failing
write remain unresolved. The exact signal experiment is recorded separately
from input acceptance; it is not a production launcher rebuild.

Candidate 47 also hit SIGPIPE before reaching login; its reuse handler had not
run. A controlled respawn, without the extra shell signal workaround, reached
login and the successful Facebook return described above. The intermittent
startup exit is still unresolved. Restart persistence and long-run acceptance
remain open checks.

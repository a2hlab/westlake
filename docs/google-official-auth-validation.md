# Official GMS token and Spotify Google Sign-In follow-up

**Spotify root cause is now confirmed:** a [diagnostic replay](spotify-google-signin-diagnosis.md)
recorded no matching OH activity, start error `-91`, and Google `Status(17)`.
The immediate blocker is external Google activity resolution. The initial run
below did not capture that status; its records are preserved as collected.

**Result: the current hybrid does not complete these flows.** On 2026-11-15
Pacific / 2026-11-16 UTC, the official Google SDK and official GMS token Binder
both returned `BadAuthentication`. A forced microG token request from the same
probe succeeded. Spotify's Google login launched its bundled sign-in activity,
then returned to Spotify with an error. No new account was created.

This narrows the earlier [two-path validation](google-two-path-validation.md):
its microG scoped tokens, official Advertising ID and official Store
catalogue/download/install results remain valid. They do **not** demonstrate
general official GMS authentication or third-party Google Sign-In. Full
compatibility is not established, and the currently implemented path fails the
tests below. This is not evidence that Google rejected the architecture at its
servers: the first confirmed token blocker is a local credential contract.

[Machine-readable result](../tests/google-auth/validation.json) ·
[Probe source and reproduction](../tests/google-auth/README.md).

## Board results

| Check | Result | Evidence |
| --- | --- | --- |
| Same probe sees the existing Google account | One account, without recording its name | All three probe reports |
| Direct microG positive control, forced `ac2dm` request | Token returned, request 599 | [Control](../tests/google-auth/results/control.json), [provider events](../tests/google-auth/results/microg.events.txt) |
| Official SDK `GoogleAuthUtil.getToken` | `UserRecoverableAuthException`, `BadAuthentication`, recovery Intent present | [SDK](../tests/google-auth/results/sdk.json) |
| Official `IAuthManagerService.getTokenWithAccount`, transaction 5 | Expected interface descriptor; no token; `BadAuthentication`, recovery Intent present | [Binder](../tests/google-auth/results/binder.json) |
| Repeatability of official token failure | Both SDK and Binder also failed in an earlier independent client launch | [First SDK](../tests/google-auth/results/first-sdk.json), [first Binder](../tests/google-auth/results/first-binder.json) |
| Spotify 9.1.86.2432 Google login | Sign-in hub launched; result code 0; Spotify displayed an error | [App result](../tests/google-auth/results/spotify.json), [structural events](../tests/google-auth/results/spotify.events.json), [public error screen](../tests/google-auth/results/spotify-error.jpeg) |

The SDK probe is source-built with Google's actual
`play-services-auth-base:18.1.0` in a lock of 21 hash-pinned Maven artifacts. It is a
deliberately pinned SDK, not a claim about the latest release. It uses the
existing authorized probe package and development signer, with the same
`ac2dm` grant for the control. The direct Binder test independently reaches the
official GMS service. Neither test is a complete OAuth ID-token/server-code flow
or a test of a newly registered OAuth client.

The control explicitly bypasses the official GMS token implementation. Its
forced-refresh flag and provider events establish the exercised microG code
path; this is not a separate capture of network traffic.

## Confirmed token-contract gap

The comparison ran sequentially in one client against the unchanged framework
build 04 and live microG/official GMS hosts. The relevant timestamps and requests
are retained in the reports:

1. microG received account-list request 598 and issued a token for forced request
   599. Its log records `force_refresh=true`, `token_present=true`.
2. The SDK operation caused account-list request 600. Official GMS's persistent
   process then called `peekAuthToken` and `getPassword`; both were withheld by
   the proxy. The SDK returned `BadAuthentication`.
3. The Binder operation caused account-list request 601, followed by the same
   two withheld reads and the same error. There was no corresponding microG
   token request for either official operation.

[Official GMS events](../tests/google-auth/results/official-gms.events.txt)
show these calls in PID 25885, `com.google.android.gms.persistent`; looking only
at the main GMS process would miss them.

[`GoogleAuthProxy.answer`](../framework/core/java/GoogleAuthProxy.java) forwards
supported `AccountManager.getAuthToken` requests. It deliberately returns null
for `peekAuthToken`, `getPassword` and `getUserData`. Official GMS's native Java
token implementation does not simply call the forwarded `getAuthToken` method
in this run. It first looks for its own cached token, then for its reusable
account credential. Supplying an account name alone is insufficient.

Static inspection of the pinned, unmodified GMS APK supports this runtime
observation: the `tku.a(Account, String, tlv)` path in `classes2.dex` reads
`AccountManager.peekAuthToken`; token metadata includes expiry/scope keys;
`bkkw.c(Account)` in `classes4.dex` wraps `AccountManager.getPassword`, and the
`tic` path consults reusable-credential storage. This describes the inspected
version only. Proprietary disassembly and credential stores are not published.

Returning the reusable credential would change the selected security boundary
and revert to a credential-sharing experiment. Returning arbitrary strings for
cache reads would not implement scope authorization, expiry, invalidation or
original-caller identity. Neither is implemented here.

## Spotify: observed failure and separate UI gap

The user approved Spotify from the existing corpus, with no separate account
signup. The unmodified signed APK is version `9.1.86.2432` / `146555520`, SHA-256
`8ae192315399a972623f56c7e9322fd2fb625720f3167137655a1b9c29e202c7`.
The native compatibility libraries receive the usual Westlake runtime
adaptations; this is not an unmodified Android operating system.

Spotify rendered its login page. The test selected Google using the private
input mailbox; it did not enter a Google password into Spotify. The log shows
Spotify connected to the official GMS and Store channels, and subsequently
launched `com.google.android.gms.auth.api.signin.internal.SignInHubActivity`
**inside Spotify**, as the Google client SDK normally includes that activity
in the calling app. A result with `resultCode=0` and data present reached
`com.spotify.login.loginflowimpl.LoginActivity`. The screen displayed
“Something went wrong. Try again.” No account picker, authenticated Spotify
session or new-account signup was observed.

No direct microG auth connection or new microG client grant was added to
Spotify. The intended route was through official GMS. Service binds alone do
not establish successful FCM, Advertising ID or any other API operation inside
Spotify. The captured app-test interval contains
[no auth-proxy events](../tests/google-auth/results/spotify-window-auth.events.json)
in the inspected microG and official GMS processes.

Source inspection identifies another missing contract:

- [`ActivityTaskManagerAdapter.startOutside`](../framework/activity/java/ActivityTaskManagerAdapter.java)
  resolves external activities through OH abilities. A service channel to GMS
  does not register its Android activities with OH BMS.
- [`PlayServicesChannel.startInHome`](../framework/activity/java/PlayServicesChannel.java)
  handles an app's own process-to-home activity start without a result. It is
  not a cross-package Google sign-in activity/result broker.
- [`ActivityResults`](../framework/activity/java/ActivityResults.java) carries
  results between local activities. The cross-process/cross-package caller
  relationship needed for this sign-in flow is not implemented by that path.

These were initially source-confirmed limitations, consistent with the observed
failure. The initial run did not capture the returned Google status. The later
[instrumented replay](spotify-google-signin-diagnosis.md) captured status 17
following start error -91, confirming activity resolution as the immediate
cause. Spotify did not reach the independent SDK probe's `BadAuthentication`
path. The initial machine-readable records remain unchanged.

Input needed the harness's explicit main-queue click and DOWN/UP injection;
some earlier clicks left the login page visible. The eventual activity and
error screen establish the attempted Google flow, but this is not physical
touch/lifecycle acceptance.

## Implications for the proposed hybrid

Official GMS has broader implementation coverage than microG for many services,
but installing it does not make every account-dependent API usable. Every
selected API still needs its actual token, caller identity, UI, permissions and
lifecycle contracts. GrapheneOS-style compatibility changes do not by
themselves supply those missing Westlake contracts.

| Candidate next step | What it could establish | What it would not establish |
| --- | --- | --- |
| Cross-package activity launch/result transport with verified caller, target process, cancellation and lifecycle | Reach the actual official sign-in UI and return its result | Working official token issuance or server acceptance |
| Explicit scoped token mediation at the official GMS cache/auth boundary | Potentially preserve native official API logic while microG retains the reusable credential | Feasibility until original caller, package/certificate, scope, expiry, consent and invalidation are implemented and tested |
| Route Google auth SDK protocols and interactive auth to microG, keeping selected other services on official GMS | A different hybrid that may complete supported auth flows | That native official GMS authentication works, or that all account-dependent official APIs work |
| Copy the reusable credential into official GMS | A different credential-sharing experiment | The current token-only isolation objective or certification |

For the current hypothesis, implement and test the activity/result contract and
the explicit token mediation contract separately. Do not add a broad delegation
grant or hand GMS the reusable credential merely to obtain a green test.
Any delegated token request must carry a verified original app identity, not
just a package string supplied by GMS or the app. Consent and recovery Intents
must remain bound to the requesting app and user action.

Acceptance should require an existing registered app completing Google login
and reaching its authenticated backend, followed by cancellation, denied
consent, expired-token refresh, account removal/revocation, and provider restart
checks. The token protocol and interactive-flow work are not implemented by
this follow-up. Maps/location, FCM delivery, Billing, Play Integrity, other
corpus apps and long-term server acceptance remain unproven.

## Provenance and limits

The framework remains build 04, runtime DEX JAR SHA-256
`d36741190dea5b7dbc07bfa1471100f657d1bbc524a23ea9f58b655d9f5f523a`.
Official GMS is `244034029`, base SHA-256
`1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1`.
The v2 probe APK is
`5772f59a64c44b61abf337f125dc5e24aaaecc651bebc503f905379ede894535`;
its [build record](../tests/google-auth/results/probe-build.json) pins the
current source and SDK dependency list. `device_tested=false` in that immutable
build record describes the build stage; the separate validation report records
the subsequent board run.

[Process evidence](../tests/google-auth/results/processes.json) records the live
hosts and distinct mount namespaces. They still share a native UID; production
UID/SELinux isolation is not proven. The SDK probe was stopped after testing.
Only value-free probe reports, selected structural events and the reviewed
public error screen are retained here. No credentials, account identifiers,
tokens, cookies or private account screenshots are included.

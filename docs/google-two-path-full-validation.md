# Full two-path application validation

Status: in progress. The goal is to fix and validate every application flow in
the [validation plan](../tests/google-auth/full48/validation-plan.json), including
backend use, renewal and restart. The
[previous batch](google-two-path-live44.md) remains the baseline. A connection,
account picker, or partial login is not a full application pass.

## Candidate 48: legacy Google sign-in eligibility

The restricted official GMS build rejected Spotify and Wattpad's legacy sign-in
service 91 before token delegation could run. The compatibility policy now adds
91 alongside the existing 212/213 identity exceptions. This does not alter the
stored uncertified status, enable every GMS service, grant token scopes, or
replace any server-side integrity verdict. The existing version pin, opt-in
guards and rollback journal remain in use.

The updated policy passes 18 host checks. The adapter, runtime packaging and
boot image build pass, and the board preload completes without class
initialization failures. Native artifacts and verified unchanged framework
outputs are reused. Official GMS is redeployed with carried account data; the
check-in identity and provider-grant hashes are unchanged, status remains 3,
and no certification-status override is present.

In a fresh Wattpad client, service 91 changes from callback status 9 to 0. The
app invokes the legacy sign-in service and receives its callbacks. It still
returns to the login screen without an account chooser or authenticated result.
The connection fix therefore exposes another failure, whose numeric sign-in
callback status is being traced. This is not an application acceptance pass.

Exact build and observed-result evidence is in
[candidate48.json](../tests/google-auth/full48/candidate48.json). Credentials,
account identifiers, tokens, raw logs and screenshots remain private.

## Candidate 50: Wattpad fails after the service connection

The rebuilt client decodes only numeric sign-in callback status and account or
result presence. Its parser passes 14 host checks, the runtime and boot image
builds pass, and board preload has no class initialization failures. The live
client adapter hash matches the build. Official GMS remains on candidate 48;
native artifacts and verified unchanged framework outputs are reused.

With Wattpad's existing app data carried forward, the modern service 212 and
legacy service 91 broker connections both return status 0. The modern
`IBeginSignInCallback` then returns status 16 with no result. The legacy
`ISignInCallbacks` returns status 8 (`INTERNAL_ERROR`) with no account, twice.
Wattpad stays on its existing-account login screen, with no authenticated
activity result. Inspection of its original APK shows that `GoogleManager`
opens the interactive legacy flow for silent result 4; result 8 takes its
failure branch instead.

The internal failure still needs localization. This evidence does not establish
a Google server rejection, a successful delegated token, or a completed login.
Do not convert status 8 into a successful result or bypass account selection to
make the app proceed. The first diagnostic build, candidate 49, assumed a fixed
Binder header and did not decode the callback. Candidate 50 validates the exact
interface token within the bounded header and produces the results above.

The check-in identity and provider grants remain unchanged. The recorded policy
status is still 3 and no certification override is present. See
[candidate50.json](../tests/google-auth/full48/candidate50.json) for exact hashes
and observations. Account sign-in and an uncertified status can coexist; neither
fact by itself establishes broad Westlake application compatibility.

## Candidate 51: legacy sign-in reaches account selection

The pinned Google APK shows that legacy service 91 creates a `SignIn.API`
client for service 44. Adding that dependency to the eligibility policy changes
Wattpad's legacy callback from status 8 to status 4 (`SIGN_IN_REQUIRED`) and
opens Google's real account chooser. The check-in identity and provider grants
were unchanged during this comparison; policy status remains 3 with no
certification override. Twenty policy checks, the rebuilt adapter and boot
image, and board preload pass. Native artifacts are reused.

Selecting the existing test account then reaches the provider's separate app
authorization boundary. Wattpad initially has no grant, so the provider refuses
the origin and GMS falls back to its uncertified-device page. A subsequent
operator grant pins the verified original Wattpad signer and permits only basic
identity scopes. That retry reaches a scope refusal. The original APK requests
`user.birthday.read` during legacy sign-in, while its later `GoogleAuthUtil`
request is `oauth2:openid email profile`. Birth-date permission is pending user
clarification; it has not been added to the grant. No explicit server client ID
is granted, and ID-token requests remain ungranted for this app.

This fixes the observed internal-error stage and confirms the account chooser.
Token delivery, authenticated return, app backend use and renewal remain
incomplete. The uncertified-device screen after a local provider denial is not
evidence that Google rejected a successfully delegated token. See
[candidate51.json](../tests/google-auth/full48/candidate51.json).

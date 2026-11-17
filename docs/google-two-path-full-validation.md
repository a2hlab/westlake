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

### Candidate 51 follow-up: two successful Google activity returns

Spotify reaches Google's account chooser, receives delegated OAuth access tokens
and server authorization codes, and receives `RESULT_OK`. Its next visible page
is the separate Spotify account-creation form asking for a birth date. Testing
stops there under the user's earlier signup restriction. A Spotify account
session, library, playback, renewal and restart are not yet validated.

The user subsequently authorizes Wattpad's birth-date scope. Only that scope is
added to its existing signer-pinned grant; the other entries are preserved.
After an in-place app restart, the Google flow issues OAuth tokens and returns
`RESULT_OK`. Wattpad then invokes `IAuthManagerService` transaction 5. Its original
APK requests `oauth2:openid email profile`; the current silent boundary accepts
only ID tokens. The official GMS process records `silent_request_denied` and
`silent_read_denied` for Wattpad, and the client receives `BadAuthentication`.
Wattpad remains on its login page. This is a local post-return authorization gap,
not evidence of a Google certification rejection at that step.

Both service hosts remain alive; recorded uncertified status is still 3 and
there is no certification-status override. The successful Google returns show
that this status can coexist with working delegated authentication. They do not
establish full app acceptance or production per-app isolation. Exact observations
are in [candidate51-followup.json](../tests/google-auth/full48/candidate51-followup.json).

## Candidate 52: authorize follow-up OAuth access-token requests

The silent authorization path now accepts ordinary OAuth access tokens after a
successful, verified legacy Google sign-in activity. It stores the scopes issued
during that completed flow, bound to the original package, both signer digests,
selected account hash and revocation epoch. The next request may use a subset
of those scopes. Its live Binder operation and the provider cache request must
still agree on the exact requested scope set; changing scope order is allowed,
but scope aliases are not invented. The provider's separate operator grant and
live witness are checked again. Server authorization codes and opaque options
cannot become reusable grants. No account names or tokens enter the selection
record, and each new selection replaces the previous scope set.

All 23 existing ID-token checks and 44 new OAuth boundary checks pass. The adapter,
runtime packaging, boot image and board preload pass. Official GMS runs the new
adapter with its account data retained; the client remains on candidate 51.
Check-in identity and provider grants are unchanged, policy status remains 3,
and no certification override is present.

The first retry reused Wattpad's SDK-cached Google selection from before this
change. It skipped the chooser and reached a silent-read denial because no new
OAuth selection record existed. The exact app Google-sign-in preference file
was backed up before clearing it for a fresh interactive test; other app data
was retained. Automatic migration or an interactive recovery from this existing
cache state is not yet implemented.

After clearing that SDK cache, the modern callback returns 16 and the legacy
callbacks return 4 with no account. This retry has not produced another remote
Google activity launch or authenticated return; Wattpad remains at login. A
single thread sample shows the UI thread in `MessageQueue.nativePollOnce`,
which does not establish overall UI health. The new OAuth follow-up fix is
therefore **not yet validated on the board**, despite passing its host boundary
tests and preload. See [candidate52.json](../tests/google-auth/full48/candidate52.json).

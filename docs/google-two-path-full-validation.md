# Full two-path application validation

Status: in progress. The goal is to fix and validate every application flow in
the [validation plan](../tests/google-auth/full48/validation-plan.json), including
backend use, renewal and restart. The
[previous batch](google-two-path-live44.md) remains the baseline. A connection,
account picker, or partial login is not a full application pass.

Latest observed result: candidate 59 supplies the Google account ID to the pinned
official GMS broker through a separate, explicitly granted metadata operation.
Drive now receives three successful account-ID replies, then encounters denied
OAuth requests. Its file list remains unvalidated. Earlier, Wattpad reused its
Google authorization after restarting GMS and the client, and candidate 54
fixed its traced native watchdog signal-mask exit. Spotify and Wattpad still
stop at separate signup. No full application pass is claimed.

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

### Candidate 52 follow-up: live OAuth token delivered to Wattpad

Both the official GMS host and Wattpad now run the verified candidate 52 adapter.
With preserved pre-login Wattpad data and a new interactive account selection,
Google returns `RESULT_OK`. GMS records three successful interactive OAuth reads
and saves the signer-bound OAuth selection. Wattpad then invokes its separate
legacy auth request: GMS records `silent_token=true owner=wp.wattpad`, the provider
issues another OAuth token, and the client receives a token-bearing auth reply.
Wattpad advances to **Create an Account**, requesting a username and birthday.
No signup fields are filled and no account is created. This is a live pass for
the previously failing follow-up token path, not a Wattpad account or reading pass.

The saved selection is schema 2, mode `0600`, and contains the verified Wattpad
signer, account hash, revocation epoch and issued scopes `openid`, `email`,
`profile`, and `user.birthday.read`. It contains no account name or token. The
provider's operator grant remains separate. The user had explicitly authorized
birth-date access before this test. The device's recorded policy remains `3`,
there is no certification override, and the check-in identity and provider-grant
hashes are unchanged.

This successful test required manual recovery from earlier test state. Google
skipped its account chooser while retaining Wattpad's previous choice, but
Westlake had no reusable OAuth selection from candidate 51. The delegation layer
recorded `no_selection`; no Wattpad provider token was issued in that attempt,
and Google's UI fell back to its uncertified-device page. A private backup was
made before removing only Wattpad's three entries from
`shared_prefs/common.selected_account_prefs.xml`; nine other entries were
preserved. Restarting only the GMS UI process did not restore the chooser.
After restarting the GMS processes and the client, a subsequent live attempt
displayed the chooser and completed the token path above. Automatic migration
and recovery from this older cached-choice state are still unimplemented.

Startup reliability is a separate open problem. A fresh-data launch initially
remained black; a run with preserved pre-login data reached onboarding after a
long startup. Another client restart exited before login, with its verified
spawner recording signal 13 (`SIGPIPE`). A later unchanged retry reached login.
In that surviving retry, 282 sampled threads blocked SIGPIPE while one
`OS_DfxWatchdog` thread did not; the sampled process disposition was default.
This narrows investigation but does not identify the failing write or prove a
signal fix. A further restart intended to check saved OAuth authorization also
exited with signal 13 before login. OAuth reuse after restart therefore remains
unvalidated; the saved record alone is insufficient evidence. No new native
artifact was deployed in these comparisons.

Exact sanitized observations are in
[candidate52-followup.json](../tests/google-auth/full48/candidate52-followup.json).
Raw logs, account screenshots and the preference backups remain private.

## Candidate 54: retain ART's signal blocks in the OpenHarmony watchdog

A surviving candidate-52 client restart reuses the saved authorization without
clearing preferences or displaying an account chooser. Official GMS records
`silent_token=true owner=wp.wattpad`, the provider returns an OAuth token, and the
client receives a token-bearing reply. Wattpad reaches its separate signup form.
This establishes authorization reuse after a client restart, not an application
account session or expired-token renewal.

Two further unmodified starts terminate with SIGPIPE before authentication. A
signal-only native observer catches one in `OS_DfxWatchdog`. Its stack resolves
to the board's `pthread_sigmask` returning from `rt_sigprocmask`, called by
`OHOS::HiviewDFX::WatchdogInner::Start()`. The actual board library queries the
inherited mask, removes its sampler signal from that set, and uses `SIG_UNBLOCK`
on the remaining set. ART's `Runtime::BlockSignals` deliberately blocks SIGPIPE,
SIGQUIT and SIGUSR1. The observed delivery occurs when the watchdog removes those
blocks. The original signal-generation site has not been identified; this trace
does not establish a failing `write` call or a deliberate watchdog termination.

The shim now preserves these inherited ART blocks only for `SIG_UNBLOCK` from
`libhicollie.z.so` on `OS_DfxWatchdog` in an Android launch with `ASX_APK_PATH`.
Other signals and callers retain their requested semantics. A signal already
unblocked remains unblocked. The change neither changes the process-wide signal
disposition nor disables watchdog tasks.

A controlled child queues its own SIGPIPE and invokes the installed board
library's `SetTimerRust`. The unmodified control dies with signal 13. With the
production adapter header compiled into a minimal test preload, the real
watchdog callback runs: SIGPIPE, SIGQUIT and SIGUSR1 remain blocked, SIGUSR2 is
unblocked, and the sampler signal retains the library's expected block. Without
the Android-launch marker, the same test retains the original signal-13 behavior.
All three expected outcomes pass. The full Android preload requires its normal
launch environment; standalone attempts with both old and new full preloads
produced SIGSEGV before the probe's diagnostic markers and are not counted as
integration passes.

The complete current-branch shim builds. For the live comparison, the earlier
deployed native sources were reconstructed from `02a8f2a` plus the APK-caller
change `2d410bb`; all five rebuilt artifacts match the deployed baseline hashes.
Only the signal-mask fix was then applied to that baseline. This avoids mixing
the branch's separate Android-fidelity loader changes into the comparison.
The fixed shim is deployed to the stopped Wattpad runtime with a verified backup;
the Java adapter remains candidate 52. Official GMS retains its earlier native
artifacts. Exact source, binary and board-library hashes are in
[candidate54.json](../tests/google-auth/full48/candidate54.json).

The fixed live client reaches onboarding without a debugger. Its watchdog emits
the adaptation marker and has mask `0x1204`, retaining the three ART blocks.
After both GMS and the client have restarted, another Google login tap produces
one owner-tagged silent OAuth provider token and a token-bearing client reply.
No account chooser or preference reset is needed. Wattpad again reaches
**Create an Account**, where testing stops without entering signup information.
A second ordinary client restart also reaches onboarding with the adaptation
marker and all three blocks retained. Neither fixed start uses a debugger.
Startup still takes time; this does not establish broad performance or sustained
reliability. Recorded certification status remains 3 with no override, and the
check-in identity and provider grants are unchanged. Account creation, reading,
expiry renewal and production per-app isolation remain incomplete.

The native observer and controlled-library probe are documented in
[native watchdog signal validation](native-watchdog-signal-validation.md).

## Candidates 55–59: Drive account identity and OAuth are separate gates

Drive's original APK starts with its existing account and Files tab, but the
candidate-55/57 runs cannot refresh the file list. Fixed-category diagnostics
on the verified legacy-auth transaction identify five requests for
`^^_account_id_^^`. The delegation parser rejects that service as unsupported.
The other observed requests are OAuth scopes for `supportcontent` and
`notifications`; their request format is accepted, but token reads are denied.
Drive has neither a saved OAuth account-selection record nor a provider scope
grant. These observations do not establish a Google server refusal.

The account-ID request is distinct from an OAuth access token. In the inspected
microG implementation, `AuthManager.requestAuth` handles it by reading the
account's `GoogleUserId` metadata. Westlake previously withheld that field along
with other user data. Candidate 59 adds an explicit `ACCOUNT_ID` operation to
the existing private auth-provider interface, with these boundaries:

- Only `com.google.android.gms`, matching both the configured client certificate
  and broker certificate, can use it. The provider additionally requires
  `client.com.google.android.gms.account_id=true`.
- The official GMS host must opt in with
  `WESTLAKE_GOOGLE_ACCOUNT_ID_METADATA=1`, alongside enforced identity,
  delegation and private logging. Other account metadata retains its existing
  behavior. A missing grant or unavailable provider withholds the ID.
- The operation accepts one exact existing Google account, rejects extra parcel
  data and one-way calls, rechecks the grant and account before replying, and
  returns only a bounded numeric identifier. It creates no OAuth selection,
  adds no OAuth scope and provides no password or reusable credential.
- Diagnostics emit fixed categories and presence booleans. Account identifiers,
  unknown scope values, tokens and request-option values are not logged.

The updated provider policy passes 16 host checks, including absent permission,
wrong signer, wrong broker, a different client with the same certificate,
revocation, and rejection of OAuth access despite the metadata grant. Existing
delegation-policy, ID-session and OAuth-session suites pass 36, 23 and 44 checks;
the diagnostic privacy suite also passes. Adapter packaging, boot-image build
and board preload succeed without class-initialization failures.

Official GMS and microG restart with the candidate-59 adapter in their existing
runtimes. Drive retains its candidate-55 diagnostic adapter and original APK;
its native watchdog fix from candidate 54 remains installed. The provider's
older native runtime is preserved explicitly. No account or application data is
cleared. The only provider-grant addition is the broker account-ID permission.

In the recorded post-restart window, the provider serves 15 account-ID reads to
official GMS, and GMS records 15 successful metadata reads. Three owner-tagged
Drive account-ID requests align with three successful legacy-auth replies
containing `authtoken`/`tokenDetails`. Those fields carry the account-ID result
for this API; their presence alone must not be counted as an OAuth token or a
successful application session. The delegated provider issues no OAuth token
for Drive in this window.

Drive then makes two `peopleapi.readonly`, two `supportcontent`, and one
`notifications` OAuth request. Their formats pass validation, but all five
reads remain denied; the client receives `BadAuthentication` with recovery
intents. Several GMS service connections also retain status 9. No additional
scope or service exception was granted in this test. The observed Files view
still spins; file listing, file opening, renewal and restart acceptance remain
incomplete. Further work must distinguish account permission/recovery from
service eligibility and other local sync errors.

All three hosts remain alive. Certification status stays 3 without an override,
and the official check-in identity is unchanged. The provider still has one
Google account with its ID metadata. Its account-store bytes changed during
restart, so byte-for-byte store preservation is **not** claimed; a fresh OAuth
renewal after this provider restart also remains unvalidated. Shared client host
UIDs still prevent a production per-app isolation claim.

Exact build pins and sanitized results are in
[candidate59.json](../tests/google-auth/full48/candidate59.json). Raw captures
and configuration backups remain private.

### Wattpad follow-up after the candidate-59 provider restart

The subsequent Wattpad retry does not reproduce the earlier candidate-54 OAuth
success. The original APK, candidate-52 client adapter and fixed native watchdog
shim start and reach the login page, but modern sign-in returns status 16 and
legacy sign-in returns status 4 twice, with no account. No OAuth request reaches
the provider in the observed login window. No authenticated activity result or
new Wattpad account is produced. This is a failed follow-up checkpoint, not
evidence that Google rejected a newly issued token.

Read-only comparison finds that Wattpad's SDK cache used the account's email as
its `GoogleSignInAccount.id`. Its stored email matches the provider account, but
that cached ID differs from the numeric `GoogleUserId` now supplied to GMS.
This identifies a migration mismatch without proving it is the sole cause.

Two manual recovery attempts are recorded separately. First, only Wattpad's
Google sign-in preference file is backed up and removed before a client restart.
Second, only its three keys in GMS `common.selected_account_prefs.xml` are
backed up and removed, preserving nine other entries, before restarting GMS and
the client. Both attempts still produce status 4 without an OAuth provider
exchange. Account data and OAuth grants are retained. Therefore neither
uninterrupted restart acceptance nor successful manual migration is claimed.

The next diagnostic comparison must isolate the metadata opt-in from the other
service/runtime changes and trace the missing chooser/recovery transition.
The provider still has one account; all three hosts remain alive; status remains
3 without an override. The temporary screen-timeout override is restored.
See [candidate59-followup.json](../tests/google-auth/full48/candidate59-followup.json).

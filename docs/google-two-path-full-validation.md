# Full two-path application validation

Status: in progress. The goal is to fix and validate every application flow in
the [validation plan](../tests/google-auth/full48/validation-plan.json), including
backend use, renewal and restart. The
[previous batch](google-two-path-live44.md) remains the baseline. A connection,
account picker, or partial login is not a full application pass.

Latest result: candidate 75 adds fresh FotMob Google app sign-in through the
official account chooser and persistence after another client restart. The
token saved by the app is independently verified with Google's signing keys.
Candidate 74 validates FotMob's Google favorite-team cloud round
trip and silent delivery of a fresh Google-signed ID token after its saved login
token expired. An app-side reply fingerprint matches the independently verified,
unexpired token from microG. The original APK uploads a favorite change and
restores both teams after its local favorite files and cache are cleared.
Candidate 73 separately validates that cloud feature in a retained Facebook
session. Candidate 72's native APK-descriptor classification fix keeps Cronet
129 past its earlier crash. These are specific feature results; the wider
application and isolation plan remains incomplete.

Candidate 71 accepts Gmail's bounded native OAuth scope list while still denying
ungranted token access; candidate 70 fixes its GMS APK loading. The mailbox
remains unsynced. Gmail scope consent, native account-selection mediation, the
GMS-version gap and remaining service eligibility failures are still open.
No full application pass is established.

Earlier, candidate 62 renews Wattpad's legacy Google sign-in after
the SDK's cached selection is deliberately expired. Official GMS returns a
successful callback, microG supplies fresh OAuth tokens, and Wattpad reaches
separate signup without clearing its username or opening a chooser. A control
with a revoked saved-selection epoch denies both legacy requests and issues no
Wattpad OAuth token. This is a local SDK-expiry test, not actual server-token
expiry or a full application pass. Drive's OAuth/file-listing gap and the wider
Google/Facebook acceptance plan remain open.

Candidate 63 exposes the account to Gmail through the pinned provider, and
candidate 64 discovers its real mail sync adapter. Gmail's manual-sync action
uses an internal asynchronous pipeline; discovering the Android sync adapter
does not prove that Gmail invokes it or that mailbox synchronization completes.

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

### Candidate 60: metadata reversal and callback-queue diagnosis

Turning off the broker's account-ID metadata option does not recover Wattpad.
With the candidate-59 GMS/provider and candidate-52 client, both the cleared-choice
run and a run restoring the SDK/GMS sign-in preferences from verified backups
return modern status 16 and legacy status 4 twice. Neither produces a remote
activity launch or a provider OAuth token for Wattpad. These are sequential
reversal/recovery checks; the complete app-data snapshots were archived rather
than restored, so they are not identical-state replays of the earlier on run.
They establish that disabling metadata alone is insufficient to recover this
state, not that metadata cannot affect any sign-in path.

The original APK's `GoogleManager` has a status-4 branch that constructs the
legacy sign-in intent and starts an activity for a result. Its launch wrapper
passes a non-null intent through. The observed callback alone does not establish
that this particular application branch executed.

An opt-in, read-only callback diagnostic samples the main message queue two
seconds after supported sign-in callbacks. It logs handler/callback class names,
message codes and timing, never message objects, bundles or argument values.
Both `WESTLAKE_GOOGLE_SIGNIN_QUEUE_TRACE=1` and private logging are required;
the existing sign-in diagnostics must also be enabled. It does not remove
barriers, dispatch messages or change authentication responses.

With metadata re-enabled, the diagnostic retry again receives statuses 16, 4
and 4. All three samples have no head synchronization barrier. A short overdue
frame-message backlog in one sample drains by the next sample. No chooser
launch or Wattpad provider OAuth exchange follows. A persistent main-queue
barrier is therefore not supported by this capture; callback delivery and the
application's subsequent control flow still need tracing.

The Java/runtime/boot builds, 14 callback-parser checks and board preload pass.
The first preload attempt exhausted storage during staging and never ran the
preload process. Incomplete/redundant build copies were removed, and private
app-data snapshots were verified and archived on the host. A first runtime
attempt was separately interrupted by GMS's uncaught-exception handler killing
its main process; the underlying exception remains unidentified. GMS and the
client were restarted, and all three hosts remained alive through the captured
retry. Certification status remains 3 without an override. No account signup,
renewal pass or full application pass is claimed.

See [candidate60.json](../tests/google-auth/full48/candidate60.json).

### Candidate 61: distinguish token retrieval from interactive login

The callback diagnostic now records the callback implementation class. An
additional opt-in ART method trace requires `WESTLAKE_GOOGLE_SIGNIN_METHOD_TRACE=1`
as well as the existing diagnostic and private-log flags. It starts only for the
first legacy sign-in response, writes inside a private app directory, and records
method execution rather than argument values. It materially perturbs timing:
the requested three-second capture actually spans 46.09 seconds on this board.
The 329,256 decoded records match the trace header, with no overflow or unknown
method records. The Java/runtime/boot builds and board preload pass.

The legacy status-4 replies reach `GoogleManager$getAuthenticationToken`'s
callback and its failure handler. The captured interval does not execute the
separate interactive-login callback. Original-APK analysis explains the branch:
when `silentSignIn()` has not completed, Wattpad's `isLoggedIn()` treats a non-null
`google_username` preference as a logged-in session. Its token-retrieval failure
handler does not open the chooser. The preference was present while the SDK's
cached Google selection had expired. The trace starts after that earlier branch;
the branch diagnosis combines static code, cache inspection and the recovery
experiment rather than claiming it was directly traced.

With execution and queue tracing disabled, removing only `google_username`
restores the interactive activity launch. The exact preference is backed up and
ten other entries are preserved. This first retry does not deliver a Wattpad
OAuth token in the observed interval. A second targeted recovery backs up and
removes only Wattpad's three remembered GMS-choice entries, preserving nine
others, then restarts GMS and the client. The account chooser appears. Selecting
the already authorized test account produces three successful interactive OAuth
reads, a saved OAuth selection, a successful follow-up legacy-auth token reply,
and Google's `RESULT_OK` return to Wattpad. The provider records four Wattpad
OAuth issuances. Wattpad reaches **Create an Account**; no signup is submitted.

This recovers live OAuth delivery with both GMS and microG on candidate 59 and
account-ID metadata enabled. It does not establish automatic recovery or renewal
after expiry. The original-app legacy `silentSignIn` service-91 path remains
different from the synchronous `IAuthManagerService` transaction-5 path currently
covered by `GoogleAuthOperations`. The device remains uncertified (3), with no
override or change to check-in identity. Provider grants are unchanged from the
approved configuration, including the earlier birth-date permission. The
account-ID metadata reply is counted separately from the four OAuth issuances.

A subsequent client-only restart preserves all app preferences and again reaches
the signup page through the existing-account Google-login button. No chooser or
new remote activity result is needed. The provider issues one fresh OAuth token,
the synchronous legacy-auth reply carries it back to Wattpad, and all three hosts
remain alive. The active SDK selection now has a numeric ID and an unexpired
timestamp. This is authorization reuse across that restart, not a completed
Wattpad account session or an expiry-renewal result.

A controlled expiry check changes only the active SDK selection's
`expirationTime` to ten minutes before the board clock, preserving its numeric
ID, account fields, scopes, app username, GMS choices and provider grants. The
next client restart returns modern status 16 and legacy status 4 twice, with no
interactive activity launch, follow-up transaction-5 request or Wattpad provider
OAuth exchange. The login form remains visible. Thus the newer numeric ID alone
does not recover the expired-SDK path. This is a local SDK-expiry simulation;
Google-issued tokens and server state were not expired. The exact preference
backup is restored after the test. After another client-only restart, that
restored cache again yields one live provider OAuth issuance and a token-bearing
transaction-5 reply, reaching signup without a chooser. The sequence is therefore
working unexpired cache, failing expired cache, working restored cache. No GMS or
provider restart or permission change occurs between these three checks.

See [candidate61.json](../tests/google-auth/full48/candidate61.json).

### Candidate 62: asynchronous legacy sign-in renewal

The missing route is the original `ISignInService` transaction 101, whose work
continues after its initial Binder reply. The previous synchronous auth scope
cannot cover that worker lifetime. An opt-in adapter now parses the pinned
GoogleSignInOptions request and creates a bounded authorization lease from the
HELLO-verified original APK, signer, callback, requested scopes, optional account
and saved-selection epoch. Official GMS's persistent process can consult that
lease through a private, verified same-app channel. The provider still checks its
separate operator grant and rechecks liveness before and after HTTP. Completion
of callback 101, channel death, timeout or revocation denies subsequent reads.
Google's returned status and account payload are not replaced.

The bounded parser passes 21 host checks, including unsupported and malformed
options. The lease passes 19 checks covering scope/account/signer mismatch,
missing authorization, death, deadline, revocation and completed requests. The
adapter/runtime/boot builds and board preload pass, with no class-initialization
failures. Only official GMS is updated to candidate 62; microG remains on 59,
the original Wattpad client on 61, and the native watchdog fix on 54. Method and
queue execution tracing are disabled for the live tests.

The first live retry uses the same timestamp-only SDK-expiry manipulation as
candidate 61, preserving the numeric account ID, username, GMS choices and scope
grants. GMS accepts two legacy requests. Their overlapping eligibility causes
one read to be refused rather than assigning it to an arbitrary operation. One
legacy callback returns status 4, and the other returns status 0 with an account.
Two asynchronous OAuth reads and a subsequent synchronous auth read obtain
tokens; microG records three Wattpad OAuth issuances. Wattpad reaches **Create
an Account** without a chooser. The modern sign-in attempt still returns 16.
No separate Wattpad account is created.

For a negative control, the client is stopped, the exact same expired SDK
snapshot is installed, and only Wattpad's saved-selection epoch is changed to a
revoked generation. GMS and microG remain running. Both legacy callbacks return
4, no Wattpad OAuth token is issued, and the existing-account login form remains
visible. This tests pre-request revoked state; it does not test the public
sign-out/revocation API or revocation during an in-flight HTTP exchange.

Restoring the exact original epoch state, reinstalling the same expired SDK
snapshot and restarting only the client reproduces the successful result:
legacy callbacks 4 then 0, two asynchronous token reads, one synchronous token
read, three provider OAuth issuances, and the signup page without a chooser.
This run needs an explicit host foreground action after an initial black
capture. GMS and microG remain running throughout all three authorization
controls. The exact original SDK preference and epoch state are restored after
the test, and the client is stopped. The controlled sequence is therefore
successful renewal, revoked failure, restored successful renewal.

The new route is disabled by default (`WESTLAKE_GOOGLE_ASYNC_SIGNIN=1` enables
it alongside the existing guards). It supports the tested OAuth silent-sign-in
variant, not asynchronous ID-token/server-code or extension variants. Matching
uses pending verified requests and fails when more than one qualifies; there
is no unique operation identifier propagated through every GMS executor. The
implementation therefore relies on the pinned official GMS worker routing.
This run does not establish general concurrency reliability, live caller-death
or in-flight revocation behavior, distinct-client UID isolation, actual Google
token expiry, or Wattpad account-session/reading acceptance. Certification
status remains 3 without an override, and check-in identity and provider grants
are unchanged.

See [candidate62.json](../tests/google-auth/full48/candidate62.json).

### Candidate 63: separate Gmail account visibility from sync and eligibility

Gmail is retested with candidate-62 Java/boot artifacts and the candidate-54
native watchdog fix. The original base APK and three splits verify with their
Google signing lineage; the app data is retained. GMS stays on 62 and microG on
59. With the original client configuration, Gmail renders its welcome/setup
screens, lists no account, and displays **Device not compatible**. This
configuration has a Play-services connection but no auth-provider connection;
`LocalServiceBinders.accountService()` therefore supplies its empty-account
fallback.

The comparison adds Gmail's connection and pins to the existing auth provider,
plus only `client.com.google.android.gm.sha256` in the private provider grant.
The signer comes from the verified original APK's current certificate. No OAuth
scope or account-ID permission is added for Gmail. With the same binaries and
after a client-only restart, Gmail displays the existing test account. The
provider records successful account and feature queries from the verified Gmail
process. GMS and microG remain running. Explicit host foreground actions are
needed after black captures; automatic window handoff is not claimed.

The compatibility dialog persists with the account visible. Original-APK
resource analysis maps that text to the Google API status-9 branch. Several
service connections return 9, but the current process-global `afterService`
diagnostic is approximate during overlapping requests; this test does not
identify one required service as the sole cause. Account discovery and the
compatibility warning are distinct failures.

After dismissing the warning and selecting **Take me to Gmail**, Gmail renders
its main account screen with **Account not synced**. **Sync now** does not
produce a mailbox in the observed interval. The deployed candidate-62 build's
source hash for `AppSpawnXInit.java` matches the current file. Its
`installContentServiceAdapter()` handles content observers but supplies false,
zero, empty or null defaults for sync queries and does nothing for sync
requests. This is a concrete missing sync implementation, consistent with the
visible behavior; this run does not trace which sync method Gmail invokes at
that tap. A real sync-adapter path and its lifecycle still need implementation
and board validation.

Official GMS also records four Gmail OAuth requests for `peopleapi.readonly`
and four local scoped-auth refusals in the captured interval, alongside four
account-ID requests. No Gmail OAuth issuance or mailbox sync is accepted.
Account-ID metadata and account enumeration are not OAuth or backend passes.
The next work must cover first-party account/scope authorization, required GMS
service eligibility and actual sync execution, without treating an account-list
fix or a generic compatibility message as evidence of Google registration.
Status remains uncertified (3), without an override, and check-in identity is
unchanged. The sole provider-grant change is Gmail's signer pin.

See [candidate63.json](../tests/google-auth/full48/candidate63.json).

### Candidate 64: implement app-owned sync and separate discovery from execution

The default-off `WESTLAKE_LOCAL_SYNC=1` client switch routes supported content
service sync calls to a process-local dispatcher. It discovers the client's own
enabled manifest services, resolves compiled sync metadata, checks that the
account is visible, binds through the existing service manager and invokes the
real `ISyncAdapter`. Initialization and actual sync have distinct callback
tickets; duplicate or stale replies cannot complete a later attempt. The
dispatcher persists sync settings, bounds the queue and handles completion,
Binder death and cancellation with a deadline. It grants no OAuth permissions
and does not substitute a successful adapter response.

The callback-ticket host test passes 14 checks, including concurrent duplicate
callbacks. Java compilation covers 138 sources and 290 classes. Runtime packaging,
boot build and board preload pass, with no class-initialization failures. Native
artifacts and hash-verified unchanged framework outputs are reused.

Only Gmail receives the new Java/boot overlay. On its first launch and a second
preserved-data launch, it discovers three enabled sync adapters, including
`gmail-ls` / `MailSyncAdapterService`, and calls the master/per-account setting
queries and status-listener registration. Gmail renders **Account not synced**;
the status-9 compatibility dialog still appears. The captures contain no uncaught
exception markers. This validates discovery and queries, not execution.

Ordinary input, a main-queue hit-tested click comparison and an explicit root-0
input comparison produce no content-service sync request. Automatic root
selection is restored. Input submission reaches the native bridge, but the
button listener's execution is not established. Existing private-input logging
deliberately suppresses view-tree dumps. No adapter binding, `startSync`, callback,
mailbox content or mail sending is observed.

A completed resource-reference scan and targeted inspection of the original APK
also correct the assumption that **Sync now** directly calls Android's sync API.
Its listener enters account-component futures and an internal `SyncEngine` path,
then further account work and conversation-provider notifications. This static
path does not establish where the live run stops. Tracing those stages and input
delivery remains necessary alongside validating the new dispatcher separately.

Across the two starts, the captured GMS/provider window contains four account-ID
metadata requests, four `peopleapi.readonly` requests with local silent OAuth
read refusals, and four provider feature-query refusals. No Gmail OAuth issuance
or specific mail-token request is established. These observations do not show
Google rejecting an issued Gmail token.

GMS stays on candidate 62 and microG on 59. Both remain alive. Status remains 3
without an override; the check-in identity, grants and provider account-store
bytes are unchanged across this candidate. The original Gmail APK is unchanged.
Periodic/delayed scheduling, durable jobs, retry/backoff, cross-process sync
state and destruction of services retained by the binding cache remain missing.
Cancellation cannot forcibly stop adapter code that ignores it. The feature is
still experimental, and the full application pass count remains zero.

See [candidate64.json](../tests/google-auth/full48/candidate64.json) and the
[dispatcher test notes](../tests/google-auth/README.md#app-owned-sync-dispatcher-candidate-64).

### Candidate 65: distinguish input delivery from click and sync execution

The native `i` command prints a receiver-tap label, but the deployed native
implementation prefers `OHTouchInjector` when that helper is available.
Instrumenting only `InputEventBridge` therefore produced no events even when
the compatibility dialog visibly closed. The diagnostic build now covers both
Java paths, with a default-off flag and a requirement for private logging.

The trace records dispatch phases and exception class/stack identifiers, plus
bounded structural window metadata. It does not read view text, titles, account
values, entered text or exception messages. Routing and event handling are
unchanged. Compilation, packaging, boot build and preload pass. The preload
reuses independently hash-verified public framework artifacts through a private
temporary cache; both that index and the disposable probe copies are removed.

On the board, the compatibility-dialog tap selects the dialog and both DOWN and
UP run on the main thread and return handled. After dismissal, **Sync now**
selects the single remaining focused, visible and attached main window. Its
DOWN and UP also run on the main thread and return handled, with no dispatch
exception. This rules out an initial main-thread handoff failure or wrong-window
selection for this captured tap. It does not prove that Android's queued
`View.PerformClick` runs or that Gmail's listener completes its internal futures.
No content-service sync request, adapter execution or mailbox content is observed.

GMS and microG remain alive; their identity, grants and provider account-store
bytes are preserved. Status remains 3 without an override. Full application
acceptance remains open. Exact results are in
[candidate65.json](../tests/google-auth/full48/candidate65.json).

### Candidate 67: repair missing input transforms; Gmail's sync listener now runs

Diagnostic iterations 66 and 66b ruled out a stranded click in the main queue.
The compatibility dialog's queued click drained normally, but the manual-sync
tap left no click queued and never entered Gmail's listener. Structural metadata
placed the enabled, clickable button under the tap, beneath a transformed parent
view. The original native library's `MotionEvent::transform` and `applyTransform`
were verified as single return instructions. Drawing used the parent's transform;
the event passed into that parent did not receive the matching coordinate change.

The native implementation now composes view transforms while keeping raw samples
separate, implements explicit raw-sample transformation and returns transformed
JNI pointer coordinates. Copying and history retain the transform. Global scaling
preserves normalized pressure and size. The private C++ object layout changes, so
the build audits compiler dependency files and rebuilds both consumers together.
Other native inputs, including the existing privacy and BLAST fixes, are retained
with hash verification.

Forty host checks pass; the old source fails the first translation check. Java
compilation, native linking, runtime packaging, boot generation and board preload
pass, with no class-initialization failures. Gmail keeps its original APK and app
data. After the same tap, the fixed trace records `View.PerformClick.run`, the
Material button's `performClick`, Gmail's `pai.onClick`, `oly.j`, and the account
component's `okv.c`/`okv.m`. This validates the missing-click repair. A subsequent
tap without ART method tracing also queues and drains `PerformClick`.

The method trace perturbs execution: although it requests a stop after three
seconds, the recorded interval is about 17 seconds. It does not overflow. The
queue and hit diagnostics are bounded, default off, and require private logging;
they do not inspect message payloads or view text. Raw traces remain private.

Mail still does not load. The trace has not established completion of the account
future or execution of the later sync callback body. No content-service sync
request, sync-adapter execution or mailbox content is observed. There are no new
auth-proxy/delegation diagnostic events in the captured post-click window; that
is not proof of no network or authentication activity. No mail is sent.

GMS and microG remain alive. Status stays 3 without an override, and check-in
identity, provider grants and provider account-store bytes are unchanged. Input
parity also remains incomplete: transformed JNI batching still uses translation
offsets for incoming coordinates, surface rotation is reported unknown, and
MotionEvent Parcel I/O is unimplemented. Full application acceptance remains open.
See [candidate67.json](../tests/google-auth/full48/candidate67.json) and the
[native input test instructions](../tests/parity/native/README-input-transform.md).

### Candidate 68: capture private logs and identify Gmail loading/version gaps

An opt-in native Android Log sink allows this credential test to capture app
logs without sending them to HiLog or stderr. The launcher must supply a private
directory owned by the app UID (0700). The sink accepts only a regular 0600 file
with one link, refuses final symlinks, bounds records to 4096 bytes and the file
to 8 MiB, and drops contended writes instead of blocking application work. The
existing muted behavior remains the default. Raw logs may contain credentials;
only fixed findings and hashes are included here.

Fourteen host checks pass, including permissions, symlinks, hardlinks, FIFO
refusal and concurrent size-limit enforcement. The native library compiles and
links with candidate 67's other inputs hash-verified. Only Gmail receives the
native overlay; its Java artifacts, original APK and app data are retained.
The process launches and writes an app-owned 0600 capture. ART method tracing
is disabled for this retry. GMS and microG are not restarted.

The capture records two concrete failures. Gmail asks for GMS version 250200000
but receives 244034029. Separately, the package manager supplies GMS base and
configuration-split paths under the other host's private runtime; Gmail cannot
open those assets or instantiate GmsAppComponentFactory. Dynamite cannot load
its loader from that context, so Cronet and ProviderInstaller module loading
also fail. `PlayServicesChannel.localize()` currently translates remote paths
to the host's real filesystem path. A native probe outside the app SELinux
context can open the hash-verified APK after dropping to Gmail's UID. Unix UID
permissions alone are therefore not established as the cause; the actual app
security context, native path handling and AssetManager loader still need
isolation.

Gmail still displays the status-9 compatibility warning and Account not synced.
Dismissing the dialog and tapping Sync now does not establish mailbox loading.
The new evidence identifies loading and version gaps, not their exact share of
the downstream failure, and does not demonstrate a Google registration refusal.
No version number, certification result or API success is substituted. A proper
repair must establish APK/resource loading in the actual client context while
preserving private GMS data, then test an official GMS version meeting the app's requirement
and revalidate the pinned authentication routing.

Status remains 3 with no override; check-in identity, provider grants and account
store bytes are unchanged. Full app/backend acceptance remains unproven. Gmail
disables one internal logger backend, and the bounded capture can drop records;
absence of a log is not proof that an operation did not occur.

See [candidate68.json](../tests/google-auth/full48/candidate68.json).

### Candidate 69: confirm the asset failure in Gmail's actual security context

A bounded native diagnostic now observes the existing APK load and, for up to
32 explicitly selected APK paths, attempts a read-only open in the same process.
It emits only numeric open/ZIP/load metadata, never paths or raw bytes. Both
private logging and the diagnostic switch must be enabled; loader results are
unchanged. The native rebuild retains candidate 68's private capture and all
previous input, BLAST and signal fixes. Only Gmail is restarted.

The first four startup samples all report UID 20010060, errno 13 (EACCES), no
ZIP provider and no loaded APK assets. This identifies file access before ZIP
parsing as the immediate failure. By comparison, a separate native probe can
open the hash-verified original GMS APK after dropping to that UID, both outside
and inside the client's mount namespace. That probe does not run in the same
app security context. The controls establish a context-dependent access
restriction, not a specific SELinux rule or a corrupt APK.

The next repair should use root-spawner-verified read-only descriptors for the
remote public APKs. It must preserve the separate GMS private data boundary.
This diagnostic does not yet implement that repair, meet Gmail's newer GMS
version requirement, or establish mailbox/backend acceptance.

See [candidate69.json](../tests/google-auth/full48/candidate69.json).

### Candidate 70: give Gmail verified read-only access to GMS package code

The default-off client route uses the existing root spawner protocol to verify
the connected GMS process and obtain its launch APK descriptors. The launcher
pins the spawner and the original base APK SHA-256. The client checks descriptor
type, read-only access, APK filenames and bounds, then verifies the base hash
with positional reads. It maps only GMS source, scan and split APK paths to the
retained descriptors. GMS private data and native-library directory paths are
not exposed or remapped by this change. There is no package-version or
certification substitution.

The descriptors live for the client process because loaded code can outlive a
Binder connection. Reconnection must present the same files; a changed code set
requires a client restart. A terminal verification refusal is cached until
restart. The initial wrong-hash test refused code access but repeated the large
base hash on later package queries; the final build removes that retry loop.

Initial board validation verifies nine GMS APK descriptors and successfully
opens, creates ZIP providers for and loads the base plus both configuration
splits. Their direct-path baseline failed with EACCES in the same client UID.
GmsAppComponentFactory and Dynamite loader error markers disappear; the Cronet
install-time split is mapped and ProviderInstaller reports installing
GmsCore_OpenSSL. These are code-loading/provider milestones, not proof of
network operation or mailbox sync. Gmail still shows Account not synced,
status-9 service failures and the separate requirement for GMS 250200000 while
the actual installed version is 244034029.

The original positive/wrong-hash/restored-pin sequence succeeds, refuses code
access, then reproduces successful asset loads. The final cached-rejection build
repeats that sequence: the valid pin loads all three assets; the wrong pin
produces exactly one refusal in both initial and later captures, no verified
code set, no successful asset loads and no GMS-owned regular descriptors;
restoring the valid pin reproduces all three successful loads. No uncaught
exception or fatal-signal markers appear in these control captures.
Native code uses the verified candidate-70 APK diagnostic; Java compilation
covers 140 sources and 293 classes, runtime packaging and boot-image builds
pass. This candidate is tested through real Gmail launches, without a separate
standalone preload run.

The base APK is explicitly hash-pinned; the split set is supplied by the trusted
root spawner rather than individually configured hash pins. Production code
installation/update integrity, native-library mapping, general reconnect
behavior and full application acceptance remain unvalidated.

GMS and microG remain alive throughout the client controls. Certification stays
3 with no override; provider grants, account-store bytes and check-in identity
are unchanged. No new Gmail OAuth scope or account-ID permission is granted.

See [candidate70.json](../tests/google-auth/full48/candidate70.json).

### Candidate 71: parse Gmail's native scope list without authorizing it

After the code-loading repair, Gmail requests a 14-scope OAuth token. Its
original signed APK contains a scope list longer than the silent-request
handler's 512-character limit, with one trailing ASCII space. The handler
rejects the request as unsupported and the cache-key parser separately rejects
the trailing delimiter. These are local compatibility failures, not Google
server refusals of an issued token.

The silent handler now uses the parser's existing 4096-character bound; the
complete package/signer/service key must still fit that bound. Ordinary OAuth
scope lists may contain one trailing ASCII space. Scope comparison ignores that
one delimiter but preserves the exact service passed to the provider. Leading
or repeated spaces, control/non-ASCII whitespace, duplicate scopes, and
server-code requests with trailing spaces remain rejected. This creates no
account selection, provider scope grant, or scope alias. Diagnostics recognize
the additional fixed public Gmail scope names without printing arbitrary scope
values.

Seven host test programs pass: 175 numbered checks plus the boundary-trace test.
The previous parser fails the new trailing-delimiter case. Compilation covers
140 Java sources and 293 classes; runtime packaging and boot-image builds pass.
Only the official GMS host receives the candidate-71 Java/boot overlay. GMS and
Gmail restart with retained data; Gmail keeps client 70b and native runtime 70,
while microG stays on 59 without a restart. No standalone preload is claimed.

The real Gmail startup now passes three OAuth requests into mediation: one
14-scope request and two People API requests. All three token reads are denied.
After a recorded Sync now tap, another 14-scope request passes parsing and its
token read is denied. The request has zero unknown scope categories and one
empty trailing segment in diagnostic classification. This verifies parsing and
continued denial; it does not verify token issuance or backend acceptance.
The action window uses the board's whole-second clock and shared-host logs, so
it is correlation rather than exclusive attribution to that click.

The account list alone does not create a reusable scoped authorization. The
current saved-selection mechanism requires a successful supported Google
Sign-In activity; Gmail's native account selection needs separate mediation.
The operator's approval for Gmail's broader scopes is also pending. No Gmail
OAuth scope or account-ID grant is added. Gmail still displays Account not
synced, has an unmet GMS-version requirement and receives service status 9.

The nine verified GMS APK descriptors and three successful base/configuration
asset loads are reproduced after restarting GMS and Gmail. Status remains 3
without an override; check-in identity, provider grants and account-store bytes
are unchanged. All three hosts are alive at the preservation check.

See [candidate71.json](../tests/google-auth/full48/candidate71.json).

### Candidate 72: recognize Android libraries loaded through APK descriptors

Restarting FotMob with its retained Facebook session exposed a native failure
after verified GMS code loading. Two bounded native traces locate a fatal
`brk` in Google's Cronet 129 after `sysconf(96)` returns -1. The import resolves
to the compatibility shim, but its caller classifier sees `/proc/self/fd/N`,
not the underlying APK filename. The old `.apk` test therefore misses this
Android caller and forwards bionic's configured-CPU selector unchanged to
musl. A private diagnostic confirms the descriptor label and missed translation.
The first untraced run reported signal 11; only the subsequent traced signal-5
failures are localized to this Cronet branch.

The classifier now resolves bounded `/proc/self/fd/N` labels, including APK
member suffixes, and applies the existing APK filename rule to their targets.
It preserves `errno`, rejects malformed/closed descriptors and does not cache
descriptor numbers, which can be reused. Ordinary native-library descriptors
remain native. This classifies the calling ABI; it does not verify a package,
signer, scope or token. Candidate 70's root-spawner and APK hash checks remain
the separate code-trust boundary.

The host test passes 27 checks using real APK/native-file descriptors, malformed
labels, closure/reuse and preserved `errno`. The previous classifier fails the
first live APK-descriptor case. Both a narrow port to the previously tested
native baseline and the current repository's full shim build compile. The
narrow port initializes Cronet 129 in two independent FotMob launches. Their
captured log spans exceed 328 and 96 seconds respectively, with no recurrence
of the traced startup trap. Match lists and navigation render, and both launches
show the retained account menu with Sign out. No credentials or new consent
are supplied in this restart check.

The current repository shim is then deployed and its mapped inode and binary
hash verified in a fresh FotMob process. It also initializes Cronet 129 and
renders the match list, news feed and retained signed-in menu. Its captured log
span exceeds 212 seconds. A temporary black display recovers by foregrounding
the host, without restarting the client; general window routing is still open.
This follow-up uses the actual repository build rather than only the narrow
baseline port. GMS and microG remain alive;
certification stays 3 without an override, and check-in identity, provider grants
and provider account-store bytes remain unchanged.

The menu establishes retained local signed-in state, not a fresh authenticated
backend response. Public match data can also be available without login.
Facebook restart behavior is not evidence of Google token delegation or
renewal. The original FotMob APK and app data are retained; GMS/microG keep
separate hosts. Service status-9 responses and the actual GMS version gap remain.
Cloud preference round trip, server-token expiry, sustained/concurrent use and
full application acceptance remain incomplete.

See [candidate72.json](../tests/google-auth/full48/candidate72.json) for exact
artifact boundaries and the current-source build follow-up. Raw traces, account
identifiers, tokens and screenshots remain private.

### Candidate 73: FotMob cloud favorites survive loss of their local cache

The original FotMob APK continues with the Facebook session established in
candidate 47 and the current-source shim validated in candidate 72. The stored
account identifier matches the SDK token, whose local expiry fields have not
elapsed. No credentials, additional permissions, signup or new login are used
in this continuation.

A controlled test first reads the test account's team dataset from FotMob's
server. Tapping Follow in the original app adds a second team; a subsequent
read shows that exact addition. The host only reads the dataset and does not
write it. The client is then stopped, and its favorite-team JSON, ordering JSON,
team ETag and application cache are moved to a private rollback directory.
Four login/SDK preference files remain byte-identical. The original APK's team
manager code identifies these files as its local favorite store.

After restarting with an empty cache, the app creates a new incoming dataset
file, byte-identical to the independent 134-byte server response. It restores
both favorite-team records and displays both teams in Following. Three SDK
preference files remain byte-identical after restart; the app preference file
changes only its normal launch counter, with login fields unchanged. This
validates outgoing synchronization and
in-app restoration for this retained Facebook session, beyond the earlier
local Sign out menu observation.

A read control is important to interpreting the result: the dataset GET returns
the same HTTP 200 body with the real token and an intentionally invalid token.
Only the authorized test account is queried. Therefore that GET response cannot
prove credential validation or access-control enforcement. The observed app
write and restoration establish feature behavior; they do not establish the
server's authentication policy. Raw URLs, account identifiers, tokens, response
bodies and captures remain private.

GMS and microG remain running, status stays 3 without an override, and check-in
identity, provider grants and account-store bytes remain unchanged. No source
change or new build is needed for this candidate. A fresh Facebook login on this
exact build, actual token expiry/renewal, FotMob's Google cloud flow, general
window/input behavior and the other planned applications remain incomplete.
The temporary favorite markers are retained for continued testing, with cleanup
pending. No full application pass is added.

See [candidate73.json](../tests/google-auth/full48/candidate73.json).

### Candidate 74: Google token renewal and cloud favorite restoration

FotMob's Google session from candidate 44 is resumed on Java adapter 71,
native runtime 70 and the current repository shim from candidate 72. The APK is
unchanged. Its saved Google ID token has expired at board time; the stored
account ID matches that token's subject. Startup requests pass from the original
app through official GMS to the separate microG host without a new account
chooser, credential entry or scope grant. Cronet 129 initializes successfully.

The provider cache contains a different ID token issued during this test.
Verification against Google's public signing keys succeeds, with an accepted
Google issuer, the same subject and FotMob audience, and an unexpired `exp` at
board time. A subsequent client restart enables the existing private TokenData
fingerprint diagnostic. Five token replies are observed; the final provider
cache's independently verified fresh token matches one of those exact client
reply fingerprints. This establishes delivery to the original app through GMS,
beyond a provider-only success message. It does not independently verify all
five reply tokens. The diagnostic is then disabled and the exact original
launcher restored. Another restart returns five token replies without any
fingerprint markers. Tokens, fingerprints, account IDs and raw claims stay private.

The on-disk login token remains unchanged. Inspection of the original APK shows
that its sync path requests a token into a temporary result and can return that
token without updating the saved login-token preference. Therefore the old
preference alone is not evidence that renewal failed. This test demonstrates
recovery with a naturally expired saved token and fresh silent issuance; it
does not wait through the new token's full lifetime or establish every expiry,
revocation or error-recovery case. The existing experimental provider forces
refresh for delegated requests, so this does not isolate an expiry-triggered
refresh decision from ordinary startup behavior.

In the Google session, the original Follow button adds Manchester United to
the test account's existing Barcelona favorite. Independent read-only requests
observe the server team count change from one to two. The host does not write
the dataset. After stopping the app, the favorite JSON, order, team ETag,
incoming sync JSON and app cache are backed up and removed. The login preference
is byte-identical across this reset. A fresh process downloads a new 134-byte
dataset, identical to the independent server response, recreates both favorites,
and displays them in Following. After restart, the login preference differs
only in `app_launch_count`; its Google identity fields remain unchanged.

As in the Facebook control, the Google dataset GET returns the same HTTP 200
body with an intentionally invalid token. Only the authorized test account's
dataset is queried. The cloud write and restoration establish feature behavior;
the GET response does not establish backend credential enforcement. The token
signature and exact delivery checks are separate evidence of Google issuance
and the two-path handoff.

Official GMS and microG retain separate hosts. Certification remains 3 without
an override; check-in identity and provider grants are unchanged. The provider
account-store bytes change during the Google test, and a fresh token is present;
the account count remains one. Whole-store byte preservation is not claimed.
No production source change or new build is required. The added test favorite
is retained for continued validation, with cleanup pending. Fresh interactive
login on this exact build, general window routing, other planned applications
and production isolation remain open. No full application pass is added.

See [candidate74.json](../tests/google-auth/full48/candidate74.json).

### Candidate 75: fresh Google app sign-in on the repaired runtime

Using the same original APK and artifacts as candidate 74, FotMob's own Sign
out action clears its login provider and shows its Sign in button. The ordinary
Google sign-in button opens the official GMS account chooser. After a loading
delay, selecting the already authorized Google test account returns a successful
activity result (`RESULT_OK`, -1) to FotMob and restores its signed-in screen.
No separate signup form, additional scope grant or account-password entry is
needed. This is fresh application sign-in with the existing microG account,
not a new microG account-registration test.

The auth trace records FotMob-owned OAuth and ID-token issuance and a new saved
selection grant. FotMob stores a different ID token from the expired pretest
value. Its signature verifies with Google's public keys; its issuer, original
account subject and FotMob audience match, and it is newly issued and unexpired
at board time. These checks examine the app's saved token, independently of
the sign-in screen and service handshake.

After stopping the client with process identity checks, another process starts
with the same account ID, verified login token and favorite files. Its signed-in
UI returns without another account chooser. This closes the fresh-app-sign-in
check left open in candidate 74. Together, candidates 74 and 75 cover FotMob's
planned Google sign-in, cloud preference round trip, fresh silent token delivery
and client-restart behavior on the repaired runtime.

The device remains at uncertified policy 3 without an override. Check-in
identity and provider grants remain unchanged, with one provider account and
separate live official-GMS/microG hosts. Some GMS service status-9 replies and
the version gap remain; general window behavior, every expiry/revocation case,
the other planned apps and production isolation are not established. No full
application pass is added. Test favorites remain pending cleanup. No production
code change or rebuild is required for this follow-up.

See [candidate75.json](../tests/google-auth/full48/candidate75.json).

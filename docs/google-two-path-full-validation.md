# Full two-path application validation

The [validation plan](../tests/google-auth/full48/validation-plan.json) covers
eight Google app flows with 39 checks and three Facebook app flows with 12
checks. The table records what the original APKs demonstrated on the board.
Most rows combine observations from more than one build. The shared isolation,
concurrency and sustained-use requirements remain open. The latest [process
and window checks](#live-app-and-service-isolation-on-the-board) cover six
live roles and their native HAPs. All eight APKs now have [dedicated test hosts](#dedicated-hosts-for-the-eight-apks).

| Original app | Observed on the board | Still to verify |
| --- | --- | --- |
| FotMob · Google | Fresh sign-in completed. A changed favorite reached the server and returned after clearing the local favorites. A fresh Google-signed ID token reached the app after the saved token expired. The account survived a restart. [Cloud and renewal](../tests/google-auth/full48/candidate74.json) · [Fresh sign-in and restart](../tests/google-auth/full48/candidate75.json) | All four named Google checks have evidence across those two runs. The favorite read endpoint also answered an invalid-token control, so the read alone cannot establish server authorization. |
| Spotify · Google | Earlier runs returned account selection to Spotify and reached its signup form on the shared host. The dedicated hosts later passed the two-caller window test. With a fresh profile using official Play services and Play Store, the original Spotify APK automatically foregrounded Google’s sign-in window. It stayed on “Checking info” for at least 7 min 57 s. [Signup](../tests/google-auth/full48/candidate51-followup.json) · [Dedicated host](../tests/google-auth/full48/candidate93.json) · [Two callers](../tests/google-auth/full48/candidate97.json) · [Official Google setup](../tests/google-auth/full48/candidate98.json) · [Back and DNS retest](../tests/google-auth/full48/candidate99.json) | Automatic return after successful Google sign-in on the dedicated host, a Spotify session, library, playback, silent renewal and persistence. |
| Wattpad · Google | Google sign-in returned to Wattpad. After the local SDK selection was deliberately expired, silent sign-in returned to the separate Wattpad signup form. [Activity return](../tests/google-auth/full48/candidate51-followup.json) · [Expiry control](../tests/google-auth/full48/candidate62.json) | Established Wattpad account, reading or library, persistence and renewal after actual token expiry. |
| YouTube · Google | With the source-built codec library, the original app played the public clip “Me at the zoo” to 19.063 seconds twice. The board showed moving video in the upper-left of the player window. [Original-player comparison](../tests/google-auth/full48/candidate100.json) | Google sign-in, account persistence, audible output, correct picture sizing and broader playback coverage. |
| Gmail · Google | Enabling sync and restarting the original app populated the inbox. An existing message body opened. The app’s daily periodic registration survived another restart. [Inbox and message](../tests/google-auth/full48/candidate79.json) · [Periodic registration](../tests/google-auth/full48/candidate80.json) | The pull-to-refresh gesture was exercised. Completion of an incremental refresh with a server-side change remains open. |
| Drive · Google | The approved account listed the existing 107-byte fixture. Its three lines rendered from a search result and again from My Drive after a process restart. [Board results](../tests/google-auth/full48/candidate89.json) | Silent renewal after actual token expiry. The second open could reuse cached file content. |
| Maps · Google | Guest startup, map tiles, place search, a route preview and restart were observed with both service connections enabled. The native permission prompt and revocation worked. [Guest search and route](../tests/google-auth/full48/candidate83.json) · [Permission and location](../tests/google-auth/full48/candidate88.json) | A real position fix. OpenHarmony accepted two location requests and delivered no fix during the observation. Permission was restored to Deny. Signed-in place search also remains open. |
| Uber · Google | The installed rider app received a trip link for two fixed places. Continue with Google opened Uber’s sign-in form. [Board results](../tests/google-auth/full48/uber-trip-link89.json) | Rider sign-in, authenticated session, in-app destination search, fare preview and persistence. |
| FotMob · Facebook | Fresh Facebook login returned to FotMob, its account menu displayed Sign out, and the session survived a process restart. [Board results](../tests/google-auth/full48/candidate76.json) | All four named Facebook checks have evidence on the tested build. |
| Wattpad · Facebook | Facebook consent completed and the SDK delivered a successful activity result to the original Wattpad app. Wattpad displayed Create an Account. [SDK return](google-two-path-live44.md) | Wattpad account session and restart persistence. |
| Spotify · Facebook | The original APK opened Facebook’s HTTPS login form after the user selected Facebook. Westlake displayed the form in a WebView owned by the Spotify process. [Earlier startup failure](../tests/google-auth/live44/com.spotify.music-facebook-candidate47.json) · [Live form on the dedicated host](../tests/google-auth/full48/candidate97.json) | Account submission, authenticated return, Spotify session and persistence. |

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

### Candidate 76: fresh Facebook login and live token acceptance

On the repaired FotMob runtime, the original Sign out action removes the
Facebook SDK token and clears the app's login type. The original Facebook
button opens the real web login. Authorized existing-account credentials and
the existing FotMob consent complete successfully; the optional save-login-info
prompt is declined. No separate app signup or account deletion is performed.
The local CustomTabMainActivity is reused. Successful activity results reach
FacebookActivity and, from this entry point, FotMob's MainActivity.

The SDK saves a different token for the same account and application. FotMob's
stored Facebook account matches the SDK account. The same three permissions
(email, openid and public_profile) remain, with none declined or expired, and
both token and data-access expirations are in the future at board time.

A read-only host-side GET to Facebook Graph `/me?fields=id`, using the app-saved
token, returns HTTP 200 and the matching account. An intentionally invalid-token
control returns HTTP 401 with Facebook error 190. TLS verification remains on
and redirects are refused. This independently establishes live acceptance of
this token by that endpoint; it does not claim an in-app network capture or
acceptance by every FotMob endpoint. Candidate 73's dataset GET, which also
accepted an invalid token, remains a separate cloud-feature result.

After an identity-checked stop and restart, FotMob retains the same token,
account, permissions and favorite-team sync file and displays the signed-in
More screen. The Graph positive and negative controls reproduce. The original
APK and candidate-71 Java, candidate-70 native runtime and full candidate-72
repository shim are reused; no new implementation or rebuilt artifact is
introduced. The Google FotMob profile is preserved separately.

The preservation check keeps certification at 3 without an override, the same
check-in identity and provider grants, one provider account and both service
hosts alive. Some GMS service status-9 replies and the version gap remain.
This Facebook flow does not exercise Google token delegation. Facebook token
renewal/revocation, a new cloud round trip, general window routing, production
isolation and full application acceptance are not established. Temporary test
favorites remain pending cleanup. Raw credentials, account identifiers, tokens,
logs, API bodies and screenshots stay private.

See [candidate76.json](../tests/google-auth/full48/candidate76.json).

### Candidate 77: fix the Contacts-client compatibility warning

A fresh original Gmail launch reproduces the exact Device not compatible
dialog with ConnectionResult 9. The app's own stderr identifies Contacts as
the failing client. Original-APK inspection connects that label to
AbstractPlayServicesHelper's connection-failure handler and the dialog builder.
That composite client includes People.API_1P and Feedback.API; their original
constructors use service IDs 5 and 29. The independent version check still
reports required GMS 250200000 versus installed 244034029.

The existing opt-in, version-pinned local service eligibility policy now appends
5 and 29. It retains all prior entries and the original rollback journal.
It changes no certification status, token response, requested scope or Google
backend verdict. Twenty-three host checks pass, including unchanged status and
other flag keys; the previous policy fails the new People/Feedback expectation.
All 140 Java sources compile to 293 classes. Runtime packaging and boot-image
builds pass. Nine GMS Java/boot files are replaced with verified source-built
artifacts; exact prior host artifacts and private policy snapshots provide
rollback. The original APKs and native runtime remain unchanged. No separate
preload is claimed.

After the user's explicit approval, the provider receives a Gmail grant pinned
to the original APK's current signer: its 14-scope native request plus the
observed People read-only request. Existing app grants are preserved. This
records operator scope permission only; it does not invent native account
selection or a successful Google Sign-In activity.

Two fresh Gmail processes now render Account not synced without the invalid-
device dialog. Neither captured run logs the Contacts connection failure or
the status-9 dialog. Both invoke the actual IPeopleService interface. The first
run also records broker status 0 associated with 5 and 29; numeric association
remains approximate under overlapping calls, and only People has a subsequent
called-interface observation. No contact-list/backend feature pass is claimed.
The second launch initially has a black frame and recovers after explicitly
foregrounding the host; automatic window routing remains unvalidated.

Sync now sends the original 14-scope request through parsing, but the account-
scoped token read is still denied locally. No Gmail token issuance or mailbox
acceptance is established. The action is correlated using the board's whole-
second clock and shared-host logs. Native account authorization, the provider's
older scope parser, the GMS version gap and other service refusals remain open.

The preservation check keeps uncertified status 3, no certification override,
the same check-in identity, the explicitly approved provider grant, one provider
account and both service hosts alive. Provider account-store bytes are unchanged
across this policy test. The eligibility list gains exactly 5 and 29; other
Phenotype metadata also changes while GMS runs, so the whole preference file is
not claimed byte-identical. This is a specific warning/service-connection fix,
not a full Gmail or production-isolation pass. Private logs, policy snapshots,
account data and screenshots are excluded.

See [candidate77.json](../tests/google-auth/full48/candidate77.json).

## Candidate 78: explicit native-account consent and Gmail token delivery

Gmail calls the native Android account-token service without going through the
Google Sign-In chooser used by third-party apps. A package/signer/scope grant
alone did not authorize that account access, so the previous implementation
correctly denied its token read. This candidate adds a separate, opt-in native
consent record, provisioned for this board test only after the user's explicit
Gmail approval. It does not synthesize a Google Sign-In result or saved chooser
session.

Official GMS uses transaction 28 when a reusable OAuth request has no existing
saved selection and `WESTLAKE_GOOGLE_NATIVE_ACCOUNT_CONSENT=1`. The provider
still requires the pinned official GMS broker, independently verified live
origin and APK signers, and the exact operation's live witness. It additionally
checks the account hash, approved scopes, revocation epoch and consent expiry
before and after network I/O. Returned scopes cannot exceed the approved set.
Google server consent remains required; a consent challenge is not converted
into a token. Existing chooser-based transaction 23 remains separate.

The provider-owned `grants.properties` record uses
`delegate.<package>.native.{source,account_sha256,epoch,issued_at_ms,expires_at_ms,scopes}`.
`source` must be `operator-native-account-v1`; the account hash is SHA-256 of
account type, a NUL separator and account name. Consent lasts at most 24 hours
and does not replace the existing package/signer/scope grant. Removing these
native fields withdraws consent. A changed GMS revocation epoch invalidates the
old record. This is an experimental operator workflow; a production consent
screen and production isolation are not implemented or validated by this test.

The 66 new host checks cover wrong accounts and signers, missing or malformed
records, scope expansion, expiry, epoch revocation and returned scopes. The
combined authorization suite passes 241 numbered checks plus the boundary
trace test. The adapter builds 141 Java sources into 294 classes; runtime
packaging and all nine boot-image JARs pass. Both service hosts receive the
new adapter with verified rollback artifacts. Native runtime binaries are
reused.

The board tests use the unchanged original Gmail APK and existing authorized
account. Without a native-consent record, the provider reports
`native_consent_absent` and no Gmail OAuth token is issued. Installing the
approved record produces provider OAuth issuance and official GMS
`silent_token=true ... native_consent=true`; Gmail receives token-bearing
replies. Removing only the native consent fields, while preserving ordinary
signer/scope grants, denies a fresh Gmail process again. Restoring the same record, without extending its
expiry, reproduces provider and official-GMS token delivery in another process.
These controls test local authorization; token issuance alone is not
authenticated mailbox acceptance.

The first approved launch exposes a separate Cronet 129 fatal trap. Gmail's
runtime still used the older candidate-54b shim; the trap and call frames match
the APK-descriptor ABI-classification failure already fixed in candidate 72.
Deploying that existing current-source shim to Gmail allows Cronet to initialize
and the app to stay alive. The Contacts warning is absent, but the app still
shows **Account not synced**. Its SettingsService requests fail with
`net::ERR_NAME_NOT_RESOLVED`, and its settings client cannot start without the
required server settings. Other GMS status-9 replies and the installed-version
gap remain. The next investigation is the Cronet DNS boundary, followed by a
real mailbox sync check.

Private account records, consent hashes tied to the test account, credentials,
tokens, raw logs and screenshots are excluded from the public evidence.

The new foreign-broker transaction-28 probe builds with the original test
signer, but its live denial is **not validated**. Graphical attempts abort in
physical-display initialization before interaction. Later experimental
headless launches are refused by `HapDomainSetcontext` before Java starts,
despite matching the current HAP token and UID. No SELinux check is bypassed;
those attempts count as setup failures, not security passes. The headless
experiment is not retained in the published test source. The probe shares the
provider's UID and cannot establish production UID isolation in any case.

Final checks retain uncertified status 3 without an override, the check-in
identity and one provider account. Both service hosts and Gmail are alive.
The provider account store changes during token activity; byte identity is not
claimed. Native consent matches the explicitly approved record. Exact sanitized
results are in [candidate78.json](../tests/google-auth/full48/candidate78.json).
Chooser-flow regression on the upgraded hosts and the new foreign-broker test
remain separate outstanding checks.

## Candidate 79: network ABI for APK descriptor callers and Gmail API acceptance

A read-only inspection of live Cronet's relocation slots confirms that
`getaddrinfo` and `freeaddrinfo` already resolve to the native network adapter.
The adapter's caller selector recognizes named or app-written libraries but
misses the loader's APK descriptor label. It forwards those calls with musl's
`addrinfo` member order and constants. Cronet expects Bionic, whose address and
canonical-name pointers occupy the opposite slots. The caller-label problem is
similar to candidate 72's `sysconf` classifier failure, but affects a different
library boundary.

The network selector now reuses `bionic_apk_path.h`, including bounded
`/proc/self/fd/N` resolution and descriptor-reuse handling. Normal native callers
keep the host ABI; the existing explicit-library selector remains supported.
This selects ABI translation, not package authenticity or token authorization.
The original APKs and the Google consent policy are unchanged.

The new test loads an actual caller DSO through a self descriptor and invokes
the real adapter's resolver and release functions. It checks Bionic address
layout for APK code, host layout for native code, descriptor reuse and the
existing library allowlist. All 24 checks pass on the host and the OH board;
the parent source fails check 12 at the APK address-layout assertion. The probe
uses a numeric loopback address, so it needs no external DNS. Its `.apk` file
contains an ELF test fixture and does not validate ZIP loading or signatures.
Build/run instructions are in
[README-network-apk.md](../tests/parity/native/README-network-apk.md).

The OH adapter compiles with warnings as errors and strict undefined-symbol
linking. Its in-place Gmail overlay has a verified host rollback copy; the
mapped inode and artifact hash match. Java, the Android runtime and the other
native libraries are retained. In the original Gmail client, the previously
failing Google hostnames resolve successfully and the adapter records Bionic
address translation. No `ERR_NAME_NOT_RESOLVED` or fatal-signal marker occurs in
the fixed capture. Native OAuth issuance and official-GMS token-bearing replies
still occur. The compatibility warning stays absent in the observed UI.

An independent host-side request to Gmail's read-only profile API uses the
cached native Gmail OAuth credential. Google returns HTTP 200 and the authorized
account, while an invalid token returns HTTP 401/`UNAUTHENTICATED`. A second
metadata-only query returns a recent inbox message identifier with the approved
token and 401 with the control. Normal TLS verification is enabled, redirects
are refused, and no message bodies are requested. These checks establish Google
API acceptance of that token; they do not prove the app's own mailbox sync.
Account identifiers, message identifiers, counts, tokens and response bodies
remain private.

The original app's account settings show Sync Gmail unchecked. Enabling that
checkbox persists the automatic-sync preference and queues one matching job in
LocalSyncService, which starts the original `gmail-ls` adapter in initialization
mode. A controlled restart retains the account and enabled preference. The new
process again records address translation, token-bearing replies and no
DNS-error or fatal-signal marker in its captured log.

The restarted app then displays real recent inbox conversations. Opening an
existing welcome message renders its HTML body inside the original Gmail app.
This is direct mailbox and message-view evidence beyond the independent API
check. Screenshots and message contents remain private. A manual pull-to-refresh
gesture is exercised; no new mail is sent to create a server delta, so this does
not validate new-message arrival or prove a completed incremental refresh.

The earlier process also calls `addPeriodicSync`, which throws
`UnsupportedOperationException: periodic sync scheduling is not implemented`.
That remains a background-scheduling gap even though the app's initial mailbox
flow now works. Sync-adapter initialization completion is not independently
established by the captured callback trace. Sustained sync, incremental refresh,
periodic scheduling and full application acceptance require further checks.
No mail is sent or deleted during validation.

Certification remains 3 without an override; the check-in identity, one provider
account and explicitly approved native-consent record are preserved. Exact
sanitized results are in
[candidate79.json](../tests/google-auth/full48/candidate79.json).

## Candidate 80: real periodic sync and the standard adapter caller guard

LocalSyncService previously threw on `addPeriodicSync`. It now supports both
legacy registration and periodic SyncRequest, returns copied registrations,
removes matching registrations and jobs, and stores the next window in the
app's existing sync preferences. Duplicate registration preserves its window.
At most 64 records and one MiB of serialized data are accepted. Only an adapter
owned by the current app and a visible account may be registered; invalid
periodic flags and excessive extras are refused. The minimum interval is 900
seconds and the minimum flex is 300 seconds. Due work uses the existing real
adapter dispatch/callback path and avoids a burst of missed jobs after restart.

The first real-adapter fixture attempt exposes another failure: the standard
AOSP AbstractThreadedSyncAdapter accepts only the Android system UID. The
Westlake caller retains its application UID, so `startSync` silently returns.
A start trace alone therefore did not prove that the adapter's work or callback
ran, including in earlier Gmail captures. The fixture reaches five assertions
and times out waiting for completion on that framework.

The companion manifest source port changes only this caller guard. With
`WESTLAKE_LOCAL_SYNC=1`, it also accepts a direct call whose UID **and** PID match
the current process. The normal Android system caller remains accepted.
Different UIDs, different processes and disabled opt-in remain refused. No
system UID or Binder identity is fabricated. The guard test executes the actual
transformed AOSP method body against 28 identity/opt-in cases; the parent source
fails the required same-process case. SocketBinder already installs its verified
remote calling identity during channel dispatch; production isolation and a
new real cross-process adversarial test are not claimed by this host matrix.

The source build recompiles the three classes owned by
AbstractThreadedSyncAdapter and retains all other source-framework entries
exactly. Framework jarjar/D8 packaging and the nine-JAR, 27-artifact boot image
are rebuilt. The Westlake adapter compiles 142 sources into 296 classes. Timing
checks pass 26 cases; dispatch-state checks pass 14. The final source-port map
reproduces the exact transformed source used for compilation. Its descriptive
scope metadata was updated afterward without changing the transformation.
The source adaptation is in
[manifest PR 70](https://github.com/a2hlab/manifest/pull/70).

The fixture uses its own local account and provider, with no Google account or
network. It runs a normal sync and a periodic request through a real
AbstractThreadedSyncAdapter; both report successful completion. Registration,
refused accounts/authorities, copied return data, duplicate handling and
cancellation pass 15 assertions. A fresh process restores the retained record,
checks the saved automatic setting and removes it, passing seven more. Report
start times are newer than their verified launch cutoffs. The periodic request
uses the normal 900-second period and a full 900-second flex window, permitting
its first run immediately. The clock and minimum interval are not shortened.
This observes one periodic window, not several elapsed cycles.

The fixture runs sequentially with Gmail in the test host's UID; it does not
prove separate production app isolation. Its setup initially used the default
empty account service, then enabled the existing local-account backend for the
fixture process. Google endpoints are absent there. Original Gmail APK bytes
and data are retained, and its original launcher is restored after testing.
The fixture source and build instructions are in
[tests/periodic-sync](../tests/periodic-sync/README.md).

Gmail retains its inbox on the new framework. Its real periodic call registers
a daily 86,400-second interval with a 3,456-second flex window and writes the
registration to its app-owned preferences. A further Gmail restart restores
one registration. Its serialized record is unchanged, including the next window;
there is no new registration/reset event. Automatic sync remains enabled,
token-bearing replies still arrive, and no periodic unsupported exception,
DNS failure or fatal signal appears in the captured log. The full daily window
has not elapsed in this test.

A staging upload filled the nearly-full board. Its incomplete file was removed
after checking its exact hash and size. Two code files from a verified stopped
old probe were retired, freeing 94,015,500 bytes; exact host copies and a removal
journal remain available. No real application's data was deleted. Deployment
then completed with all overlay hashes checked and rollback files retained.

Periodic timers currently run while the application process lives. Records
resume when the app restarts and initializes LocalSyncService; this does not
start a stopped app through OpenHarmony's scheduler. OS-managed wake, idle
policies, long-duration operation, incremental mailbox refresh and the wider
Google/Facebook acceptance plan remain open. Exact sanitized results are in
[candidate80.json](../tests/google-auth/full48/candidate80.json).


## Candidate 81: Drive native OAuth formatting and live broker denial

The original Drive APK reaches the approved account and obtains real OAuth tokens
through official GMS and microG. Its five-scope file-access request starts with
one ASCII space after `oauth2:`. The old policy rejects that as an empty scope
before a Google request. The policy now accepts one ASCII delimiter at either
edge of an ordinary OAuth list, preserving the exact service sent to the provider.
Repeated delimiters, duplicate scopes, other whitespace, unapproved scopes and
server-code grammar retain their checks. The 4,096-character key limit remains.
Five authorization/session test programs pass 220 checks; the old parser fails
the new leading-delimiter case.

The user explicitly approved eight scopes for the unchanged Drive APK and the
existing test account: espresso, Photos read-only, Cloud Search, Drive read-only,
workspace_ui, supportcontent, People read-only and notifications. The operator
consent is bound to the current APK signer, account, revocation epoch and a
24-hour expiry. All prior grant entries remain byte-for-byte intact. Only the
client signer metadata pin was added; no direct microG client-token scopes or
Google Sign-In session were fabricated. Consent remains an operator-provisioned
test record; a native user consent UI is still absent.

After deployment, both hosts record token-bearing native-consent replies for
Drive, including its previously rejected scope list. An independent, read-only
host API check using Drive's own issued token gets HTTP 200 for account metadata
and file listing. The account matches the approved test account and returns no
files. An invalid-token control gets HTTP 401 on both endpoints. These calls
request no file bodies and perform no upload, file edit or deletion. This proves
backend acceptance of that token, not completion of the application's file UI.

Drive's own Files screen still says "Can't refresh", including after a retained-
data restart. Private Android logs identify a second local path: Drive also calls
`AccountManager.getAuthToken` directly. That currently uses the restricted
bootstrap provider operation, which refuses it without a separate direct-client
grant. Its document provider and native Cello fetch consequently report auth
errors/cancelled root loading. The working official GMS request and the failed
AccountManager request are distinct. The next fix must route that API through
the verified official native-auth path with the same consent checks, rather than
adding a direct bootstrap token grant. File opening also lacks an existing test
item. Application listing, file opening, actual token renewal and full acceptance
remain incomplete.

Before Drive consent was written, the live probe completed two foreign-broker
native transaction-28 tests and one chooser transaction-23 test. Every request
reached the provider and received `SecurityException`; three matching
`broker_not_granted` events were captured, with the grant file unchanged. The
probe could read its allowed account view and verify the official GMS package
first, so an unavailable channel is not counted as a successful denial. Setup
initially mixed leftover Gmail config splits, then used a private-mode probe APK
that the different-UID verifier could not reopen. Isolating the APK and making
only that public code file mode 444 resolved those setup failures without
changing APK bytes or account-data permissions. This test used candidate-80
client code and candidate-78 hosts before the parser deployment. It does not
establish separate production app UID isolation.

The candidate-81 adapter and matching boot image were then installed for official
GMS, microG, Drive and the retained Gmail runtime. Exact code hashes and host
rollback files were checked for all 116 replacements. The four test runtimes
share root-owned mode-444 code through hardlinks; private application data is
not shared this way. Original APKs/data and the prior check-in identity remain.
All three active hosts pass executable/UID checks; uncertified status remains 3
without an override. Gmail control on this adapter retains the inbox and receives
three token-bearing replies, with no fatal signal or prior DNS-name failure. This
does not validate a new-message delta. Overlapping callers, sustained use and the
wider Google/Facebook acceptance plan remain open. Sanitized evidence
is in [candidate81.json](../tests/google-auth/full48/candidate81.json).


## Candidate 82: AccountManager uses the verified official native-auth route

The unchanged Drive APK uses two token entry points. Its GMS SDK already reaches
the consent-bound official service; direct `AccountManager.getAuthToken` calls
previously reached the restricted bootstrap provider and failed. An explicit
`WESTLAKE_GOOGLE_NATIVE_ACCOUNT_BRIDGE=1` client opt-in now routes those calls to
the same original `com.google.android.gms.auth.GetToken` service, preserving its
declared service-process selection, launch/certificate checks and transaction-5
native-auth operation. Claimed packages still cannot override the originating
app. An official failure never falls back to the direct provider. The provider
and official service retain candidate-81 code and authorization.

The client translates the authenticated GMS TokenData into AccountManager's
result without caching a reusable credential or fabricating a token/expiry.
The pinned original SDK serializer identifies token field 2 and optional expiry
seconds field 3. Bounded parsing passes 107 checks, including malformed fields,
duplicate token/expiry fields, bad string encodings, missing values, overflow
and every truncated prefix. The complete 143-source/297-class adapter compiles;
runtime packaging and all nine boot-image JARs pass. Existing framework outputs
are reused only after input/compiler checks, retaining candidate-80b's sync port.

The live original APK now uses the official route for both 22- and 23-scope legacy
requests, with no direct provider token trace in the client. With the original
eight-scope consent retained, 123 broader requests fail the provider's scope
check and five already-approved requests still obtain tokens. The user then
explicitly approves the additional 19 legacy scopes. Only the two Drive scope
fields expand to 27; signer/account/epoch and original expiry remain. No direct
bootstrap token grant is added.

Google accepts the broader request and issues a token, but normalizes its scope
set to include `drive.labels.readonly`, which was not in the APK's requested
list. Independent token introspection returns HTTP 200 and identifies that sole
extra scope. The strict post-response check continues to withhold the token.
Approval for that additional read-only label permission is pending. This is a
local consent mismatch, not evidence that Google rejected the account/device.
Google documents its meaning in the
[Drive Labels scope guide](https://developers.google.com/workspace/drive/labels/guides/authorize).
The exact approved legacy list is in [Drive consent review](drive-consent-review.md).

The user separately approves creating and opening one labelled, harmless text
fixture. Its backend creation succeeds using the Drive-origin token, and a GET
returns exactly the approved 107 bytes with HTTP 200. The file is left in place;
no other upload, file edit or deletion occurs. This independent backend operation
is not a successful AccountManager callback or an application file-open result.
The original Drive UI still cannot refresh. Positive bridge token delivery,
original-app listing/opening, actual renewal and restart acceptance remain open.
Sanitized evidence is in [candidate82.json](../tests/google-auth/full48/candidate82.json).

## Board retest of Maps and the Drive fixture

The original Maps APK 26.43.00.1002020710 resolved the public search **Hong
Kong Central Station** with its Play services and Play Store connections
disabled. The map moved from North America to Central and displayed LANDMARK,
the HSBC Building, St John’s Cathedral, Hong Kong Park and local transit stops.
This used the app’s search field and a tap on the result inside Maps. The board
kept ordinary network access throughout the experiment.

| Maps test | On-board result |
| --- | --- |
| Original connected startup on client build 47 | A black screen. The UI thread was waiting on a Maps initialization future. One worker was waiting for an account ID and another was waiting for the Android service connection before it could make the corresponding Binder call. |
| Same app data and APK, both service connections disabled | The original app offered sign-in. After dismissing the install-services dialog and choosing **Skip**, it drew a labeled map. A horizontal pan revealed Greenland, Iceland and the Atlantic. |
| Restore both connections and relaunch after onboarding | Explore and labeled tiles appeared. Searching for Hong Kong Central Station produced the app’s connection-error dialog. A further launch reproduced the failure with the platform Cronet library explicitly selected for Bionic network compatibility. |
| Repeat the connected search with private diagnostic logging | Maps’ Gaia token warnings included five more recoverable authentication errors across submission. The recent log contained `BadAuthentication`. The repeated Java I/O exceptions originated in resource loading and referenced the official GMS base APK and two configuration splits. The warning does not identify which side rejected the account operation. |

The mixed-version control loaded nine verified public GMS APKs using the current
Java adapter and stopped in the CronetDynamite split with `SIGTRAP` on the older
native stack. With the matching Bionic shim and network adapter, Maps reached
Explore and drew tiles. The diagnostic log showed zero APK asset-load errors
through a fresh search for **Hong Kong City Hall**. The app still displayed the
connection error. Recoverable authentication warnings increased from 14 to 29
across the query window. This run kept the client, official GMS and microG in
their existing hosts and made no account-grant changes.

**The same query succeeded with both service connections active after changing
the account selection in Maps.** In the profile menu, **Switch account → Use
this app without an account** replaced the account avatar with the guest icon.
Hong Kong City Hall then produced a red pin on Edinburgh Place alongside City
Hall Public Library and City Gallery. The connected profile with the test
account selected had shown the connection-error dialog for that same query.
The test establishes guest place search on this board. Signed-in search and the
origin of the authentication refusal remain open.

After a client restart, Maps retained its guest selection with both service
connections active. In **Directions**, selecting Central station on Des Voeux
Road Central as the origin and entering Hong Kong City Hall as the destination
produced a blue route to the City Hall area by Edinburgh Place. The map showed
a two-minute driving route and an eight-minute walking alternative. Both
endpoints were entered manually. Physical location and turn-by-turn navigation
remain untested.

Drive’s Files tab still showed **Can’t refresh**. The original app accepted
`Westlake-Drive-validation.txt` in search, but never displayed or opened the
file. The earlier Drive API check read the approved 107 bytes. The local
returned-scope check continues to withhold the token containing the extra
read-only labels permission.

Sanitized board evidence is in [candidate83.json](../tests/google-auth/full48/candidate83.json).

## Opening the Drive fixture and a public YouTube video

The approved `Westlake-Drive-validation.txt` exists in the test account. The
saved HTTP 200 response still matches all 107 approved bytes. The exact-name
search in Drive had yielded no file, so this round opened the existing file’s
ordinary HTTPS link with the original APK. Its manifest selected
`DriveOpenUrlActivityAlias`. The process remained alive, but the screen still
showed only the system bars at 99 seconds. One sample contained 22 account-token
requests and 21 failed callbacks reporting `BadAuthentication`. The previous
run identified an extra returned permission, `drive.labels.readonly`, for a
broader request. The individual callbacks in this run do not identify the cause
of each failure. No file was uploaded during this retest. App listing, file
opening and token renewal remain open.

| Original YouTube 21.43.523 test | Observed on the board |
| --- | --- |
| Sign in with Play services connected | YouTube displayed a sign-in error. GMS received the interactive request and logged a `SecurityException`. YouTube received a canceled result. The OH service host stayed in front, leaving a black screen. Bringing the client host to the foreground restored YouTube’s controls. |
| Search and open Accounts | Searching for **Me at the zoo** stayed on a spinner. Accounts returned to the sign-in error. The source of the authentication rejection remains undetermined. |
| Disconnect the client from Play services and the Store | YouTube prompted to install Play services. Dismissing the prompt closed its app window. The process and both service hosts stayed alive. |
| Open the public watch link | The board’s implicit link resolver returned error `10103101` for the HTTPS URL. Explicitly choosing the original APK in the test launcher reached its exported URL activity. The trace then recorded 35 launches of the internal watch activity in one sample. The screen was black at 52 seconds. There was no observed playback. |

The source-APK launcher accepts an optional `WESTLAKE_TEST_VIEW_HTTPS` when
testing a specific APK. It resolves a browsable HTTPS intent against that APK’s
manifest and selects an enabled, exported activity in the bound process that
requires no component permission. An unrelated `.invalid` host was refused
before any activity launch. This tests link handling *inside the chosen APK*.
System-wide app-link registration is still unvalidated. The structured results
are in [the board evidence](../tests/google-auth/full48/candidate84.json).

## Candidate 85 fixes the YouTube watch-screen restart loop

The public link for **Me at the zoo** entered YouTube’s exported URL activity
once. The complete pre-fix trace contains 68 starts of its internal watch
activity. A second run logged the first five launch requests. The initial one
requested a new task and a clear to the top of its stack. Each of the next four
also set `FLAG_ACTIVITY_CLEAR_TASK`. Those requests came without a calling
activity token. Android defines this combination to finish the existing target
task’s activities and make the new activity its root.

Westlake now handles that reset for a local, same-affinity task in the source
APK host. Once the replacement transaction is ready, it finishes the previous
activities from top to bottom. The original YouTube APK then started its watch
activity twice in total. The adapter cleared the two previous activities on the
second start. The count held through 128 seconds. At 71 seconds YouTube showed
its sign-in error, and dismissing the dialog exposed Home. No moving video or
player-time progression appeared. The app still logged `BadAuthentication`.
On a connected restart the watch activity count again stayed at two. Disconnecting
the client from Play services and the Store brought up YouTube’s install prompt.

The final guarded build reached the original watch page after the sign-in
dialog was dismissed. It displayed **Me at the zoo**, the channel **@jawed**,
public comments and a sponsored card. The player displayed a playback error.
Tapping **Tap to retry** produced a different playback attempt identifier and
the same error. The app logged `PlaybackServiceException(PLAYER_ERROR)` for
two distinct attempts. The cause remains undetermined. Loading watch-page
metadata is verified. Account acceptance, moving video and audio remain open.

A separately signed test APK with no network or account permissions reproduced
the lifecycle failure. Without the fix, both old screens survived and the
replacement reported that it was below the root. With the fix, both screens
reported destruction, the replacement became root and received its payload,
and the three earlier lifecycle tests still passed. A fifth check requested a
separate task before a later clear. The initial fix cleared both screens. The
final build preserved them, and the board displayed **Five activity checks
passed**. The direct host still lacks separate task IDs. OH-managed activities,
document tasks and tasks with different affinities are outside this board test.
See the [structured board results](../tests/google-auth/full48/candidate85.json).

## Drive file opening and YouTube playback on the matched framework

The approved `Westlake-Drive-validation.txt` already existed. Its saved Drive
API response matched the three approved lines, including the final newline
(107 bytes). Opening the existing file’s HTTPS link again entered the original
Drive APK’s file-opening activity. At 87 seconds its log contained 23 account-token
requests, 23 failed callbacks and zero successes. At 88 seconds the process was
alive and the screen showed only system bars over black. The file remains in
place. The additional read-only labels permission is still outside the 27 approved
scopes. The client log cannot attribute each callback failure to that permission.

| Playback test | Board result |
| --- | --- |
| **Me at the zoo**, original YouTube APK | The two earlier attempts reported a player error. Across both attempts, the main process queried the platform for H.264, HEVC and five audio formats. Its trace recorded zero platform decoder configure or start events. |
| **Big Buck Bunny**, original YouTube APK | The exported URL activity and both subsequent watch-activity launches received the chosen URL. YouTube displayed a sign-in error. Searching for the title then displayed “The connection to the server timed out.” No watch page or moving video appeared. |
| Bundled H.264/AAC clips, offline test APK | All nine checks passed on each of two launches. On the second launch, the adapter, framework, boot image and four media libraries matched the hashes measured in YouTube’s process. The probe rendered three video frames to a Surface, received an image callback, and decoded 100,352 bytes of 48 kHz mono PCM. The separate AudioTrack check wrote silence. Speaker output remains untested. |

The board can decode and render these bundled clips. The original YouTube
APK also ships its own codecs. Its selected stream, codec and reason for the
player error remain unknown. Google account acceptance, YouTube playback and
opening the file in Drive remain unverified. The [board results and matching
hashes](../tests/google-auth/full48/candidate86.json) include the individual
media checks.

## Maps location permissions on the OpenHarmony board

OpenHarmony's location switch was on, but the client HAP had neither approximate
nor precise location permission. In the original Maps APK, Android's
`checkSelfPermission` reported both granted. The location adapter withheld fixes
and the map stayed at continental scale when the tester tapped **Your location**.

Westlake now checks the current HAP grant and the APK's declared foreground
permission. Android fine location maps to `ohos.permission.LOCATION` and coarse
location to `ohos.permission.APPROXIMATELY_LOCATION`. The Java and C++ mappers
also preserve that distinction when converting back to Android permission names.
An initial build crashed before Maps drew its screen. The tested build can read
the original APK's parsed manifest directly during a permission check.

With the same APK and host grants, Maps rendered its tiles, reported both
permissions denied, and displayed **App permission denied** when the tester
tapped **Your location**. The result repeated after a process restart. The
**Settings** button sent Android's app-details intent. The resolver found no
handler, and no permission panel opened. OpenHarmony lists the shared client
HAP; Maps has no separate installed HAP in this configuration.

These tests verify the denied-permission case. A positive location fix,
approximate-only delivery and independent user grants for apps sharing a host
still need board validation. The [board evidence](../tests/google-auth/full48/candidate87.json)
includes both app runs, the deployed hashes and the result of the Settings action.

## OpenHarmony Settings from the original Maps APK

Maps 26.43.00.1002020710 now opens its own OpenHarmony app details when the
tester selects **Go to settings**. The board displayed **Westlake Maps test
host** on three app-initiated attempts. One came from onboarding and two came
from **Your location** after separate restarts with both Play connections
configured. All three had a matching acknowledgement from the native host.
The app-details page showed Notifications and Network. To reach the location
control on this board, the tester opened **Settings → Privacy and security →
Permission manager → Location → Westlake Maps test host**. The selected option
was **Deny**. The other choices were **Ask each time** and **Allow only while
in use**. No permission was changed during these runs.

The first connected launch with clean Maps data stayed black during observation.
Its UI thread waited on a startup future. Two workers waited for an account ID
and a remote service connection. With only the Maps service connections disabled,
the tester dismissed the install prompt and selected **Skip** in Maps. Labeled
map tiles appeared after both connections were restored and again after a second
Maps restart. The tests cover this onboarding sequence and the settings button.
Signed-in acceptance, a first launch with clean data and a live location fix
remain open.

The Maps process received Play services connection callbacks on both connected
restarts. Each had status 0 (success) and status 9 (SERVICE_INVALID). No such
callback appeared during disconnected onboarding. The sampled replies establish
that some service connections worked and others were refused. The cause of
the refusal and the Google account outcome remain unknown.
[Google Play services connection codes](https://developers.google.com/android/reference/com/google/android/gms/common/ConnectionResult)

Maps, Drive, official Play services and microG ran concurrently under four
different Linux UIDs. The Maps data and host-control directories were owned by
the dedicated UID with mode 0700. The test used a signed development HAP paired
with the original, unmodified APK. Cross-UID file access and production isolation
still need separate tests. The structured results include the deployed hashes
and the outcome of each attempt. [Board evidence](../tests/google-auth/full48/candidate88.json)

### Temporary approximate location grant and revocation

Selecting **Allow only while in use** granted approximate location to the Maps
host. Its precise-location switch stayed off. The shared host running Drive had
neither permission before, during or after the test.

| Check | Observed on the board |
| --- | --- |
| Maps after a restart with the grant | The APK saw coarse access granted and fine access denied. |
| Your location | OpenHarmony accepted two low-power requests. Neither delivered a first fix during the 48-second observation window. GNSS stayed off. |
| Restore Deny and restart Maps | Both Android checks returned denied. The adapter withheld two requests and started none. The native permission manager also showed Deny. |

The installed network-location library references parameters for its provider
bundle and ability. Both returned error 106 (parameter not found). During the
retest, the board’s network-location service also logged **get service name
failed!** while processing requests. That identifies a provider configuration
problem on this build. The system event does not name Maps as its caller, so
we cannot attribute its missing fix to that event alone. The board has demonstrated an independent
approximate grant and its revocation. Delivery of a physical location remains
open. [Board evidence](../tests/google-auth/full48/candidate88.json)

## Drive lists and opens the test file

The operator approved the additional read-only Drive labels scope. Westlake
added it to the original Drive APK’s two local consent lists, bringing the
total to 28. The signer, account, revocation epoch and original expiry were
preserved. The earlier run with 27 scopes had reached Drive’s file-opening
activity and recorded 23 failed account callbacks in its observation window.

| On-board test with 28 scopes | Original Drive APK result |
| --- | --- |
| Search in the running app | The Files screen initially still said **Can’t refresh**. Searching for the existing fixture returned its filename. Selecting it opened a preview with all three expected lines. |
| Restart Drive with the same app data | **Files → My Drive** listed the fixture. Selecting it opened the preview and displayed the same three lines. |

The observed client windows each contained two successful AccountManager replies
and zero failed replies. The file already existed. This test made no new upload,
edit or deletion. It verifies listing, opening and retention across a Drive
process restart. The restart could reuse cached file data. Token renewal after
expiry still needs a separate observation. [Board results](../tests/google-auth/full48/candidate89.json)

## Uber trip link reaches sign-in

The original Uber rider APK 4.653.10004 declares an HTTPS handler for
`m.uber.com/looking`. On the board, choosing that APK for a link passed the
complete URL to its exported RootActivity. The pickup and destination were
fixed public places in San Francisco. Uber’s dedicated HAP had neither
approximate nor precise location permission. The original Drive APK and both
service hosts stayed alive under separate UIDs.

On fresh app data, Uber displayed **Go anywhere with Uber**. **Continue with
Google** opened **Sign in to continue to Uber** and asked for an email or phone.
The trip link did not produce a fare or an authenticated rider session in this
run. No ride was requested. This test selected the APK explicitly. System-wide
HTTPS link dispatch remains a separate test. [Uber link format](https://developer.uber.com/docs/riders/ride-requests/tutorials/deep-links/introduction)
and [board results](../tests/google-auth/full48/uber-trip-link89.json).

## Video formats Android apps can select

In the two recorded attempts to play **Me at the zoo**, YouTube queried the
Android decoder list before displaying a player error. Its trace did not
identify the selected stream or record a platform decoder starting. To test
the list itself, a separate Android process on the board loaded the same
media shim used by YouTube. A second process ran the same probe with the
rebuilt shim. Both used the same Android framework. Neither needed an
account, a network connection or a foreground window.

| Format | Previous Android capability | Rebuilt Android capability | Local MP4 decoding |
| --- | --- | --- | --- |
| H.264 Baseline and Main | Both rejected at level 5.1 | Both accepted at 5.1 | Each 320×240 clip returned all 12 frames on both builds, with matching per-clip checksums. |
| H.264 High | Accepted at 5.1 | Accepted at 5.1 | All 12 frames matched across builds. |
| HEVC Main | Accepted up to main-tier 5.1 | No matching profile in the preferred OH decoder’s list | Neither Android build returned a frame from the 8-bit clip. |
| HEVC Main 10 | Rejected at main-tier 5 | Accepted at main-tier 5, rejected at 5.1 | Neither Android build returned a frame from the 10-bit clip. |

Westlake now translates each profile and maximum known level reported by the
preferred OpenHarmony decoder. It uses the same profile numbers for extracted
tracks and decoder capabilities. The board’s H.264 software fallback advertises
level 6.2. Westlake exposes the preferred decoder’s 5.1 maximum. The H.264
clips themselves were level 1.1.

A separate test passed the 10-bit clip directly to OpenHarmony’s video decoder.
It started and reported error 6 with zero output frames in default buffer,
NV12 buffer and off-screen surface mode. OpenHarmony decoded the 12-frame
H.264 control. The rebuilt shim corrects Android’s format queries. HEVC
playback and online playback in the original YouTube APK remain open.
[Android test source](../tests/parity/VideoDecoderProfileProbe.java) ·
[Board results and fixture hashes](../tests/google-auth/full48/candidate90.json)

## Live app and service isolation on the board

Seven read-only samples over 206 seconds followed the original Drive, Maps and
Uber APKs, official Play services, the private microG provider and the Play
Store instance configured for these clients. All six Android processes kept
their original process identity, had distinct Linux UIDs and occupied distinct
mount namespaces. Each shared its UID with its own native HAP. SELinux was
enforcing and all twelve processes used the `normal_hap` domain.

| Kernel or window observation | Android processes | Corresponding native HAP processes |
| --- | --- | --- |
| System call filter | Every sampled thread reported seccomp mode 0. | Every sampled thread reported mode 2. The main thread had two filters. |
| Privileges | Effective Linux capabilities were zero. All main threads had `NoNewPrivs=1`. | Effective capabilities were zero. All main threads had `NoNewPrivs=0`. |
| CPU scheduling | All six main threads were in the `top-app` CPU and cpuset groups while the screen was locked. The launcher selects this group before applying the APK's user identity. | All six main threads were in the root CPU and cpuset groups. |
| Window ownership | Drive kept four registered windows, Maps two and Uber three. Their window IDs stayed attached to the same processes. | The OpenHarmony lock screen held focus at every sample. |

Drive, Maps and Uber were configured for the same official GMS and Store
instances. Each endpoint had one kernel listener, and the expected service
process held its listening file descriptor. Drive and official GMS also had
the private provider endpoint configured. The three clients' sampled account
request counters did not change during this interval. The observation verifies
which services were reachable and which processes owned the windows. Concurrent
sign-in replies and interactive focus handoffs still need a live test.

Two older test processes, a second official GMS and a second Play Store,
were still running under the private provider's HAP and UID. Their staged base
APKs matched their recorded hashes. The active clients selected a different
Store instance running under its own UID. This board therefore had shared-UID
test processes alongside the six distinct active roles. Cross-app data access
was not exercised.

In a separate headless ART process on the board, enabling Westlake's existing
Android 15 seccomp option installed the filter across threads. The kernel
reported mode 2 with one filter. The same three H.264 clips returned all
36 frames with matching checksums with the option on and off. The six live
Android processes continued to run with mode 0. CPU usage, battery drain and
original app behavior with the filter enabled remain unmeasured.
[Board observations, hashes and controls](../tests/google-auth/full48/candidate91.json)


## Dedicated hosts for the eight APKs

OpenHarmony now has a separate native test HAP for each Android package in
the plan. Maps and Uber had already run under their dedicated hosts. Six
additional hosts were built from source, development-signed and installed.
The installed HAP hashes matched the local artifacts, and OpenHarmony assigned
all eight different UIDs.

| App | Dedicated host UID | Original APK has run under this UID |
| --- | ---: | --- |
| Maps | 20010062 | Yes |
| Uber | 20010063 | Yes |
| YouTube | 20010064 | Pending |
| Drive | 20010065 | Pending |
| Gmail | 20010066 | Pending |
| Spotify | 20010067 | Yes. The original APK opened and received Google's activity result. |
| Wattpad | 20010068 | Pending |
| FotMob | 20010069 | Pending |

All eight hosts have network access. Microphone, approximate location and
precise location were denied for all eight when checked. The six new UIDs
had no running process after installation. The Drive instance that passed the
file test remained alive under the earlier reusable client UID 20010060.
Its app data and account results have not been transferred to the new host.
[Installed HAP hashes, APK pins and permission checks](../tests/google-auth/full48/candidate92.json)

## Spotify account selection under its dedicated OpenHarmony host

Spotify 9.1.94.1929 started with fresh app data under UID 20010067. Tapping
Log in and choosing Google opened the official Google sign-in activity, but
OpenHarmony left Spotify in front. Bringing the Google host forward with the
OpenHarmony app launcher exposed the chooser. Selecting the existing Google
test account delivered Android `RESULT_OK` (−1) to Spotify.

The Google host then foregrounded the shared test host. Its configured return
route for Spotify still names `org.westlake.authclienthost`. Spotify's dedicated
native host supports opening its own settings, while the sign-in window handoff
is implemented for the original pair of shared hosts. Bringing Spotify's
dedicated host forward manually restored the native host window. The Android
process was alive, but WindowManager listed zero windows owned by that process
and its next screen could not be read. The successful Android result therefore
establishes account selection only. [Board observations](../tests/google-auth/full48/candidate93.json)

A fresh run of the same APK and runtime in the shared host reached the Spotify
login screen. Choosing Google automatically focused a full-screen window owned
by the official Google UI process. The center of the board display capture was
black. The account row could not be verified, so the tester made no selection
on that run. The earlier shared-host run remains the evidence for the visible
Spotify signup form. [Shared-host control](../tests/google-auth/full48/candidate93.json) · [Earlier signup result](../tests/google-auth/full48/candidate51-followup.json)

## Selecting the current native host window

The previous host wrote just its most recent lifecycle event. A foreground
event had no window ID, and a terminated process could leave a window ID on
disk. The updated host records its PID and current main window together,
including across foreground changes, and clears the ID when its window stage
is destroyed. Before launching the APK, the manifest tool checks the main
window against OpenHarmony WindowManager and rechecks the host PID. With
older HAPs it can recover the host's unique live main window. Both the
FotMob and Wattpad captures yielded the expected client and service windows
when replayed through the updated launcher. [Launcher change](https://github.com/a2hlab/manifest/pull/70) · [Checks and Spotify host build](../tests/google-auth/full48/candidate94.json)

To examine the return to the originating app, three native HAPs were installed
under different UIDs. They request no permissions and use no accounts.
OpenHarmony failed to start the first caller and also failed to start the
previously installed Wattpad host. The system log traced the launch failure
to SceneBoard's pending-session activation. The board subsequently stopped
answering over USB and TCP. The new Spotify HAP was built and signed locally.
Its live deployment, the two-caller return test and Spotify's authenticated
session remain open. [Board observations](../tests/google-auth/full48/candidate94.json)

## Returning the service window to the dedicated app

The dedicated client host can now open the shared service window and keep
access to its own OpenHarmony settings. The service uses the calling bundle
and ability from the incoming native Want. A configured return to a dedicated
host must match both the Android caller package and the native host that
opened the current window. For a Spotify run, the service must configure
Spotify's dedicated host as the return destination. The previous shared-host
entry remains appropriate for a run using the shared client host.

In a development-machine integration test, two clients sent six requests
through the compiled Java adapter and the native host's JavaScript using real
mailbox files. The test simulated the OpenHarmony ability calls. After the
second client opened the service, a late result for the first client caused no
window change. The second client regained its window. The first client then
opened the service again and regained its window on return. No account was
involved. The adapter, Android boot image, and signed Spotify, Wattpad, and
service hosts built locally. Both board connections were offline. A live
return after selection or cancellation in the original APKs remains open.
[Builds and test results](../tests/google-auth/full48/candidate95.json) · [Host configuration](../tests/host-window/README.md)

## Which media functions could YouTube call?

The original YouTube APK contains both Java and native playback code. On the
recorded public-video attempt, the main process queried the Java bridge for two
video capabilities and five audio capabilities. That bridge recorded zero
configure or start calls. The saved trace did not identify the path responsible
for the player error. [Board run](../tests/google-auth/full48/candidate85.json)

YouTube's native `libgoogle3.so` imports 33 codec and format functions. Five
are absent from the exact `libmediandk.so` on the board. All five are exported
by the hash-matched `libwebview_bionic_shim.so` on the preload list for the
three recorded launches. That gives the linker a second possible provider.
The saved evidence does not establish which functions the player called. The
APK also ships software decoder libraries. [Artifact hashes, symbol names and Java call sites](../tests/google-auth/full48/candidate96.json)

Across the eight original apps, 62 base and split APKs contained 116 libraries
under `lib/arm64-v8a/`. Three libraries directly import functions from the
codec, format or extractor API families. YouTube imports 33, Spotify imports 23,
and Gmail imports 19. Only YouTube imports any of the five at issue. This count
covers strong ELF imports. Java calls and runtime lookup can also reach media
services. [Per-app inventory](../tests/google-auth/full48/candidate96.json)

The standard native builder now produces a hash-recorded `libmediandk.so`.
Its three codec factories still return null. A diagnostic build can report the
first request for each kind of factory using fixed labels. The host test made
768 calls, including concurrent calls and the caller-specific variants. Only
the process with tracing enabled emitted the three expected messages. The
OpenHarmony build was staged locally with the other 58 libraries from the
recorded composition. Both board transports were offline.

For the next public-video run, pass the OHOS native builder output to
`--runtime-library-build`, select `--runtime-library libmediandk.so` and add
`--runtime-env WESTLAKE_MEDIA_CODEC_TRACE=1` to the per-app launcher. Confirm
the running artifact hash and check for advancing playback alongside the Java
and native events. A native factory event establishes a codec request. A
successful app test requires the original player's video to advance.

## Native window return after a board restart

Two native programs requested a result from the same OpenHarmony ability. The first
received its assigned result code 41 and the second received 42. WindowManager still
showed the service in front after each return. The caller needed a separate action to
bring its window forward.

The dedicated host had also stopped processing window requests. On a cold launch,
a diagnostic Wattpad host recorded that the bridge was enabled and its first interval
handle was the number zero. The host treated zero as an absent timer, so the request
remained unread. The fixed host checks for an undefined handle when it starts, polls
and clears the interval.

The signed Spotify, Wattpad and service test hosts were updated in place under their
existing UIDs. Each client opened the service window. With Wattpad as the current
caller, a stale return to Spotify was rejected and the service kept focus. A return
to Wattpad focused Wattpad. Spotify then opened the service again and regained focus
on return. WindowManager confirmed the destination on all six steps. A test driver
submitted the window requests directly to each host's mailbox. [Board steps, host hashes and regression test](../tests/google-auth/full48/candidate97.json)

The original Spotify 9.1.94.1929 APK also ran after reboot. As a control, its process
had no connections configured to the Google or Play Store service processes. Pressing
Google reached the SDK sign-in activity, returned result code zero and displayed
“Something went wrong. Try again.” A successful sign-in through the running two-path
service pair and a Spotify account session still need a live test.

On the newly signed Spotify host, the same original APK opened both Facebook SDK
activities. Westlake's browser fallback navigated to Facebook’s HTTPS OAuth page.
The board showed the mobile number or email and password fields, and the focused
window belonged to Spotify. This was an app-owned WebView. No password was submitted
during this observation. [Original APK and Facebook form](../tests/google-auth/full48/candidate97.json)

## Where Spotify’s Google sign-in stopped with official Play services

With fresh app data, Spotify 9.1.94.1929 connected to the original Play services and
Play Store processes. Each ran under its own OpenHarmony UID. All 17 base and split
APKs matched the locked inputs and passed Android signature verification. This run
used official Google services alone. It measured the app-to-Google window handoff.

Two launcher settings changed the outcome. Omitting the expected Play services APK
hash made the peers reject the connection. Supplying its verified hash restored it.
With Play services limited to its main process, Android canceled Spotify’s request
to open the sign-in activity. Enabling manifest-declared processes for Play services
let Google’s UI process start. OpenHarmony moved focus there automatically when
the tester selected Google in Spotify.

Google’s account picker and add-account activities started. The fresh profile showed
“Checking info” for at least 7 min 57 s, including after a Back command. No email or
password prompt appeared, and Spotify received no activity result in that interval.
A separate HTTPS check from the board verified the server certificates and received
HTTP 302 from Google Accounts and HTTP 204 from Google’s connectivity endpoint.
The background Play services process twice reported that the Google Services
Framework settings provider was missing. The UI process logged no matching lookup
failure. These observations leave the reason for the wait open. Neither a device
certification decision nor a Spotify session was observed. [Board evidence](../tests/google-auth/full48/candidate98.json)

The same board opened the public YouTube video “Me at the zoo” in the original
APK with the instrumented media library loaded. Its watch page displayed the
title and comments. The player displayed an error and offered “Tap to retry.”
The same error remained visible 55 seconds after the tap. Video and audio
capability queries ran. The trace recorded no codec configure or start call,
and none of the three instrumented native codec factories reported an
unavailable operation. The test did not identify which component rejected
playback. [Public-video retest](../tests/google-auth/full48/candidate98.json)

## Why Back failed during Google account setup

The Google window was already displaying “Checking info” when the tester pressed
Back. Westlake called the activity’s Back handler from its input worker. Android’s
fragment manager requires its main thread and threw an exception. The input bridge
now queues key-down and key-up in order on the Android main looper. The rebuilt
bridge also retains the previously deployed bridge’s private text-input handling.

With the rebuilt bridge, Back dismissed the original Spotify login menu. The tester
opened it again and selected Google. In Google’s setup activity, both Back events
ran on the Android main looper and the previous exception did not recur. Google
continued to display “Checking info.” Spotify received no sign-in result during
the observation.

A second change in this run enabled Westlake’s existing native network adapter for
the networking library shipped in the original Google Play services APK. Earlier,
the Google background configuration service had logged a hostname-resolution
failure. In the retest the adapter recorded successful lookups of Google domains
from Play services. Setup still did not reach an email or password field. Its
dependency at that point remains unidentified. [Input, window and DNS evidence](../tests/google-auth/full48/candidate99.json)

## TikTok welcome animation on the OpenHarmony board

The original TikTok 47.1.4 APK contains a 6.1-second, 500 × 500 HEVC welcome
animation with square pixels and no rotation metadata. A decoded frame shows
an upright handset. The board screenshot shows the welcome slogan upright in a
1200 × 1920 activity. It did not capture a moving frame of the welcome clip,
so the displayed width, height and rotation of the animation are still unknown.
[App and screen measurements](../tests/google-auth/full48/candidate101.json)

In a separate board test, OpenHarmony’s hardware decoder accepted the original
clip and then returned error 6 before its first output frame. Both memory
output and surface output produced this result. The public codec API offered
no software HEVC decoder on this image. A standalone software decoder built
from OpenHarmony’s FFmpeg source decoded all 122 frames on the same board.
The decoded pixels matched the host reference byte for byte. This decoder ran
outside the app. [Decoder measurements](../tests/google-auth/full48/candidate102.json)

The Westlake bridge now delivers terminal decoder failures through Android
`MediaCodec.CodecException`. On the original clip, both input and output
dequeue reported the error, and callback mode delivered it on the requested
handler. The previous bridge waited past the 12-second callback deadline. As a
control, an H.264 transcode of the same animation delivered all 122 frames
across an output-surface change. On-screen sizing still requires a capture from
the original TikTok player. [Board results](../tests/google-auth/full48/candidate102.json)

# Spotify Google sign-in through delegated microG authentication

The build-11 board run passed the Google activity result back to the unmodified
Spotify APK and reached Spotify's **Create account / date of birth** form.
The test stopped there under the user's instruction not to create a new Spotify
account. This establishes progress through the real Google sign-in flow, not a
completed Spotify account login, playback, or general Play services compatibility.

Google returned fresh Spotify-scoped OAuth tokens and server authorization codes
to microG. Official GMS consumed those results through a new, opt-in adapter. The
reusable Google credential remained in microG's account store; no credential was
entered or copied into official GMS. These processes still share native UID
20010056, so this experiment does not establish production isolation.

## What failed and what changed

The [previous boundary investigation](google-auth-boundary-validation.md) found
that native GMS read a Spotify token-cache key, then fell back to a withheld
reusable credential. A token-cache key identifies a request but cannot prove who
made it: the immediate caller at this point is another GMS process.

The new route retains the original app's verified APK identity at the interactive
activity entry. Native GMS cache reads are routed to that live UI operation,
which presents the original app identity and an account-selection witness to
microG. MicroG independently verifies the original process and APK through its
root-owned spawner before making the scoped Google request.

Two further errors were found during board testing:

- The first private grant used Spotify's `default_web_client_id` resource, which
  differs from its compiled Google login client. Pinning the client ID actually
  used by the tested APK fixed the server-code policy denial. No wildcard client
  grant was introduced.
- Both authorized requests then failed in DNS with
  `NetworkOnMainThreadException`, before receiving a Google response. SocketBinder
  can dispatch a nested call on an original waiting thread. Build 11 moves
  `AuthManager.requestAuth` to a bounded network worker with a 90-second caller
  timeout. Peer and selection checks remain on the authenticated inbound thread,
  including checks after the worker returns. Network permission is scoped to that
  worker; no process-wide StrictMode relaxation was added.

[Build-10 failures](../tests/google-auth/delegation-validation/microg10-network-failure.events.txt),
[build/source hashes](../tests/google-auth/delegation-validation/build11.json).

## Delegation contract

`WESTLAKE_GOOGLE_AUTH_DELEGATION=1` requires enforced channel identity and private
logging. Both official GMS and microG need the flag. Existing separate auth/Play
endpoints, pinned provider APK/spawner, and remote activity settings remain
necessary. Spotify connects to official GMS and Store; it has no direct microG
auth connection or direct token grant.

The additional checks are:

1. Only the pinned official GMS package/current signer may broker a request.
   Other apps cannot invoke the same-app GMS UI read endpoint or provider
   delegation endpoint using their own verified identity.
2. The original package, live process and APK signing lineage are independently
   verified against the original spawner. The current certificate authorizes the
   private grant; the verified legacy SHA-1 certificate is supplied as Google's
   Android `client_sig`. Cache-key text cannot override either identity.
3. The exact package/current signer, OAuth scopes, server client ID and supported
   options must match a private provider policy. Unknown or duplicate options,
   unsupported cache formats and unpinned opaque request options fail closed.
   Email/profile options also require the corresponding scope grant.
4. There must be exactly one matching live interactive operation and one selected
   Google account. A connection-bound Binder witness checks the selection before
   and after network I/O. Selection expires after ten minutes and is cleared on
   activity completion/death. The private policy is read again before returning
   a result, so an in-flight policy revocation is checked.
5. Fresh results are requested for every delegated token read because this cache
   boundary does not expose the original freshness flags. Authorization codes
   are not replayed from the adapter's local result map. Returned expiry comes
   from Google; it is not fabricated. Previously granted extra scopes are not
   automatically requested, and returned scopes must fit the private allowlist.
6. The UI keeps at most 16 issued-result records for expiry/scope metadata and
   exact-token invalidation. Records are cleared on completion. Invalidation can
   remove only a matching cached result for the verified origin and live flow.
   Arbitrary user data and reusable credential reads remain withheld.

The private policy adds these entries alongside the existing direct-client grants
(public application identifiers only; this is not an account/token file):

```properties
delegation.broker.sha256=7ce83c1b71f3d572fed04c8d40c5cb10ff75e6d87d9df6fbd53f0468c2905053
delegate.com.spotify.music.sha256=6505b181933344f93893d586e399b94616183f04349cb572a9e81a3335e28ffd
delegate.com.spotify.music.client_id=1046568431490-ij1gi5shcp2gtorls09frkc56d4mjbe2.apps.googleusercontent.com
delegate.com.spotify.music.scopes=openid,email,profile,https://www.googleapis.com/auth/userinfo.email,https://www.googleapis.com/auth/userinfo.profile
```

The file is `westlake-auth-proxy/grants.properties` under microG's private app
data, owned by its UID with no group/other permissions. A local scope grant plus
account selection does not replace server consent: `remote_consent` is withheld,
not converted into a successful token. This version has no consent-UI relay.

## Board evidence

| Operation | Build-11 observation |
| --- | --- |
| Actual Spotify account selection | Official Google account picker worked |
| Delegated OAuth requests | Four fresh results, all with token and expiry present |
| Delegated server-code requests | Three fresh results, all with token and expiry present |
| Google consent response | No `remote_consent` in these seven results |
| Invalidation | One exact cached result removed |
| Official GMS to Spotify result | One `RESULT_OK` (`-1`) sent and received |
| Spotify next screen | Create account / date of birth; no Next or signup submission |
| Direct microG control | Forced-refresh `ac2dm` token returned, request 295 |
| Foreign probe calling GMS UI delegation endpoint | `SecurityException` |
| Foreign probe acting as provider delegation broker | `SecurityException`, broker not granted |
| Probe claiming Spotify in official token package options | `SecurityException` |
| Reusable credential entry count | microG: 1; official GMS/Store/Spotify/probe: 0 |

Seven token results are repeated reads in one Spotify flow, not seven distinct
working APIs or seven independent sign-ins. The adapter records presence and
kind only. Spotify's server-side authorization-code redemption was not separately
instrumented, and a Google status field was not independently decoded on success.
The observed evidence is the actual Google token responses, `RESULT_OK`, and
Spotify's subsequent signup form.

[microG events](../tests/google-auth/delegation-validation/microg11.events.txt),
[official GMS events](../tests/google-auth/delegation-validation/official11.events.txt),
[Spotify events](../tests/google-auth/delegation-validation/spotify11.events.txt),
[UI observation and limits](../tests/google-auth/delegation-validation/spotify11.json),
[control](../tests/google-auth/delegation-validation/probe11-control.json),
[UI denial](../tests/google-auth/delegation-validation/probe11-delegation-ui.json),
[broker denial](../tests/google-auth/delegation-validation/probe11-delegation-provider.json),
[package-spoof denial](../tests/google-auth/delegation-validation/probe11-spoof-package.json),
[live runtime hashes and credential counts](../tests/google-auth/delegation-validation/live11.json).

The source-built six-button probe is manifest input `google-official-auth-probe-v4`,
signed with the existing approved development key. APK SHA-256 is
`475755430db28ad1012bd9dc02871d29af7f8c9f390848987a10ba75251b9979`.
The probe was stopped after testing; Spotify was left at the signup boundary.
The delegation policy's 36 host checks, eight existing direct-policy checks,
boundary-trace redaction check, six bootstrap Python tests, Java compilation,
runtime packaging, boot generation and board framework preload passed.

## Reproduction and remaining limits

Build the adapter from this checkout using manifest `build_westlake_java.py`,
`package_framework_runtime.py`, `build_framework_boot.py` and
`probe_framework_vm.py`, following the prior board setup. Stage/launch apps
sequentially when sharing the input cache. Preserve microG account data on-device,
then deploy fresh microG, official GMS, Store and Spotify runtime directories.
When the provider's spawner changes, update its pin in GMS and Store as well.
Verify each live `adapter-runtime-bcp.jar` against the recorded build-11 hash.

Reusing an old runtime directory during this investigation caused UI render
aborts with `Failed to acquire physical displays for WCG support!`; fresh
deployment restored rendering without a native graphics change. A separate
preliminary run had stale provider pins, and another lacked a live Store host,
causing Spotify's SDK to choose `NO_IMPL`/12500 before auth. These setup failures
are not Google token rejections and are not counted as fixed renderer behavior.

The bridge is tied to the tested GMS cache-key protocol and Spotify signer/client
configuration. General consent/recovery, additional OAuth options, aggregate
`SCOPES:...^^snowballing^^` metadata, background refresh, account switching,
restart durability and cancellation/revocation during a delegated HTTP call
remain unvalidated or incomplete. The earlier activity fixture tested activity
lifetime separately; it does not cover all new authentication races. Worker
timeout cancels the task, but HTTP interruption behavior has not been validated.

Input used the private process mailbox. Full OH foreground/task/physical-touch
acceptance, production per-app UID/SELinux confinement, Google certification,
Play Integrity and HK corpus acceptance are not established. Account-bearing
screenshots, credentials, tokens, cookies, proprietary disassembly and unrestricted
logs are excluded from git. The saved screenshot hash identifies the private
observation without publishing it.

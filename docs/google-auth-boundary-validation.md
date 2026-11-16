# Official GMS auth boundary: continued board validation

This report preserves the build-08 baseline. The subsequent
[delegation implementation and build-11 replay](google-auth-delegation-validation.md)
returned Google tokens and an official `RESULT_OK` to Spotify, then stopped at
Spotify's new-account form. The failures and missing contracts below describe
the earlier implementation.

The activity fix survives a new official GMS host launch, and Spotify again
reaches Google's account picker and credential-recovery screen. Native official
authentication still fails while the direct microG control succeeds. This run
adds concrete evidence about the cache key, caller identity and required OAuth
options; it does not implement token mediation or establish Spotify login.

## Measured operations

| Operation | Board result |
| --- | --- |
| Build-07 forced microG `ac2dm` control | Token returned, provider request 1841 |
| Build-07 official Google SDK and token Binder | Both returned `BadAuthentication` |
| Diagnostic build-08 forced microG `ac2dm` control | Token returned, provider request 2071 |
| Build-08 official Google SDK | `UserRecoverableAuthException`, `BadAuthentication`, recovery Intent present |
| Build-08 official token Binder | No token; `BadAuthentication`; recovery Intent present |
| Same approved probe claiming `com.spotify.music` in both package options | `SecurityException` from the official token operation |
| Spotify after restarting official GMS | Account picker, account selection and redirect chain worked; "Verify it's you" appeared |
| Explicit Back from Spotify's recovery UI | Exactly one canceled result, then Google status 12501; no completed login |

The real probe APK/signer and channel identity remain the probe's during the
package-spoof test. Its account lookup succeeds first. Only the two package
options passed to the official token Binder differ from the normal operation.
This validates rejection of this specific spoof attempt, not every possible
delegation or Binder attack.

[Control](../tests/google-auth/boundary-validation/probe08-control.json),
[SDK](../tests/google-auth/boundary-validation/probe08-sdk.json),
[Binder](../tests/google-auth/boundary-validation/probe08-binder.json),
[package-spoof test](../tests/google-auth/boundary-validation/probe08-spoof-package.json),
[verified probe identity](../tests/google-auth/boundary-validation/probe08-identity.events.txt).

The narrow probe interval contains one microG token request: the explicit
positive control. Subsequent SDK/Binder calls cause account listing and official
GMS cache/credential reads, without corresponding microG token requests.
[Provider events](../tests/google-auth/boundary-validation/probe08-microg.events.txt),
[official persistent-process events](../tests/google-auth/boundary-validation/probe08-official-persistent.events.txt).

## What the new trace establishes

The pinned official GMS APK is version `244034029`, base SHA-256
`1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1`.
Inspection of its `tku`/`tns` cache-key builder shows a package, signer and scope,
with optional encoded request parameters. Expiry and granted-scope metadata
have separate keys. This is a version-specific internal format, not a stable
Android API or an authorization capability.

The diagnostic adapter confirms that the actual Spotify flow reads:

- A Spotify-owned OAuth cache key with a SHA-1-shaped signer field.
- A Spotify-owned `oauth2:server:client_id:...` cache key with request options.
- Its reusable account credential after the cache read is withheld.

The classifier establishes only field categories and signer shape. It does not
assert that a string's signer is independently authenticated. It never records
the raw key, client ID, scope list or account value.

At those Spotify cache reads, official GMS persistent PID **3469** sees Binder
caller PID **3063**, the official GMS main process, on a same-app connection.
The original Spotify process is **26968**. A later credential read comes from
GMS UI PID **4361**. The account-picker request identified Spotify at the UI
entry, but that original PID is not the immediate caller at the auth cache.

The standalone probe differs: its cache read reports Binder PID **3469** (self),
while SocketBinder still has a foreign inbound peer context. Thus simply using
`Binder.getCallingPid/Uid()` at this late point is insufficient in both measured
paths. The trace does not prove that all original context has disappeared from
GMS internals; it identifies what this adapter boundary currently exposes.

[Spotify trace and process IDs](../tests/google-auth/boundary-validation/spotify08.json),
[persistent-process trace](../tests/google-auth/boundary-validation/spotify08-persistent.events.txt),
[UI trace](../tests/google-auth/boundary-validation/spotify08-ui.events.txt).
The wider Spotify interval also contains background GMS/Store traffic. Its
provider token events must not be attributed to Spotify merely by timestamp.

## Consequences for the token-only implementation

The current proxy supports an exact package/signer/scope grant for its direct
client. Its token response carries the token and limited request diagnostics;
it does not carry the full native GMS expiry, granted-scope or consent contract.
Its request options accept only its own package and forced-refresh flag.

A compatible mediation path therefore needs these contracts before it can
complete this native Google Sign-In flow:

1. Capture an independently verified original app identity and bind it to the
   auth request, retaining that association across GMS process and UI hops.
   A package/signature embedded in a cache key alone is not authority.
2. Authorize the original package, verified signer, account, exact scope,
   server client ID and supported request options. An account-picker selection
   is not a blanket grant for arbitrary token scopes.
3. Preserve the distinct OAuth access-token and server-authorization-code
   contracts. Carry supported request options and consent/recovery outcomes;
   do not reduce every response to a reusable bearer-token string.
4. Preserve expiry, invalidation and revocation, including the freshness and
   single-use requirements of authorization codes. Returning a token from
   `peekAuthToken` without its metadata would be an incomplete cache adapter.

The **actual pinned microG APK** contains `AuthManager` option fields/setters,
expiry/invalidation methods, and a server-auth-code helper using the matching
server-client scope format. That is supporting static evidence, not a successful
Spotify exchange or proof of Google backend acceptance.
[APK capability inspection](../tests/google-auth/boundary-validation/microg-apk-capabilities.json).

No reusable credential was supplied to official GMS, no Spotify token grant was
added, and no account signup was performed. The remaining work is a token and
delegation protocol, rather than another activity-launch fix.

## Reproduction, provenance and limits

Use the current adapter sources with `WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE=1`
and `WESTLAKE_GOOGLE_PRIVATE_LOGS=1` on official GMS, plus the existing enforced
channel identity, provider pins and activity flags. The trace is opt-in, emits
only fixed categories and stack frames, deduplicates records, and caps each
process at 512 unique records. Duplicate suppression is not a call counter.
Trace classification never changes authorization or token return values.

Build the four-button probe from `tests/google-auth` and pin it as manifest input
`google-official-auth-probe-v3`. Verify its signer is the existing approved
`77e047c82dd1571e2f1b1e745cddc6e35dae8e7a3fcde358a3f79c4d185653ab`
before launching. Use the three normal buttons sequentially, then
`spoof-package`. Require each report's start timestamp to follow its button
action. The [probe README](../tests/google-auth/README.md) describes the tools
and flags. The approved diagnostic probe was PID 5992 and was stopped afterward.

The first diagnostic probe was accidentally signed with another development
key. The provider rejected it during account listing, before official token
testing. Those four reports are retained as **invalid positive-control setup**,
not counted as the package-spoof result. Rebuilding with the approved key fixed
the setup without changing any grant.
[Setup correction](../tests/google-auth/boundary-validation/ungranted-signer08.json).

Validation passed the Java build (123 sources / 255 classes), runtime packaging,
boot generation, board framework preload and the host diagnostic-redaction
test. Runtime JAR hashes were checked in both the live official host and the
approved probe. Spotify retained the tested build-07 client framework; only the
official host needed the new boundary diagnostics.
[Build/source/runtime hashes](../tests/google-auth/boundary-validation/build08.json),
[approved probe build](../tests/google-auth/boundary-validation/probe04-build.json).

These experimental processes still share native UID 20010056. The package-spoof
rejection does not establish production UID/SELinux isolation. Input used the
private process mailbox. Full OH foreground/task/physical-touch acceptance,
completed OAuth consent, Spotify backend login, general API coverage and Google
certification remain unproven. Account-bearing screenshots, credentials,
tokens, arbitrary logs and proprietary disassembly are excluded from git.

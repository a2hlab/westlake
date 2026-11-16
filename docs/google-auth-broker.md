# Google account broker (two-path)

- **Status:** experimental and off by default.
- **Origin:** Codex built and board-tested it on `experiment/microg-bootstrap` (report head [027c5b9]).
- **This change:** it came to main on 2026-11-16, without that branch's bootstrap experiment. The split
  compiles and passes the host tests, but the board has not run it in this form yet.

[027c5b9]: https://github.com/a2hlab/westlake/tree/027c5b923efa0306e0f3db69d7156b041f3e711c

## What it does

The two paths split responsibilities between microG and Google's official apps:
- **microG** runs in its own namespace. It keeps the reusable Google account credential in its account
  store, and its own AuthManager issues scoped tokens.
- **Official Play services and the official Play Store** still provide Google APIs, modules and the
  Store. Through a narrow broker they get the account list and scoped tokens, never the credential.

A sign-in can also be delegated. An app, such as Spotify, signs in through official Play services' sign-in
UI. The scoped token asked for on its behalf is then issued for that app, verified as the live original
caller, and not for Play services.

The broker does not certify the device. microG's check-in is not Google's uncertified-device registration,
and official Play services enforces its own identity's `uncertified_status` whoever holds the account.

## Pieces

- **`PlayServicesChannel`:**
  - It connects to the provider (`WESTLAKE_GOOGLE_AUTH_CONNECT`). Before sending any account data, it
    pins the provider to its root spawner and its APK hash (`WESTLAKE_GOOGLE_AUTH_SPAWNER`,
    `WESTLAKE_GOOGLE_AUTH_APK_SHA256`).
  - On the provider, `AUTH_PROVIDER` hands a verified client its token-provider binder.
  - `DELEGATED_AUTH` and `READ_DELEGATED_AUTH` carry delegated requests.
  - In the provider's namespace, a foreign client reaches nothing else.
- **`GoogleAuthProxy` and `GoogleAuthPolicy`:** the provider and client halves.
  - They handle account listing, scoped tokens, invalidation of the caller's cache, and account
    feature queries. Unsupported options and mutations fail.
  - Grants list the exact package, current signer and scopes. They live in microG's private
    `westlake-auth-proxy/grants.properties`, whose owner and mode are checked, and which is read again
    for revocation.
  - Account metadata a client has seen verified is kept apart from tokens. A transport outage keeps it,
    and an explicit denial deletes it.
- **`GoogleAuthDelegation` and `GoogleAuthDelegationPolicy`:**
  - They verify the original app and parse its request.
  - A bounded worker makes the scoped AuthManager request, with a 90-second timeout, and the checks are
    repeated after the network call.
  - A live operation gets at most 16 results, and the account selection expires after 10 minutes.
- **`RemoteActivities`:** the selected-account witness of a live interactive sign-in. The activity
  transport itself comes with the remote-activities change.
- **`LocalAccounts`:**
  - In the provider, microG's authenticator answers. Its store is written owner-only and atomically
    (`PrivateFiles`), and a store that cannot be read fails closed.
  - In a client, account-manager calls go to the broker.
- **`GoogleAuthBoundaryTrace`:** value-free diagnostics, behind `WESTLAKE_GOOGLE_AUTH_BOUNDARY_TRACE=1`.

## Switches

| Setting | Effect |
|---|---|
| `WESTLAKE_GOOGLE_AUTH_PROVIDER=microg` | This process (microG) is the provider |
| `WESTLAKE_GOOGLE_AUTH_CONNECT=<endpoint>` | Official Play services and the Store use the provider at this endpoint |
| `WESTLAKE_GOOGLE_AUTH_SPAWNER`, `WESTLAKE_GOOGLE_AUTH_APK_SHA256` | The provider's root spawner and APK hash that a client must find |
| `WESTLAKE_GOOGLE_AUTH_DELEGATION=1` | Original-app delegation, on microG and official Play services |
| `WESTLAKE_CHANNEL_IDENTITY=enforce`, `WESTLAKE_GOOGLE_PRIVATE_LOGS=1` | Required by the provider and by delegation |
| `WESTLAKE_REMOTE_ACTIVITIES=1` | The interactive sign-in UI runs in Play services' declared process |

Start microG, then official Play services, then the Store, then the app. Pin the provider again whenever
microG is redeployed.

## Limits

- One existing Google account.
- The delegation adapter fits the cache protocol of the tested Play services build (versionCode
  244034029). It is not a stable Play services interface.
- A `remote_consent` answer is not turned into success, and no consent UI is relayed.
- Isolation is not real yet. In the board runs, every process of the setup shared one uid, so the
  namespace and the broker's checks do not stop hostile native code under that uid. Per-app uids
  (ADR-0003, ADR-0008) come first, before any security claim.
- A PendingIntent that another package created still cannot be executed through its creator. FotMob's
  Google sign-in stops there.

## Evidence

Codex's reports and evidence are on the experiment branch:
- [delegation validation, build 11 (Spotify)](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-delegation-validation.md);
- [26-app retest, including FotMob](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-service-retest.md);
- [build-04 continuous proxy, Store and outage validation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-two-path-validation.md);
- [auth-boundary investigation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-boundary-validation.md).

The retest ran under a Play services identity whose `uncertified_status` was 1. It is not a
certification result.

Host tests are in `tests/google-auth/README.md`.

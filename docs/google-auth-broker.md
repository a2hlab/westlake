# Google account broker (two-path)

- **Status:** experimental and off by default.
- **Origin:** Codex built and board-tested it on `experiment/microg-bootstrap` (report head [027c5b9]).
- **Current continuation:** the cleaned split at `40a365f`, without that branch's bootstrap experiment,
  has now run on the board with the creator-owned PendingIntent and local-discovery fixes described in
  [the continuation report](google-two-path-continuation.md).

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
One optional switch does work around Play services' uncertified-device check:
`WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT` widens Play services' own exception list, so that a few sign-in
services are served on a device Google treats as uncertified. See
[Play services' uncertified-device exceptions](#play-services-uncertified-device-exceptions). It leaves
`uncertified_status` and the check-in identity as they are; it does not register the board, change
Google's server decisions, or lift any other service restriction.

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
| `WESTLAKE_GOOGLE_IDENTITY_CLAIMS=1` | Adapt the supported interactive identity claim request; see the continuation report |
| `WESTLAKE_GOOGLE_SILENT_AUTH=1` | Authorize the supported synchronous ID-token request using a persisted, revocable app/account/audience selection |
| `WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT=1` | On pinned GMS 244034029, add identity services 212 and 213 to its local uncertified-service exceptions before main-process application bind |
| `WESTLAKE_GOOGLE_POLICY_TRACE=1` | With private logs, trace fixed policy categories and service-callback stacks; callback tracing also requires Play tracing |

Start microG, then official Play services, then the Store, then the app. Pin the provider again whenever
microG is redeployed.

The identity-service switch is off by default and requires the delegation/identity/private-log guards.
It writes only `uncertified_exceptions_whitelist` in the device-protected
`gms_chimera_phenotype_flags` preferences. This GMS build does not read that flag through Gservices.
It preserves existing exceptions and journals the old value in private GMS data. Disabling the switch
and restarting GMS restores the old value if the current value still matches the adapter's write;
a newer Google value is preserved. Carry the journal with the package data across upgrades. Unsupported
GMS versions or unreadable/malformed configuration with this feature active withhold application bind.
A later live Google flag refresh can replace the startup adaptation; this is a pinned experiment,
not an update-resilient compatibility guarantee. Token authorization and provider grants are unchanged.

## Limits

- One existing Google account.
- The delegation adapter fits the cache protocol of the tested Play services build (versionCode
  244034029). It is not a stable Play services interface.
- A `remote_consent` answer is not turned into success, and no consent UI is relayed.
- Isolation is not complete. Multiple apps still share the OH host bundle's uid, so the
  namespace and the broker's checks do not stop hostile native code under that uid. Per-app uids
  (ADR-0003, ADR-0008) come first, before any security claim.
- A single-activity PendingIntent can now execute through its verified creator and return its result
  to the foreign caller. The fixture proves this contract; actual FotMob/authentication results are
  recorded separately in the continuation report. General activity stacks and URI grants remain unsupported.

## Play services' uncertified-device exceptions

Play services refuses most third-party API requests on a device whose `uncertified_status` is 3. It still
serves the services in its Phenotype value `uncertified_exceptions_whitelist`, which in build 244034029
defaults to `151,130,40,25,257,185,51,218`. With `WESTLAKE_GOOGLE_IDENTITY_SERVICE_COMPAT=1`, Westlake adds
services to that value in Play services' own `gms_chimera_phenotype_flags` preferences before Play
services starts. The added services then pass Play services' uncertified-device check on a device that
Google still treats as uncertified.

This is a local workaround of Google's device policy, not certification:
- `uncertified_status`, the check-in identity and Play Integrity verdicts are unchanged.
- Google's servers still decide whether to issue tokens.
- Every other service stays refused: ads, measurement, location and module downloads among them.

**Approved set.** The project owner approved this workaround on 2026-11-17 for these services only:

| Service | What it is |
|---|---|
| 212 | Google Identity sign-in (the account chooser) |
| 213 | Its internal identity service, used after account selection |
| 91 | Legacy Google Sign-In |
| 44 | `SignIn.API`, which legacy sign-in opens |
| 5 | People (`People.API_1P`), needed by Gmail's contacts client |
| 29 | Feedback, needed by Gmail's contacts client |

`main` adds 212 and 213; the other four are part of the pending two-path validation change. Adding any other
service needs the owner's sign-off.

**Rules that stay:**
- No override of `uncertified_status` or `enable_uncertified_device_check`.
- No certification or attestation spoofing.
- Registering the board with Google remains the owner's decision.

**Results must say so.** A result obtained with the switch on (a round, a validation report, a gap-map
entry) must state that the exception list was widened and name the added services. It is not evidence of
what an unmodified uncertified device gets, in the same way that results from a separate check-in identity
must state that identity's `uncertified_status`. Play services' log records each widening as
`[WL-GOOGLE-POLICY] identity_service_compat=true services=...`, and each restoration as `...=false
restored=true`.

## Evidence

Codex's reports and evidence are on the experiment branch:
- [delegation validation, build 11 (Spotify)](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-delegation-validation.md);
- [26-app retest, including FotMob](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-service-retest.md);
- [build-04 continuous proxy, Store and outage validation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-two-path-validation.md);
- [auth-boundary investigation](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/google-auth-boundary-validation.md).

The retest ran under a Play services identity whose `uncertified_status` was 1. It is not a
certification result.

Host tests are in `tests/google-auth/README.md`.

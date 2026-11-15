# Westlake Google Play services proxy discussion

Discussion snapshot: 2026-11-15. Audience: Westlake implementers and reviewers.

The intended direction is to keep **official Google Play services and Play
Store running in a sandbox**, supported by a GrapheneOS-style compatibility
layer, while investigating microG for authentication, check-in and selected
service replacements. The product should not depend on every user manually
submitting a Google Services Framework ID to an uncertified-device form.

The central hypothesis is still open: **can state obtained through microG make
the official Google backend usable on Westlake after its own fresh policy and
credential checks?** Source inspection supports concrete integration points;
it does not establish Google's acceptance. An opt-in bootstrap experiment is
implemented locally. A continuous microG authentication proxy is still a design
proposal. The [experiment runbook](google-bootstrap-experiment.md) describes
the executable steps; this note preserves the reasoning and intended scope.

## Product requirements and intended direction

- Preserve the broader API implementation available in official Play services.
  The proposal does not replace the whole Google stack with microG.
- Investigate automatic account/device setup without making manual GSF-ID
  submission a product requirement. Successful submission would not establish
  compatibility with every Google API or application anyway.
- Keep app identity, permissions, account ownership and token scope intact
  across routing. A proxy must not treat a caller-supplied package name as proof
  of identity or globally give another package Google's signing identity.
- Use OHOS-backed or microG-backed replacements selectively where they have
  a specific benefit. Measure completed app operations, not merely startup,
  a successful login screen or a service connection.
- Finish the code and host preparation first, then request board access and
  a Google test account for runtime validation. Changing the manufacturer
  string alone does not test the proxy hypothesis.

Official implementations offer a plausible compatibility advantage because
they contain Google's actual service protocols, modules and behavior. Real
coverage also depends on Westlake's Android framework and native adaptation,
Google device policy, backend acceptance and each app's own requirements.
No percentage improvement across the Hong Kong app corpus has been measured
for this hybrid proposal.

## What GmsCompat contributes

GrapheneOS-style GmsCompat is an OS compatibility layer around official Google
packages. It is not an alternative implementation of all Play APIs, an account
token issuer or a Google device-certification service. Its useful ideas for
Westlake include adapting privileged assumptions for sandboxed apps, providing
controlled service behavior and selectively routing calls to OS implementations.

The proposed division of responsibility is:

| Responsibility | Proposed owner | Constraint |
|---|---|---|
| Normal Play API implementation and modules | Official GMS | Must function on Westlake and pass applicable Google checks |
| Play Store UI and app distribution | Official Play Store | Account and delivery operations require separate validation |
| Android platform behavior over OHOS | Westlake adapters | Must implement the semantics apps and GMS actually depend on |
| GMS-specific sandbox compatibility and routing | GmsCompat-style layer | Scoped changes with caller identity and permissions preserved |
| Login and scoped-token issuance | microG, if the proxy proves useful | Requires an explicit account and credential ownership contract |
| Selected fallback APIs | OHOS service or microG implementation | Each route needs functional, permission and lifecycle tests |
| Device check-in identity and refresh | One designated owner at a time | Two independent writers must not race over one identity |

General Android-to-OHOS adaptation belongs in reusable Westlake platform
services. GMS-specific compatibility can call those services. Putting every
platform adapter inside a GmsCompat-like module would couple unrelated Android
apps to Google-specific code without resolving their platform requirements.

## Registration and authentication are different states

| State | What a successful result establishes |
|---|---|
| Device check-in | An installation ID, security token and associated check-in state |
| Google account login | An authenticated account and credentials for subsequent exchanges |
| Scoped token exchange | A token for a particular service, caller and request context |
| Official GMS device allowance | The tested official services/modules are permitted under current policy |
| Device certification or Play Integrity | A separate certification or attestation-related result |
| Application acceptance | The particular app completes its actual task |

microG check-in is not an implementation of the separate user-facing
uncertified-device registration form. The reviewed login/check-in code does
not return a documented transferable exemption from official GMS policy.
Account-free APIs can also encounter device restrictions, which is why account
login alone cannot explain or demonstrate universal API access.

Earlier Westlake evidence for official GMS 24.40.34 included a check-in identity
while `uncertified_status=3` remained restrictive. Restrictions could reject a
service broker with error 9 or refuse fresh module access. The full-table
Gservices replacement fix addresses stale local settings; it does not make a
restriction still supplied by Google disappear. These are earlier observations,
not new results from the bootstrap experiment.

## Three architectures discussed

| Architecture | Credential ownership | Current position |
|---|---|---|
| Official GMS with GmsCompat-style adaptation | Official GMS | Primary compatibility target |
| One-time microG bootstrap followed by official GMS | Official GMS receives the reusable account credential; ownership passes to the official stack | Implemented as a local experiment |
| Continuous microG token provider with official API services | microG retains the reusable credential and supplies authorized scoped results | Proposed; not implemented by the bootstrap experiment |

In the continuous design, an app reaches official Play services through the
normal Google-facing interface. Authentication operations that have explicitly
supported routing go through a trusted Westlake account broker to microG.
Other official service operations remain in official GMS. Selected complete
service replacements would be separate routes with their own contracts.

The bootstrap is deliberately smaller. microG authenticates in one host; the
host stops; selected state is handed to a separate official-GMS host; official
GMS then operates normally. It can test whether the state is useful without
first building a universal authentication router. It gives official GMS the
reusable credential, so it does not demonstrate the stronger separation of a
token-only continuous provider.

Two full stacks both claiming `com.google.android.gms` cannot simply coexist
as ordinary packages in one Android package namespace. A continuous design
would need an extracted authentication component, explicit namespaces or
another clearly defined routing/ownership arrangement. The current sequential
experiment avoids simultaneous package-role conflicts.

## What the microG token review established

In the inspected login path, microG exchanges the web login credential and
stores `AuthResponse.token` through `AccountManager.addAccountExplicitly` in
the account's password slot. That value is a **reusable account credential**,
not the password typed by the user. It should not automatically be described
as a standard OAuth `refresh_token`; the Android auth protocol distinguishes
`Token` from the returned scoped `Auth` value.

Subsequent `AuthManager` requests read that reusable credential and request a
token for a particular service. Its cache key includes the package, signing
certificate and service, with further delegation/options. Expiry, consent,
invalidation and reauthentication are part of the contract. A single shared
token or a global logged-in flag cannot substitute for that behavior.

The credential families must remain distinct:

| Credential or result | Relevant use |
|---|---|
| Reusable account credential | Obtain further account/service tokens |
| Scoped service token | Access the requested service under its caller/scope contract |
| ID token | App identity flow, including server client ID and possibly nonce |
| Server authorization code | Backend exchange; single-use and requested fresh |
| Check-in Android ID and security token | Device identity and messaging transport, separate from account OAuth |
| Reauthentication proof or vault/passkey keys | Additional purchase or protected-domain operations |

No universal microG-only cryptographic binding was found in the ordinary
account-token path that would rule out bridging it. That finding does not
cover every token type, hardware-attestation flow, protected key or Google
server decision.

Source references: [microG LoginActivity](https://github.com/microg/GmsCore/blob/f686900c68aa84d1605b52304f6615f3593c6f50/play-services-core/src/main/java/org/microg/gms/auth/login/LoginActivity.java),
[AuthManager](https://github.com/microg/GmsCore/blob/f686900c68aa84d1605b52304f6615f3593c6f50/play-services-core/src/main/java/org/microg/gms/auth/AuthManager.java),
[AuthResponse](https://github.com/microg/GmsCore/blob/f686900c68aa84d1605b52304f6615f3593c6f50/play-services-base/core/src/main/java/org/microg/gms/auth/AuthResponse.java).

## Integration points and obstacles

The inspected official GMS 24.40.34 contains an AccountManager integration
point: `bkkw.c(Account)` calls `getPassword`; a path in `tic.h(Account)` falls
back to that credential when its private account store has none, then imports
it through `tic.s`. This supports a concrete bootstrap test. It does not prove
format compatibility or server acceptance, and an existing private credential
can take precedence over AccountManager.

A continuous proxy must cover more than AccountManager:

1. The account authenticator and Android AccountManager operations.
2. Google-specific token Binder interfaces and internal official credential paths.
3. Sign-In, Identity and Credentials services, including consent UI, resolution
   intents, callback delivery, audiences, nonces and single-use codes.
4. Refresh, invalidation, logout, revocation, account removal and process death.

A naive extra Binder hop can change the UID/PID seen by microG while retaining
claimed original-caller fields. The reviewed direct token interface checks the
actual caller. A trusted broker must authenticate the original package and its
signer and carry an explicit delegation contract; globally accepting supplied
identity fields would defeat the boundary.

Routing also has to occur at the right point. A hook that replaces a returned
Binder cannot provide a replacement when an earlier broker gate returns error
9 and no Binder. Such a replacement needs an appropriate route before that
gate. It would demonstrate an alternative implementation working, not Google
granting access to the official service.

Westlake's existing account storage, caller checks, per-app mounts and process
boundaries require device validation. Adding this experiment does not establish
GrapheneOS-equivalent isolation. The earlier APK/HAP discussion remains relevant:
sharing lifecycle and window management is compatible with separate app
processes, but does not by itself prove equivalent security enforcement.

## What GBox and 出境易 do and do not establish

Inspection of the downloaded GBox 1.8.4.02 package found a customized microG
`com.google.android.gms`, a renamed microG Companion
`com.android.vending.mgc`, and a Google-signed official Play Store
`com.android.vending`. That is concrete evidence of an official store alongside
microG in the shipped payload. It is not evidence of microG forwarding tokens
to a simultaneously running full official GMS implementation. Native and
virtual modes may differ.

Downloading or installing YouTube/Maps does not establish that every account,
location, Maps, integrity or payment operation works. The reviewed 出境易 public
material did not establish its precise microG implementation or justify
generalizing one mode to its other platform versions. Neither product supplies
runtime proof for Westlake's proposed hybrid.

## Device identity decision

Upstream microG's default `auto` profile first considers an applicable system
profile, then a matching automatically selected bundled product/API profile,
and otherwise falls back to `native`. Native uses host Android Build values;
serial handling is a separate exception. microG does not universally report
a Pixel model or one fixed Android version. A vendor fork can override the
profile. [ProfileManager source](https://github.com/microg/GmsCore/blob/f686900c68aa84d1605b52304f6615f3593c6f50/play-services-base/core/src/main/kotlin/org/microg/gms/profile/ProfileManager.kt).

The requested Westlake source defaults for this experiment are:

| Field | Value |
|---|---|
| Manufacturer | `HopeRun` |
| Brand | `OpenHarmony` |
| Model | `DAYU200` |
| Device and product | `rk3568` |
| Android-facing release and API | `14`, API `34` |
| Fingerprint | `OpenHarmony/rk3568/rk3568:14/oh-adapter:user/release-keys` |

Only the manufacturer fallback was changed. These are Android-facing source
defaults, not a claim that the board runs Android instead of OHOS. OH parameters
can override fallbacks; the actual resolved identity must be recorded on the
board. The native binary for that string change has not been rebuilt in this
experiment's validation record. Changing this string alone does not satisfy
Google registration or test credential handoff.

## Implemented experiment and current evidence

The local worktree is `worktrees/westlake-microg-bootstrap`, on branch
`experiment/microg-bootstrap`, based on Westlake
`3761f42b3ba7f3f2c91ab883f2f92d485e2e1883`. This branch contains the
implementation and host validation evidence. Board deployment remains pending.

| Component | Implemented behavior |
|---|---|
| [LocalAccounts](../framework/core/java/LocalAccounts.java) | Select microG's authenticator in the experiment; preserve microG ownership of its custom-token cache; import the account before exposing the store |
| [GoogleBootstrapExperiment](../framework/core/java/GoogleBootstrapExperiment.java) | Check the host/profile, wait for login, request check-in and tokens, export/import state and emit outcome-only diagnostics |
| [BootstrapTransfer](../framework/core/java/BootstrapTransfer.java) | Validate a private one-account envelope, run/profile match and one-hour lifetime; reject an existing target account store |
| [BootstrapStage](../tests/google-bootstrap/BootstrapStage.java) | Prepare account-only or account-and-device inputs before the official host starts |
| [ProbeActivity](../tests/google-bootstrap/ProbeActivity.java) | Probe brokers 51, 8, 93 and 300 as an external client; execute App Set ID |
| [Comparison tool](../tools/summarize_google_bootstrap.py) | Keep broker success, API success, matching-build evidence and server-acceptance claims separate |

The account-and-device arm is restricted to official GMS versionCode
`244034029`, APK SHA-256
`1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1`.
It stages microG's actual check-in ID/security-token pair using the inspected
private file format, verifies official readers, and leaves policy flags alone.
It does not migrate every check-in field or attestation state. Other GMS
versions need a new format review.

The [host validation record](../tests/google-bootstrap/host-validation.json)
records 17 transfer/staging checks and six result-classification tests passing,
246 adapter classes compiled, Java runtime DEX packaged, and a signed probe APK
built. No Google account was used, no board deployment occurred, and no Google
server-acceptance result was obtained. Board boot-image/native packaging and
runtime checks remain separate work.

## What the board experiment must establish

Run matched official baseline, account-only handoff, and account-and-device
handoff trials. Restore the stopped, account-free baseline between variants;
do not let two hosts independently use one check-in identity. Keep the runtime,
official APK, probe signature and intended profile fixed.

| Observation | Interpretation |
|---|---|
| microG login and token exchange work | Source authentication path works |
| Official GMS reads the credential and obtains a token | The tested client integration works; device allowance remains separate |
| Official readers consume the staged device pair | The pinned client understands that part of the handoff |
| Fresh official policy remains restrictive | That variant does not meet the product requirement |
| A broker becomes available but its API fails | Connection access improved; the app operation is still incomplete |
| App Set ID succeeds after a blocked baseline | A sampled functional improvement; not proof of Maps, location or Play Store success |
| Results survive fresh policy/credential refresh and restart | Stronger evidence for that tested configuration; continue lifecycle and soak testing |

Next coverage includes official Play Store login/download, Maps, location,
Google Sign-In, messaging, consent, invalidation, logout/revocation and rejected
unauthorized callers. A suggested 72-hour soak would examine durability; it has
not been run. If only cached modules or an initial permissive interval work,
the hypothesis remains unresolved.

The next external inputs are board access through HDC and a confirmed Google
test-account email. Password and verification steps can be completed during
interactive login. No account address is recorded in this note.

## Workspace research references

These earlier reviews remain workspace artifacts and are not included in this
Git branch. Their findings are summarized above; the paths below are relative
to the shared workspace root, not links within the published repository:

- `reports/hybrid-play-feasibility-20261115.md`
- `reports/microg-auth-proxy-review-20261115.md`

Use the implementation/runbook and validation record above for current local
code status; the earlier reviews describe source findings and earlier device
evidence, not successful results from the new experiment.

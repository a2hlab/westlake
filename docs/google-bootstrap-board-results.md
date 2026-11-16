# Board results: microG bootstrap into official Google Play services

Later evidence: the [live two-path proxy validation](google-two-path-validation.md)
completed official Play Store catalogue, download and installation with the reusable
credential retained only by microG. The results below remain the earlier sequential
handoff experiment and should be read in that historical scope.

Test session: 2026-11-15 Pacific / 2026-11-16 UTC.

**The prototype transferred a reusable microG-issued credential to official
GMS, which returned an `ac2dm` token before and after invalidation and again
after restart. Transferring the check-in identity also worked. This did not
demonstrate a workaround for Google's device-registration requirements or an
improvement in application compatibility.**

Official GMS continued to write `uncertified_status=1` in both arms. The probes
did not reproduce service error 9 even in the account-free baseline. Therefore
this experiment cannot establish removal of that rejection. The configuration
field is recorded literally: it is not an independent test of manual GSF-ID
registration status, certification, Play Integrity, or every Google backend's
allowance decision. No manual GSF-ID registration was performed in this run.

## What was actually run

One authorized test account was used on one DAYU200 board running OpenHarmony
6.1.0.31. The actual reported profile was manufacturer `HopeRun`, model
`DAYU200`, Android 14, SDK 34. Runtime reflection verified microG's active
profile against the same eight Java Build fields used by official GMS. Serial
identity was not transferred.

microG `0.3.17.252432` completed Google's login UI, check-in and the scoped-token
test. Its private bootstrap envelope was exported, then all of its experiment
processes were stopped. Official GMS versionCode `244034029` subsequently ran
its own authenticator. This is a **sequential credential handoff**, not two
simultaneous `com.google.android.gms` packages or a continuous microG token
proxy. It exercises Westlake's official-GMS host; it is not a test of an
unchanged GrapheneOS installation or proof of GmsCompat feature parity.

A fresh, account-free official-GMS baseline was stopped and cloned separately
for each arm. The first arm added only the reusable account credential. The
second additionally staged `files/checkin_id_token`. Neither arm imported
microG's policy/configuration databases or set an allowance override. Baseline
Gservices configuration was retained. The official version-pinned getters
confirmed consumption of both fields of the staged device identity; merely
copying the file was not counted as success.

Both arms were restarted in their exact existing data directories. The pinned
APK, Build profile, framework, native runtime and Bionic compatibility payload
were held constant in the final comparison. The probe was the same separately
running, source-built APK in every phase.

## Results and their limits

| Check | Account only | Account plus check-in identity |
|---|---|---|
| Private credential import | Completed | Completed |
| Official `ac2dm` token returned | Yes | Yes |
| Token returned after invalidation | Yes | Yes |
| Both token operations after restart | Yes | Yes |
| Official getters read microG ID and security token | Not transferred | Matched, including after restart |
| Full Gservices writes | `uncertified_status=1`, override absent | `uncertified_status=1`, override absent |

A token returned after invalidation establishes that the exercised
AccountManager/authenticator path completed. An independent network trace or
private cache audit is still needed to establish a fresh server exchange for
each request. `ac2dm` is not an OAuth ID token, a third-party Google Sign-In
flow, or proof that Play Store can sign in and download an app.

The final baseline, both handoff phases and both restart phases had the same
external-probe outcomes:

| Probe | Result | What it establishes |
|---|---|---|
| Phenotype, service 51 | Broker status 0; Binder present | Control broker handshake |
| Ads, service 8 | Broker status 0; Binder present | Broker handshake, not a rendered ad |
| Measurement, service 93 | Broker status 0; Binder present | Broker handshake, not analytics delivery |
| App Set ID, service 300 | Binding refused | The functional API call was not reached |

No Maps, fused location, Play Store download, third-party Sign-In, FCM delivery,
Play Integrity or banking-app acceptance test was completed. None of the Hong
Kong corpus apps was newly validated by this experiment. The comparison tool
classifies both arms as `baseline_did_not_reproduce_error9`; no sampled API
improvement is established.

Gservices evidence consists of provider writes by official GMS. A requested
check-in broadcast, a provider write, or an absent key is not by itself an
independently verified fresh Google policy response.

## Changes needed to reach the test

- The launcher now passes its authenticated appspawn socket and a private copy
  of the spawn request into the child namespace. Real microG and GMS secondary
  processes were observed. Setting `WESTLAKE_APP_PROCESSES=1` alone was insufficient.
- `AppProcesses` temporarily permits the AF_UNIX connection under StrictMode
  and restores the previous thread policy. This removed microG's observed
  `NetworkOnMainThreadException` during secondary-process startup.
- The diagnostic text helper uses the focused editor's `InputConnection` and
  its requested handler thread. A neutral string was visibly entered and
  cleared in Google's WebView. This is diagnostic-input validation, not
  physical-keyboard or normal-gesture acceptance.
- Private mode suppresses Android Java logging and input/view diagnostics,
  removes entered text from text-helper status, and uses a per-PID text mailbox.
  The neutral marker was absent from the app log and from system logs after
  excluding HDC's own command echoes. Credential input used private temporary
  files, not shell arguments. Credentials, cookies and private account stores
  are excluded from the committed evidence.
- Both Google implementations must receive the Bionic shim. The preliminary
  official-GMS launches omitted it, hit an unresolved
  `com.google.android.gms.org.conscrypt.NativeCrypto.SSL_CTX_new`, and included
  native aborts. Those runs are excluded from the final comparison. The
  corrected runs mapped the shim and GMS Conscrypt library. These are bounded
  observations, not a long-duration stability claim.

The final device-identity probe was repeated with a fresh client after an
earlier probe remained pending across a host restart. Incomplete or older
result files were rejected by timestamp checks and are not counted.

## Evidence and implementation follow-up

[Machine-readable board validation](../tests/google-bootstrap/board-validation.json)
records the source/artifact hashes, launch reports, completed phases and limits.
[Sanitized events and probe results](../tests/google-bootstrap/board-results/)
contain no account names or token values. The earlier
[host-only validation](../tests/google-bootstrap/host-validation.json) remains
as a historical pre-board record. The 17 transfer/staging checks and six
classification checks passed; the rebuilt framework passed board preload and
the native links used strict symbol resolution.

The launcher fixes and input pins are on manifest's
[`fix/google-bootstrap-board-launch` branch](https://github.com/a2hlab/manifest/tree/fix/google-bootstrap-board-launch).
The experiment processes were stopped after collection. Temporary host-side
credential exports/staging and the board's microG export were removed; the
stopped test applications' private account stores remain on the board.

This fixture used separate processes and app-data directories under one native
host UID. It does not validate production UID separation, unauthorized access
denial, SELinux parity or GrapheneOS security parity. WebView and some firmware
inputs were device-captured; full source closure is not claimed.

The credential-handoff implementation is worth retaining as an experimental
integration point while official GMS remains the API provider. It should not
be presented as an automatic registration solution. Next implementation work
should establish a reproducible blocked-API baseline, observe fresh official
policy decisions without logging secrets, and test completed account-bound
operations such as Sign-In, Maps/location and Play Store downloads. A shipping
authentication broker additionally needs caller/account/scope enforcement,
credential rotation and revocation, and production isolation; the stopped-host
file-transfer prototype does not supply those properties.

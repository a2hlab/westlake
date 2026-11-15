# microG bootstrap into official Google Play services

The [proxy discussion note](google-services-proxy-discussion.md) records the
product requirements, continuous-proxy proposal, source findings and the
distinction between authentication and Google device allowance.

This is an opt-in, sequential-host experiment for the hypothesis that microG's
login/check-in state can bootstrap official GMS on Westlake. It does not run two
implementations under one package simultaneously and is not the continuous
microG token-provider architecture. All normal launches keep the official
authenticator and existing behavior when the experiment variables are absent.

## What the code tests

1. microG's authenticator can be used by Westlake AccountManager. The old code
   always bound Google's authenticator class, which does not exist in microG.
2. microG can check in, log in through its own UI, obtain an `ac2dm` service token,
   invalidate that token and obtain another. The reusable AccountManager
   credential is exported only after these steps succeed. This is not an OAuth
   ID-token/Google Sign-In test.
3. Official GMS can read that reusable credential through AccountManager and
   perform its own scoped-token operation. This is a bootstrap: official GMS
   obtains the reusable credential and may retain it in its private store.
4. In a separate arm, official GMS can consume the same check-in Android ID and
   security token. This arm is pinned to the inspected GMS APK below.
5. An external app checks broker services 51 (Phenotype control), 8 (ads), 93
   (measurement), and 300 (App Set ID). It also executes the App Set ID request
   and checks for a successful status, nonempty ID and valid scope. IDs are not
   printed. A returned Binder alone is recorded only as broker acceptance.

The device-state arm copies no Gservices allowance/restriction flags, no
Phenotype database, no DroidGuard/attestation state and no downloaded modules.
It cannot establish certification. Fresh policy responses and repeat tests are
still necessary even if the sampled API works.

## Version boundary

Westlake base: `3761f42b3ba7f3f2c91ab883f2f92d485e2e1883` (`spawner-auth`).
The manufacturer fallback on this branch is `HopeRun`; an OH parameter may take
precedence, so every run records the actual Java Build identity.

The microG interfaces were inspected at
[`f686900c`](https://github.com/microg/GmsCore/tree/f686900c68aa84d1605b52304f6615f3593c6f50).
Runtime reflection fails explicitly if the supplied microG APK lacks those
interfaces. The local v0.3.17 APK is an available input, not yet a validated
runtime match for the inspected source.

Official GMS: versionCode `244034029`, SHA-256
`1159bb46cad6f4afd7f4a570b1a5674f1285a12e573dbe6501ed4b059fd902d1`.
Read-only inspection of this build found `aizc.j(Context)` reading UTF-8
`files/checkin_id_token`, formatted as signed Java long `androidId:securityToken`;
`aizt.b(Context)` reads the first field and `aizc.d(Context)` the second.
The diagnostic calls the official public getters `aizt.a` and `aizc.b` to verify
they return the staged pair. These obfuscated names are version-specific;
the APK hash check rejects every other build for device-identity handoff.

## Build and host checks

From the Westlake repository root:

```sh
mkdir -p /tmp/westlake-google-bootstrap-classes
javac -d /tmp/westlake-google-bootstrap-classes \
  framework/core/java/BootstrapTransfer.java \
  tests/google-bootstrap/BootstrapStage.java \
  tests/google-bootstrap/BootstrapTransferTest.java
java -cp /tmp/westlake-google-bootstrap-classes adapter.core.BootstrapTransferTest
python3 -m unittest discover -s tests -p test_google_bootstrap.py -v
python3 tools/build_google_bootstrap_probe.py \
  --sdk /path/to/android-sdk --jdk /path/to/jdk21 --out /new/probe-build
```

Compile the adapter with manifest's `tools/build_westlake_java.py`, using this
checkout as `--westlake-source`, then run `package_framework_runtime.py` with
the new adapter build. Both use the existing source framework/core build inputs.
Rebuild/package the board runtime in the normal manifest workflow; changing a
source file or compiling a class JAR alone does not update the board's boot
image. Keep the same runtime, probe APK/signature and official GMS APK across
all compared phases. The probe builder creates a development signing key in
its output directory; that key is not a Google or microG signing identity.

## Runtime switches and sequence

Set these only on the primary GMS host, through the existing launcher/spawner
configuration, with `WESTLAKE_PLAY_SERVICES_SERVE` set as usual:

```text
WESTLAKE_GOOGLE_EXPERIMENT_RUN=bootstrap01
WESTLAKE_GOOGLE_EXPERIMENT=official-baseline
```

Accepted modes: `official-baseline`, `microg-export`, `official-import`,
`official-recheck`. The run ID must contain 1–64 letters/digits/underscores/
hyphens. No account name, password or token is passed as an environment variable
or launch argument. Do not propagate these switches to ordinary client apps.

1. Start official GMS in a dedicated account-free target data directory using
   `official-baseline`. Keep its normal networking/check-in behavior. Start the
   probe as a separate Westlake app connected to that host, enter the matching
   experiment run ID, and press **baseline**.
   Capture the host's experiment events and confirm the expected control and
   blocked-service results. Save a **stopped, account-free baseline snapshot**
   before doing either handoff arm. An existing private Google account credential
   would invalidate the test even if LocalAccounts appeared empty.
2. Stop the official host. Start the microG APK with a separate data directory,
   `microg-export`, and the same run ID/runtime/Build profile. Enable device
   registration in microG and log into the test account in its own settings UI.
   The worker waits up to ten minutes for exactly one Google account, then runs
   check-in and token tests. It refuses a microG profile whose model, brand,
   manufacturer, device, product, fingerprint or Android version differs from
   the host. Serial/check-in identity are separately tracked; model equality
   does not imply device-ID equality.
3. Wait for `credential_export=complete`, then stop microG. The private export is
   `<microg-data>/westlake-bootstrap/outgoing.properties`. Its contents include
   reusable credentials. Keep it private; do not paste it, attach it to a report,
   or include it in a general diagnostic archive. Transfer it to a temporary
   private host directory for the offline staging tool, or run the same tool
   in an appropriate local Java environment with access to the private file.
4. Prepare one arm while **all target processes are stopped**:

   ```sh
   java -cp /tmp/westlake-google-bootstrap-classes adapter.core.BootstrapStage \
     /private/outgoing.properties /new/private-staging /inputs/official-gms.apk \
     account-only
   ```

   Use `account-and-device` for the second arm. Staging rejects a pre-existing
   output directory; the device arm additionally verifies the exact official
   APK hash. Copy the staging tree into the stopped target's private data
   directory, preserving owner-only permissions. The staging tree contains
   `westlake-bootstrap/incoming.properties`, `staging.properties`, and, in the
   second arm only, `files/checkin_id_token`. The target's original baseline
   snapshot is retained separately. **Never stage into a running GMS host.**
5. Start the official host with `official-import`. Before exposing its account
   store, Westlake checks the run ID, profile, age (one hour), file permissions
   and empty target account store. It atomically writes one account and its
   reusable credential, records consumption, and removes the incoming file.
   Repeated accesses/restarts use the consumed marker rather than re-importing.
   In the device arm it also verifies the staged identity and actual official
   getters. Then it runs the official token operation and requests check-in.
   Run the external probe's **after-handoff** phase.
6. Keep official GMS running for actual check-in/policy refresh. Restart it with
   `official-recheck` and run **after-restart**. Compare device-ID hashes and
   policy events with the actual official network/check-in trace. A request
   broadcast, a provider write, or a missing `uncertified_status` key by itself
   is not proof of a fresh successful server decision.
7. Stop the host and restore the **account-free** baseline snapshot before the
   other arm. Repeat export if its one-hour validity expired. Use a separate
   result directory for each arm; never mix their probe phases.
8. Remove temporary export/staging credential files after the experiment.
   Disabling the switches does not revoke Google's tokens or remove an imported
   account. Logout/revoke the test account and remove experiment data as part of
   the controlled cleanup; ordinary account stores must not be used as scratch
   data. Do not run both hosts against the same Google device identity at once.

The staged pair is a real microG check-in result, not a fabricated device ID.
It is nevertheless an experimental mapping into an unsupported private Google
format. A server refusal, rotation or mismatch is an outcome to record.

## Evidence and interpretation

Host events begin with `[WL-GOOGLE-EXPERIMENT]`; they contain presence/outcome
fields, APK/profile hashes, policy status and a hash of the device ID. They never
print the account credential, service token or check-in security token.
Third-party component logs are not covered by that guarantee; collect the
experiment event prefix and explicitly reviewed check-in evidence rather than
publishing an unrestricted login log.

The external probe stores JSON in its own private files directory and logs it
under `WL-GOOGLE-PROBE`. Compare files with:

```sh
python3 tools/summarize_google_bootstrap.py \
  --baseline baseline.json --handoff after-handoff.json --restart after-restart.json
```

| Observation | Supported conclusion |
|---|---|
| microG token/export succeeds | Source authentication path works |
| `account_password_read=present` | An authorized account-manager caller requested the credential; not proof of private-store import |
| Official token succeeds after invalidation | Tested official scoped-token path works; private-cache/network trace is still needed to establish refresh |
| Official identity getters match staged pair | Pinned client read the expected device credentials; server acceptance is unproven |
| Broker status 9 persists | Sampled service remains blocked after that arm |
| Broker success without App Set callback/data | Connection success only |
| App Set ID succeeds and the baseline was blocked | Sampled functional improvement; not Maps, location, sign-in or Play Store acceptance |
| Success after restart | Persists through that restart; not a 72-hour soak or future-version guarantee |

No device/account run has been performed as part of this implementation. Still
required: actual board isolation/access checks, login UI, official fresh policy
refresh, private credential import/rotation, logout/revocation, unauthorized
caller tests, Play Store login/download, Maps/location/sign-in and a soak run.
Westlake's existing channel/account isolation limits still apply; this patch
does not create a production credential-isolation boundary.

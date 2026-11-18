# App-owned periodic sync fixture

This fixture has a local test account, a local content provider and a real
`AbstractThreadedSyncAdapter`. It makes no network requests and uses no Google
account. Its two launches test execution, duplicate registration, returned-value
copying, cancellation and persisted registration across a process restart.

Build with:

```sh
python3 tools/build_periodic_sync_probe.py \
  --sdk /path/to/android-sdk --jdk /path/to/jdk21 \
  --signing-key /path/to/development-key.p12 \
  --out /path/in/workspace/periodic-sync-build
```

The key is a throwaway development key with the Android development password,
matching the other fixture builders. Use a dedicated fixture data directory.
Launch `org.westlake.syncprobe.ProbeActivity` through the normal source-app
launcher with `WESTLAKE_LOCAL_SYNC=1`. The fixture needs the existing local
account-service backend, enabled with a unique
`WESTLAKE_PLAY_SERVICES_SERVE=wl.sync.fixture` endpoint in its own process.
Do not configure Google auth client/provider endpoints for this fixture. The
service-role flag selects LocalAccounts; this does not run official GMS.

The activity uses NoDisplay and target SDK 22 so it can keep the main looper free
while its worker waits for service binding, then finish after writing its result.
It does not test rendering. Read `files/periodic-sync-phase1.json` only after its
start timestamp is newer than the verified launch cutoff. A fresh second
process using the same fixture data writes `periodic-sync-phase2.json` and
removes its retained periodic registration. Verify `passed`, the executed check
count and the corresponding adapter dispatch/completion traces. A stale report
or an unavailable process is not a pass.

Phase 1 uses the ordinary minimum 900-second interval with a full 900-second
flex window, permitting its first job immediately. The production clock and
minimum interval are not shortened. The timing unit test separately covers
later windows, catch-up suppression, restart state and overflow. This fixture
checks a first periodic dispatch, not multiple real-time cycles or waking a
stopped application.

The implementation persists registrations and resumes their windows when the
app starts and creates LocalSyncService. Its timer executes while the app
process lives. It currently has no OpenHarmony job that starts a stopped app;
OS-managed wake, device idle policies and stopped-process background reliability
remain separate gaps.

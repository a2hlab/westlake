# Activity/result contract fixture

These APKs contain test code only. The host uses the package name
`com.google.android.gms` to exercise the configured host route; it is not
Google Play services. Use a dedicated `wl.remote.fixture...` endpoint and no
auth connection or private account data.

Build with Android SDK 34/build-tools 34.0.0, JDK 21, and a throwaway development
PKCS12 key whose password is `android`:

```sh
python3 tools/build_remote_activity_fixture.py \
  --sdk /path/to/android-sdk --jdk /path/to/jdk21 \
  --signing-key /path/to/development.p12 --out /new/output/directory
```

The build verifies both signed APKs and records their hashes and source hashes.
The manifest repository's `remote-activity-host` / `remote-activity-client`
entries in `app-inputs.lock.json` pin the board-tested artifacts. If rebuilding
with another development key, review and update those two artifact hashes and
sizes before using `tools/prepare_app.py` and `tools/probe_source_app.py`.

Run both with the same candidate framework and these flags:

```text
WESTLAKE_REMOTE_ACTIVITIES=1
WESTLAKE_CHANNEL_IDENTITY=enforce
WESTLAKE_GOOGLE_PRIVATE_LOGS=1
```

The host additionally uses `WESTLAKE_APP_PROCESSES=1`, `WESTLAKE_NO_ACTIVITY=1`
and `WESTLAKE_PLAY_SERVICES_SERVE=wl.remote.fixture07`. The client uses
`WESTLAKE_PLAY_SERVICES_CONNECT=wl.remote.fixture07`.

The client automatically checks target filtering, launch/result identity,
referrer, nested Parcelable data, cancellation, and PendingIntent behavior.
Both the remote entry and the nested PendingIntent destination forward their
result to another activity and finish; neither may cancel the waiting caller.
The nested sender is created in `:worker`, relayed through the main process,
and used by `:ui`; all hops belong to the verified host APK. Passing a fresh
sender onward to the foreign client must not let that client prepare/read its
stored Intent.

When `Fixture$Wait` is shown, verify the process table and terminate only the
fixture's `:ui` process. Read the client's private `files/remote-activity-results.json`:
`host_death_cancel` and `exactly_three_results` must be true along with the
other checks. In a separate run, terminate the verified client while the
target waits: the target process must survive and emit `wait_destroyed=true`.
Always distinguish the fixture host from the real GMS host by parent PID and
endpoint, since they deliberately use the same package name.

The [implementation report](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/remote-activity-results.md),
on the experiment branch with its evidence, separates
these contract tests from actual Spotify/Google authentication results.

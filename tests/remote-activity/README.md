# Activity/result contract fixture

These three APKs contain test code only. The host and store use package names
`com.google.android.gms` and `com.android.vending` to exercise the configured
routes; neither contains Google's code. Use dedicated `wl.remote.fixture...`
and `wl.remote.store...` endpoints and no auth connection or private account data.

Build with Android SDK 34/build-tools 34.0.0, JDK 21, and a throwaway development
PKCS12 key whose password is `android`:

```sh
python3 tools/build_remote_activity_fixture.py \
  --sdk /path/to/android-sdk --jdk /path/to/jdk21 \
  --signing-key /path/to/development.p12 --out /new/output/directory
```

The build verifies all three signed APKs and records their hashes and source hashes.
The manifest repository's `remote-activity-host`, `remote-activity-store` and `remote-activity-client`
entries in `app-inputs.lock.json` pin the board-tested artifacts. If rebuilding
with another development key, review and update those three artifact hashes and
sizes before using `tools/prepare_app.py` and `tools/probe_source_app.py`.

Run all three with the same candidate framework and these flags:

```text
WESTLAKE_REMOTE_ACTIVITIES=1
WESTLAKE_CHANNEL_IDENTITY=enforce
WESTLAKE_GOOGLE_PRIVATE_LOGS=1
```

The host additionally uses `WESTLAKE_APP_PROCESSES=1`, `WESTLAKE_NO_ACTIVITY=1`,
`WESTLAKE_PLAY_SERVICES_SERVE=wl.remote.fixture18` and
`WESTLAKE_PLAY_STORE_CONNECT=wl.remote.store18`. The store uses the same process
flags with `WESTLAKE_PLAY_STORE_SERVE=wl.remote.store18` and
`WESTLAKE_PLAY_SERVICES_CONNECT=wl.remote.fixture18`. The client connects to both
endpoints with `WESTLAKE_PLAY_SERVICES_CONNECT` and `WESTLAKE_PLAY_STORE_CONNECT`.
Start the host and store before the client.

The client first repeats 25 rounds of known/unknown service and provider discovery.
Both owners must answer positive queries, misses must return empty/null, and the
two mutually connected hosts must not forward these requests back to each other.
Missing results or a hung client is a failure. Capture process thread counts during
this run as well as the two `mutual_host_*_discovery` assertions.

The client automatically checks target filtering, launch/result identity,
referrer, nested Parcelable data, cancellation, and PendingIntent behavior.
Both the remote entry and the nested PendingIntent destination forward their
result to another activity and finish; neither may cancel the waiting caller.
The nested sender is created in `:worker`, relayed through the main process,
and used by `:ui`; all hops belong to the verified host APK. A second sender is
passed onward to the foreign client. Its raw stored-Intent read must fail, but
executing it through its verified creator must reach the non-exported `:result`
activity and return the answer to the original client. The fill-in must not
change an immutable sender; a second execution of a one-shot sender must fail.
A fabricated sender claiming the host's package must not open a private activity.
The destination must also read the original caller's package record and open its
APK locally; an intermediate process's `/proc/self/fd` path is not sufficient.

When `Fixture$Wait` is shown through its foreign PendingIntent, verify the process
table and terminate only the fixture's `:result` process. Read the client's private `files/remote-activity-results.json`:
`host_death_cancel` and `exactly_four_results` must be true along with the
other checks. In a separate run, terminate the verified client while the
target waits: the target process must survive and emit `wait_destroyed=true`.
It must also emit `caller_metadata_released=true` after its temporary registration
has been released. Other live connections/operations must retain their registrations.
Always distinguish the fixture host from the real GMS host by parent PID and
endpoint, since they deliberately use the same package name.

The [implementation report](https://github.com/a2hlab/westlake/blob/027c5b923efa0306e0f3db69d7156b041f3e711c/docs/remote-activity-results.md),
on the experiment branch with its evidence, separates
these contract tests from actual Spotify/Google authentication results.

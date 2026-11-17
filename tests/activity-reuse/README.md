# Local activity reuse fixture

This APK has no network or account permissions. It exercises Android lifecycle
and result delivery through the direct-launch task:

1. `CLEAR_TOP | SINGLE_TOP` destroys the redirect activity, delivers its synthetic
   payload to the existing target, and preserves the target's original result
   recipient.
2. `SINGLE_TOP` delivers to the current instance without creating another one.
3. An ordinary start creates another instance and returns its result normally.
4. After the original root opens a second screen, the application starts a
   replacement with `NEW_TASK | CLEAR_TASK`. Both old activities must report
   destruction. The replacement must receive its payload and report itself as
   the task root.
5. The app then launches a screen with `NEW_TASK | MULTIPLE_TASK` before
   requesting another reset. The direct host has one activity list and cannot
   identify the separate task. Both live screens must survive this request.

Build with `tools/build_local_activity_fixture.py --sdk PATH --jdk PATH
--signing-key PATH --out NEW_DIRECTORY`. The signing key is a development key
with the same `android` password as the other source fixtures. Pin the generated
APK's hash and size in the manifest application's lock before staging it with
`prepare_app.py`; rebuilt ZIP/signing output may have a different hash.

The entry point is `org.westlake.activityreuse.fixture.Fixture$Root`. A successful
run emits `PASS request=41`, `PASS request=42`, `PASS request=43`, and `PASS all=3`
with the `[WL-REUSE-TEST]` prefix for the original three cases. The fourth case
emits `PASS replace_task rootDestroyed=true middleDestroyed=true isTaskRoot=true`
and the fifth emits `PASS preserve_separate_task`. Check for **Five activity
checks passed** on screen and confirm the process remains alive.

Candidate 46 reproduces duplicate redirect creation and times out with no
completed cases. Candidate 47 passes all three cases on the OH board; evidence
is in `tests/google-auth/live44/activity-reuse-candidate47.json`. Candidate 85 adds the same-affinity task replacement case. The board results
are in [candidate85.json](../google-auth/full48/candidate85.json). Separate OH
missions, cross-process reuse and live app playback need their own evidence.

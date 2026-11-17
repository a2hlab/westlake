# Local activity reuse fixture

This APK has no network or account permissions. It exercises Android lifecycle
and result delivery through the direct-launch task:

1. `CLEAR_TOP | SINGLE_TOP` destroys the redirect activity, delivers its synthetic
   payload to the existing target, and preserves the target's original result
   recipient.
2. `SINGLE_TOP` delivers to the current instance without creating another one.
3. An ordinary start creates another instance and returns its result normally.

Build with `tools/build_local_activity_fixture.py --sdk PATH --jdk PATH
--signing-key PATH --out NEW_DIRECTORY`. The signing key is a development key
with the same `android` password as the other source fixtures. Pin the generated
APK's hash and size in the manifest application's lock before staging it with
`prepare_app.py`; rebuilt ZIP/signing output may have a different hash.

The entry point is `org.westlake.activityreuse.fixture.Fixture$Root`. A successful
run emits `PASS request=41`, `PASS request=42`, `PASS request=43`, and `PASS all=3`
with the `[WL-REUSE-TEST]` prefix, then displays the three passed cases. A failed
case or incomplete run emits `FAIL`. Check process survival and the visible
result as well as those markers.

Candidate 46 reproduces duplicate redirect creation and times out with no
completed cases. Candidate 47 passes all three cases on the OH board; evidence
is in `tests/google-auth/live44/activity-reuse-candidate47.json`. These checks do
not cover cross-task or cross-process reuse, manifest launch modes, OH ability
lifecycle management, or acceptance by a live application backend.

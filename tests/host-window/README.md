# Development host window handoff

Run `node tests/host-window/host-window.test.cjs` for policy tests. This does not replace board validation.

Build and development-sign two copies of `apps/imehost`, with `tools/build_apps.py --only-host
--host-bundle org.westlake.authclienthost` and `org.westlake.authservicehost`. Use the pinned V1
resource compiler and manifest's `sign_development_host.py`. Install these dedicated HAPs without
replacing hosts that already run other apps.

Launch each runtime with the matching verified `--host-build`, `--host-window-bridge`, and
`WESTLAKE_REMOTE_HOST_ROUTES` through `--runtime-env`:

- FotMob: `com.google.android.gms:org.westlake.authservicehost`.
- Official GMS: `com.mobilefootie.wc2010:org.westlake.authclienthost`.

Keep the existing verified activity channels, provider pins, private-log controls and restricted
identity configuration. The flag defaults off. Only these two development HAPs implement the route;
there is no arbitrary bundle launcher or credential transport in the mailbox.

On the board, verify the chooser and result change foreground without shell activation within the
flow; test cancellation separately. For the background case, first confirm a third host owns the
focused window in `hidumper -s WindowManagerService -a '-a'`, then cancel and verify focus stays
there. An `aa start` success message alone is not sufficient. Test input mailboxes must be owned by
the target app UID and consumed once; a root-owned file can repeatedly replay a cancel tap.

The HAP uses its real `UIAbilityContext.startAbility` token. Its mailbox is within the host UID's
trust boundary and its wait/polling mechanism is for development. This is not production per-APK
isolation, a general activity stack or an atomic window-ownership protocol. See the
[continuation report](../../docs/google-two-path-continuation.md) for observed results and limits.

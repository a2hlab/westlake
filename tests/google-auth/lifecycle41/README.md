# Lifecycle and expired-session validation

These allowlisted results cover candidates 41–43 on the existing development board.
The [continuation report](../../../docs/google-two-path-continuation.md) explains scope and limitations.

- `overlay-control-result.json`: the original launcher corrupts its reusable overlay inputs.
- `overlay41-result.json`, `source-namespace-script42-result.json`: revised mount regression results.
- `restart41-*.json`: three in-place FotMob restarts without cache/data reset.
- `window42-result.json`: automatic chooser, cancellation and success return; signed-in UI.
- `host-negative42-result.json`, `background42-result.json`: denied requests and retained third-host focus.
- `pair42-restart-*.json`: both official GMS and FotMob restart in place, then ten silent replies.
- `provider-expiry41-result.json`: earlier cache-only expiry observation; not exact reply proof.
- `exact-expiry43-result.json`: one exact renewed reply token independently verified among five observed replies.
- `fingerprint-off43-*.json`: another in-place restart, five silent replies and no fingerprint records.
- `expiry43-preservation.json`: original identity, uncertified status and unrelated policy/grants retained.
- `artifacts42.json`, `artifacts43.json`: exact source and artifact hashes. Native runtime, ART and shim are reused.

Raw logs, token fingerprints, account stores and screenshots remain private. The signed-in UI and
valid Google ID token do not independently establish application backend acceptance. These tests
also do not establish production per-APK isolation or general Play services compatibility.

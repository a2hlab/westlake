# OHOS IME and networking repair

Starting point: isolated JIT-review parent 9623 / Toutiao child 16640. Preserve the accepted v142 artifacts, default parent 4778, APK and app data.

Initial observations (2026-09-04):

- OHOS InputMethodService has the active Kika keyboard. `liboh_ime_helper.so` is absent from the runtime search directory; the local old helper is ARM32, not deployable to this ARM64 process.
- Parent log: `register_InputMethodBridge: FindClass(OhImeBridge) null`. Child creates the NO_IME adapter and emits no show-keyboard callback.
- Wi-Fi and default network 100 are active. App logs contain datagram socket option ENOPROTOOPT and Chromium TLS -202 errors; neither alone proves the blank-feed cause.
- Android's `/system/etc/security/cacerts` is absent. OHOS has `/system/etc/security/certificates`. Inspect the effective trust provider and platform bundle before changing trust behavior. Never disable certificate or hostname checks.

This directory contains isolated probes/builds/evidence for this repair. No baseline promotion is implied.

## Current result

HTTPS and IPv4/IPv6 UDP controls pass. Toutiao Recommended content and images
render with verified TLS handshakes. IME is **not fixed end to end**: the new
ARM64 C-API helper loads and reaches the service, but the surface-only launcher
has no OHOS window session/focus, and attachment is rejected.

See `/home/dspfac/openharmony/docs/engine/OH-IME-NETWORK-REPAIR-2026-09-04.md`.

## Rebuild and evidence validation

Run from this directory, in order:

```sh
python3 fetch_inputs.py
bash build_native.sh
python3 build_java.py
bash build_boot.sh
bash build_control.sh
python3 validate.py
```

`validate.py` checks saved device evidence, not a new live run. The offline
certificate fixtures expire quickly; regenerate the fixture set before a later
device run. Do not interpret an expired positive fixture as a runtime failure.

Archive and terminate only identified repair children/parents before running
`python3 stage.py`. Its live-process guard rejects overwriting mapped repair
inputs. It does not stop processes, change the default service, or promote the
baseline. The runner hash-checks all boot/runtime inputs before starting ART.
The new boot image lives in an isolated subdirectory of ART's existing
compile-time trusted OAT root; no trust check was disabled.

Windows HDC host subpaths use backslashes. The initial malformed staging
directories were moved to `*.bad-hdc-layout` on the device, not deleted.

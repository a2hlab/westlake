# Reproduce the Android-reference fixes

Start from the exact source revisions in [a2hlab/manifest](https://github.com/a2hlab/manifest).
Its normal bootstrap installs the content-locked OH SDK and platform compiler.
The commands below accept arbitrary checkout/output paths; every output must
be new. The `historical/` scripts preserve the original experiments and their
epoch-specific layout, but are not portable build entry points.

## What is reproducible from source now

| Component | Entry point | Additional input |
| --- | --- | --- |
| Six public-SDK native helpers, including `popen` and process CPU time | `tools/build_native.py` | Manifest-locked OH native SDK |
| Generic JIT file-cache implementation | Manifest `rebuild.py --profile art` | Manifest-locked ART/dependency source and toolchains; full process acceptance remains separate |
| Window policy object | `tools/build_window_policy.py` | Manifest SDK/clang and checked-in 75-header closure |
| Linked window policy DSO | Same tool with `--providers` | Exact `libipc_single.z.so` and `libutils.z.so` in `window-providers.lock.json` |
| Unsigned comparison APKs | `tools/build_parity_apps.py` | Android SDK build-tools 34.0.0/API34, pinned in `android-sdk.lock.json`, plus host Java |
| Native contract/stress probes | `tools/build_parity_probes.py` | OH SDK or Android NDK 25.2.9519653 |

The Android SDK/NDK, OH firmware/provider DSOs, proprietary APKs and signing
identities are external inputs. They are not silently downloaded or replaced.
The manifest inventories the immutable runtime files observed on the reference
device, separately from the source-build inputs. A passing component build is
not a complete framework/boot-image rebuild or acceptance of the freshly linked
ART in the older device stack.

## Build helpers and comparison apps

From this repository, with paths supplied for your workspace:

```sh
python3 tools/build_native.py --sdk /workspace/toolchains/ohos-sdk/native --out /workspace/out/native
python3 tools/build_window_policy.py --sdk /workspace/toolchains/ohos-sdk/native \
  --clang /workspace/toolchains/clang-15 --out /workspace/out/window-object
python3 tools/build_parity_apps.py --sdk /opt/android-sdk --variant control24 --out /workspace/out/benchmark
python3 tools/build_parity_apps.py --sdk /opt/android-sdk --variant control26 --out /workspace/out/window-app
python3 tools/build_parity_probes.py --oh-sdk /workspace/toolchains/ohos-sdk/native --out /workspace/out/probes-oh
python3 tools/build_parity_probes.py --android-ndk /opt/android-sdk/ndk/25.2.9519653 --out /workspace/out/probes-android
```

`control22` contains the standalone socket-close control, `control24` is the
measured offline WebView workload, and `control26` adds the window/CPU API
control. The benchmark DEX rebuilt by the portable builder matches the measured
APK's DEX SHA256 `e050548eea26880e909eef32aff9568f36598f08aa8db59776ca32d6e2088cd8`.
Unsigned ZIP payloads are deterministic. Supply your own signing identity and
sign **one** APK for both devices; its signed hash will differ from the original
APK because the original development private key is not distributed.

For example, use the Android SDK `apksigner sign --ks /your/keystore --out
/workspace/out/control.apk /workspace/out/benchmark/control-unsigned.apk`.
Let the tool prompt for your keystore password. Record the final APK SHA256 and
verify the installed copy on both devices before comparing results.

## Native lifecycle comparison

With both phones attached and debug transports authorized:

```sh
python3 tools/run_parity_probes.py --hdc /path/to/hdc --oh-serial OH_SERIAL \
  --adb /path/to/adb --android-serial ANDROID_SERIAL --run trial1 \
  --oh-build /workspace/out/probes-oh --android-build /workspace/out/probes-android \
  --native-build /workspace/out/native --out /workspace/out/native-comparison
```

The runner stages only those probes in a new directory, verifies device hashes,
and checks successful/denied shell execution and concurrent stream/fork cleanup.
It uses each transport's shell identity; this is explicitly not an ordinary
app-domain acceptance test. The historical normal-app exec-denial and unchanged
Toutiao evidence remains a separate recorded check.

## Re-run the performance comparison

Use the same signed `control24` APK, framework candidate26, appspawn v15 and ART
v27 for every OH trial. The binary identity inventory and remaining framework
source gaps are in the manifest repo. Preserve the original device runtime and
use isolated data/runtime mounts. The historical `launch_probe.py` records this
isolation and its checks; adapt transport paths, host UID/token/window ID and
baseline staging to your device, rather than copying recorded PIDs or UIDs.

Create `/data/data/org.westlake.gapcontrol/code_cache/art-volatile` as that app's
UID, mode 0700. Keep `WESTLAKE_OH_JIT_ANON_FALLBACK=1` fixed. Alternate four fresh
processes: file, anonymous, file, anonymous. For the file trials set
`WESTLAKE_OH_JIT_FILE_CACHE_DIR` to that directory; for the others leave it unset.
Keep JIT and its threshold unchanged. Do not use an interpreter trial as the
anonymous control. Launch `org.westlake.gapcontrol/.BenchmarkActivity`.

Each run must produce 12 page/phase records, all focused and visible, followed
by 12 input/callback pairs. The first two page loads are warmup. On the reference
1200x1920 OH display the injection is `uinput -T -d 600 160 -i 30 -u 600 160`;
on the OnePlus display it is `adb shell input tap 550 240`. Confirm coordinates
hit the control button on your display. Keep the display awake and use a fresh
process for every run. Record display density, WebView version, network route,
APK hash, runtime hashes and input protocol.

Use `art-build/tools/verify_jit_file_cache.py` on the actual child log, maps and
identity. A file trial must show successful JIT compilation, separate shared
RW/RX views of one deleted cache inode and zero capabilities in `normal_hap`.
Then use `tools/summarize_parity.py LOG... --out summary.json`. It rejects missing,
duplicate, hidden or unfocused samples. It does not turn an incomplete run into
a lower latency result. Run `python3 -m unittest discover -s tests/parity -v` to
recompute all four published sample summaries from checked-in benchmark markers.

The historical same-binary comparison reports p90 261–309 ms with anonymous
backing and 105–114 ms with file backing, plus 10.5% less process CPU through the
last page. These are WebView callbacks, not physical presentation, live Toutiao
startup/scrolling performance, or proof of hardware-independent Android parity.

## Window, input and networking controls

A bridge built from `framework/jni/BUILD.gn` carries the window helper's native, and
`OhWindowPolicy` uses it from there. For a bridge built without it, load the helper
through `WESTLAKE_WINDOW_POLICY_PATH`. The Java `OhWindowPolicy` calls are included in
both synchronous and asynchronous
`WindowSessionAdapter.relayout` paths plus removal. The source `android_os_Process`
registration also includes `register_android_os_ProcessCpuTime`; full runtime
builders must compile `oh_process_cpu_time.cpp`. For the isolated old runtime,
preload the CPU-time and popen overlay DSOs and retain the original registrations.
The corresponding framework/JAR/boot-image build chain is still a declared
external dependency; a new helper alone does not patch an existing boot image.

Run `KeepScreenActivity`, check the native main/dialog lock sequence 1,0,1,0,1,0,
and confirm hold/release behavior past the configured screen timeout. The
`PowerManager` facade remains incomplete. Run `ImeEdgeActivity` for 17 IME groups
and `WebActivity` for network/TLS/permission/JavaScript checks. Restore the device's
previous screen timeout after tests. Do not distribute raw app data or credentials.

For the Windows USB fallback, put `tools/network/start-oneplus.ps1` and
`stop-oneplus.ps1` beside the official gnirehtet v2.5.1 release files. Supply
`-Serial`, `-Adb` and optionally `-HostInterface`. The start script verifies the
relay/client hashes and reuses only the expected relay executable. Android may
require its normal VPN consent. Record the route as USB IPv4 VPN, not Wi-Fi.

Full details and failed controls are in [the follow-up report](OH-ANDROID-PARITY-2026-09-05.md).

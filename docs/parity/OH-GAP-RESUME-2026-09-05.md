# Android reference and OH gap follow-up — 2026-09-05

The OnePlus reference phone is online through Windows over USB. OH now runs the unchanged Toutiao APK under its ordinary app domain, accepts Chinese keyboard input, loads search results, and scrolls them. The candidate remains isolated: a recurring native worker abort and a measurable page-load tail prevent promotion or a claim of full Android parity.

Evidence root: `/home/dspfac/bridge-build-arm64/gap-repair-20260904`. Earlier candidate13 reports remain historical records; their offline-phone and failed-IME observations are superseded only by the specific checks below.

## Networking reference

See [OnePlus USB networking](ONEPLUS-USB-NETWORK-2026-09-05.md) for the tested start/stop scripts and Wi-Fi diagnosis. `fw-internet` fails enterprise authentication before association/DHCP. The saved trust configuration is incomplete; credential rejection is not ruled out. No Wi-Fi credentials were changed.

The USB VPN provides working IPv4 networking, including HTTPS with normal certificate validation, WebView, and unchanged Toutiao feed/search. Leave the relay and VPN running. After reconnect, run `C:\Users\dspfa\Dev\gnirehtet-v2.5.1\start-oneplus.ps1`. Android's physical `Active default network: none` does not negate its validated VPN. The benchmark recorder now distinguishes these states.

## Changes and verification

| Boundary | Change and current evidence |
| --- | --- |
| App specialization | `appspawn-x/src/child_main.cpp` joins the top-app CPU/cpuset groups before dropping privileges and removes the capability bounding set while permitted. Private `appspawn-x-v15` runs as UID 20010054, `normal_hap`, with all five capability masks zero. |
| Runtime files | The isolated launcher maps app-readable copies of runtime files, Android resources, system configuration and private app data into a new mount namespace. This resolves ordinary-app traversal failures after reboot. It does not alter global mounts or relabel original files. The harness currently targets the imehost UID and needs installer integration before general deployment. |
| JIT memory | An OH-only, per-launch `WESTLAKE_OH_JIT_ANON_FALLBACK=1` probe uses ART's existing anonymous cache when executable memfd mappings fail with EACCES/EPERM. It is limited to private caches where the caller already allows RWX updates; zygote sharing and default behavior remain unchanged. **Code updates can temporarily use RWX.** SELinux policy is unchanged. |
| JIT controls | Normal-app native probes reproduce memfd/ashmem RX denial and successfully execute/rewrite anonymous code (42 then 43). A clean baseline ART rebuild reproduces the cache failure; the patched build starts and logs successful baseline and optimized Toutiao compilations. This establishes functionality, not a JIT speedup. |
| Interface lookup | `oh_network_interfaces.h` falls back from permission-denied interface ioctls to kernel AF_PACKET indices from `getifaddrs`. JNI bindings in `oh_network_jni.cpp` and the AndroidRuntime source use it. Root and ordinary-app probes round-trip all 33 interfaces and reject missing names/indices. The strict-built helper DSO is runtime-tested; the main AndroidRuntime source integration was not separately rebuilt. |
| Network/TLS controls | `resume20web-final-child.log`: ten network groups pass, including multicast, IPv6 loopback, GC/concurrency, asynchronous close and observed numeric descriptor reuse in 30/32 trials. Four certificate cases pass; the modern standard peer-chain API returns four certificates. Hardware local WebView/DOM completes. `resume19web-online.log` independently records HTTPS WebView/DOM completion. IPv6 loopback does not imply IPv6 Internet through the USB tunnel. |
| IME | `resume20ime-current.log`: 17 assertions pass, including actual Pinyin subtype/session binding, grapheme deletion, a 40,000-character document, stale edits and a second window. Fresh Toutiao runs show successful native attach. Wake/host-focus preflight is now recorded; this does not retrospectively prove the cause of every old attach failure. |
| Real app | Toutiao 13.9.0 is unchanged on both devices: SHA-256 `a1112a0c941f865847c2fab9138cf815735268fbfcd41d697412fb80992c7395`. OH touch input types `nihao`, chooses `你好`, and submits search. A diagnostic CDP read reports title `你好-头条搜索`, ready state `complete`, 1,616 text characters and height 4,159 CSS pixels. A focused touch swipe changes scrollY from 0 to 594.5. This verifies content/input/scroll behavior, not physical presentation latency. |

The real-app DOM check uses candidate21's opt-in `WESTLAKE_WEBVIEW_DEBUG` hook in `OhWebViewBridge.java`. Existing off-thread view-tree WebView getters returned empty URL/title and invalid settings values even while content was loaded. The debugging port was removed and the diagnostic process stopped afterward. Failed scroll attempts while the screen was locked remain recorded; only the focused before/after pair supports scrolling.

## Matched local performance

Both devices ran control APK SHA-256 `44816d9ac706bc94ff2e54780b8a1ad368c5a8c8c71f081232d9dec01b0f7d83`, with the same 10,979-character page and 400 CSS-pixel layout. Each run has 12 loads, excludes the first two, and records 12 injected taps. Other diagnostic ART children were stopped; Android Toutiao was stopped for the two listed reference runs. The Windows USB VPN was active, although the measured page is local.

| Run | Page-ready median / p90 | Release-to-click median | Release-to-frame-callback median |
| --- | ---: | ---: | ---: |
| OH resume20bencha | 99.5 / 264 ms | 6 ms | 11 ms |
| OH resume20benchb | 98 / 231 ms | 8 ms | 19.5 ms |
| Android oneplus20b | 115 / 135 ms | 18 ms | 30 ms |
| Android oneplus20c | 112.5 / 137 ms | 16.5 ms | 29 ms |

The OH slow tail repeats at loads 10 and 11; its cause has not been profiled. Similar medians do not establish equivalence. Devices, WebView versions, thermal/background state and injection protocols differ. These are WebView visual-state and Java click/frame callbacks, not touch-to-photon measurements or a matched live Toutiao benchmark. Raw samples and nearest-rank p90 are in `resume20-benchmark-summary.json`.

## Retained artifacts and open gates

`verify_resume.py` verifies recorded controls, hashes and observations and writes `resume-verification.json`. Success means the evidence is internally consistent, not that every gap is closed. Candidate21 has 64 pinned build artifacts; its staging returned `GAP_INPUTS_OK`. The private shared payload has a 354-file hash inventory. Clean ART build/link records are in `resume16-art/`; appspawn build records are in `resume15-spawn/`.

`resume21-preservation.log` confirms global ART MD5 `a5436670f182aa3e24fde289ed38a0cd`, original Toutiao data inode 49251 and accepted copied-profile inode 50270. The historical candidate13 ART is distinct from global ART and was not overwritten. Original APK/data, global runtime/HWUI, CA trust stores and OS policy were preserved. The temporary display timeout was restored.

Remaining work before promotion:

- Diagnose the repeated `SIGABRT` in native `work_thread` in both real-app runs. The process continues and the tested UI works, but this is not a clean fatal-signal gate.
- Profile and reduce the repeated page-ready tail with controlled A/B runs, then measure matched live workloads. No new compiler optimization or native-speed parity is claimed.
- Complete physical presentation and rotation/floating-keyboard validation. Geometry controls alone do not certify those interactions.
- Resolve stock-Android asynchronous-close assertion failures in the reference diagnostic APK before using them as cross-platform parity gates. OH-specific hidden-API reflection failures on stock Android are not valid common gates.

The deprecated peer-certificate-chain API, Android IME APK execution and vendor-private/privileged keyboard operations retain the limitations documented in the preceding report.

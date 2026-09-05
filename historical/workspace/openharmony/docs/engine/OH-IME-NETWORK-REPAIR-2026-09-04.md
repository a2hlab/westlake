# OHOS IME and networking repair — 2026-09-04

Follow-up: the subsequently approved ability-owned window repair now passes
screen-touch and ASCII keyboard flows in Toutiao. See the
[IME follow-up report](OH-IME-ABILITY-WINDOW-REPAIR-2026-09-04.md) for its evidence,
remaining gaps and current live processes. The results below record the earlier
network-only candidate and its then-blocked IME state.

The isolated new-JIT candidate now passes HTTPS and IPv4/IPv6 UDP controls, and
Toutiao Recommended headlines/images render again. **IME remains blocked by
window ownership/focus. This is a partial repair, not acceptance of the whole
stack or native-performance parity.**

## Implemented changes

1. **Crypto backend selection.** Android ignores writes to the read-only
   `java.vendor` property. The former workaround therefore left
   `AndroidDigestFactory` failing initialization when Conscrypt was absent.
   The pinned Bouncy Castle input now permits its existing Java digest backend
   only with the explicit `WESTLAKE_USE_BC_DIGESTS=1` platform setting; Conscrypt
   preference and the original failure path otherwise remain. The trust bridge
   no longer attempts to spoof the vendor. No empty fake crypto provider was
   installed and no certificate/hostname check was disabled.
2. **HTTP ALPN convention.** `SSLSocket.getApplicationProtocol()` returned an
   empty string for no negotiated protocol. The boot HTTP client's platform
   adapter passed that straight to `Protocol.get`, producing
   `IOException: Unexpected protocol:` after a successful TLS handshake. The
   adapter now normalizes the empty result to `null`, allowing its existing
   HTTP/1.1 fallback. It does not invent a negotiated protocol.
3. **UDP boundary.** The deployed partial platform initializer left
   `OsConstants.IP_MULTICAST_ALL=0`; the OHOS SDK defines it as 49. The isolated
   platform input initializes it correctly, with the corresponding source
   correction in the runtime's fallback table. This exposed three missing
   libcore natives, now supplied by a small POSIX JNI extension: InetAddress
   `bind`, `sendtoBytes`, and `recvfromBytes`. It handles dual-stack addressing,
   IPv6 scope IDs, array/direct-buffer bounds, packet source addresses, EINTR,
   and errno propagation. It registers only those three missing methods and
   opens no sockets/IPC clients in the prefork parent.
4. **IME implementation prepared, not accepted.** The missing `OhImeBridge`
   Java class and forwarding adapter are included. A new ARM64 helper uses
   OHOS's public input-method C API instead of the old ARM32 helper/private
   C++ interface. Loading is child-only and lazy. All required editor callbacks
   are registered, edits are posted to the Android UI thread, and show/hide
   report actual native results. The old 20-second auto-hide path is not used.

Source/build entry points:

- [Trust bridge](/home/dspfac/bridge-build/src/framework/appspawn-x/java/adapter/security/OhTrustBridge.java)
- [POSIX JNI extension](/home/dspfac/bridge-build/src/framework/android-runtime/src/oh_network_jni.cpp)
- [IME Java bridge](/home/dspfac/bridge-build/src/framework/window/java/OhImeBridge.java), [IMM adapter](/home/dspfac/bridge-build/src/framework/window/java/InputMethodManagerAdapter.java), [C-API helper](/home/dspfac/bridge-build/src/framework/window/jni/oh_ime_helper_capi.cpp)
- [Pinned Java input transformation](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/build_java.py), [native build](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/build_native.sh), [boot build](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/build_boot.sh), [safe staging](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/stage.py)

## Verification

| Check | Observed result |
|---|---|
| SHA-256 known-answer test | Correct digest for `abc`; `java.vendor` remains `The Android Project` |
| Platform trust store | `OhSystemTrustManager`, 118 accepted roots from the existing OHOS trust inputs |
| Offline PKIX controls | Valid chain accepted; untrusted and expired certificates rejected |
| HTTPS | `https://example.com/` returns HTTP 200, with a certificate-verified TLS handshake |
| UDP | Socket creation; IPv4 and IPv6 loopback delivery; nonzero send/receive array offsets; peer port/address; two receive-timeout checks all pass |
| Toutiao | Unchanged APK; Recommended tree has populated feed rows, decoded images and visible rendered content; feed-server and CDN TLS handshakes succeed |
| Material Catalog | Launch and Slider demo navigation/render pass after giving its pre-existing offline JaCoCo agent a writable working directory |
| Stock Noice | Launch/render reached, but sound library control is inconclusive: existing app-data ownership prevents database access |
| IME | Native helper loads, but real attachment fails; no successful typing, composing, keyboard visibility, hide, or resize claim |

[Control source](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/control/MainActivity.java)
and [device log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/control05-child32588.log)
contain the positive/negative results. The fixture private keys stay on the host;
only certificates are packaged in the diagnostic APK.

[Toutiao screenshot](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/toutiao01-settled.jpeg),
[view tree](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/toutiao01-settled.tree),
and [child log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/child29738.log)
provide independent pixel, structural and network evidence. This first feed run
predates the added UDP JNI extension; final-build app evidence is archived
separately in the same repair directory.

The final build also has a [populated feed capture](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/toutiao02-feed.jpeg)
with different headlines, a [populated Search suggestions page](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/toutiao02-search.jpeg)
instead of the previous network-error screen, and its own
[child log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/toutiao02-child3328.log).
Neither capture demonstrates keyboard input. An untimed Back returned to the
home tree after the check.

[Material Slider tree](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/material02-demo.tree)
and [log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/material02-child2321.log)
show the successful control. The initial Material failure was `/jacoco.exec`
on a read-only working directory, not a TLS failure. Noice's
[log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/noice01-child1143.log)
reports SQLite open failures; its app-data root is owned by UID 20010053, while
the existing canned spawn client runs UID 20010042. Existing ownership/data were
not changed to force a pass.

## Why IME is still blocked

The direct launcher draws display-attached surfaces without an accepted OHOS
window session:

```text
SceneSessionManager: V7 err=10, persistentId=0, session=null
Legacy CreateWindow: ret=1005, windowId=0
Android IMM: showSoftInput=false, hasWindowFocus=false
OHOS helper: request focus=-2, attach=12800003 (IME_ERR_IMCLIENT)
```

An earlier helper attempt returned 12802000 because the required
`ReceivePrivateCommand` callback was missing. That callback is now registered;
the later [control log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/child31091.log)
records the remaining client rejection and false window-focus state. A direct
helper button was used only to diagnose the native layer, not as proof that
Android IMM works.

The [service-side log](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/ime-focus-service.log)
confirms both failures: SceneSessionManager reports `Invalid window type` for
the main-window request through its specific-session entry point, and IMSA
reports `31091/0/805331452 is not focused!`. The corresponding SceneSessionManager
source creates a main session only when the supplied property is null; the
specific-session request supplies a property. Changing a Java focus boolean
cannot repair that ownership/launch contract.

The next architectural step is a real ability-owned OHOS window and its focus
callbacks, with Android editor sessions attached through that owner. Faking
Android focus alone cannot grant OHOS keyboard ownership. This repair does not
alter the system input service, borrow an unrelated app's focus, replace the
launcher HAP, or change the default appspawn service. The new IME path should
remain experimental until ASCII/Chinese composition, delete/selection, editor
actions, dismissal and window-inset behavior pass on that launch path.

## Preservation, reproducibility and limits

- Device: OHOS serial `5cdbf6af00000000000000000923012c`. The OnePlus was not
  changed or re-benchmarked in this repair. Wi-Fi settings and app caches were
  not reset.
- Toutiao APK remains MD5 `cc99caaaa3baffbb100118753fa1d130`.
- New-JIT ART remains MD5 `c8b5e7352662109ddcb9ca3b97020894`; appspawn, main
  adapter bridge and existing runtime-boundary DSOs remain the pinned review
  inputs. The accepted v142 ART was not replaced.
- The boot image is staged in an isolated subdirectory of the existing
  compile-time trusted OAT root. No ART trust check was weakened.
- All auxiliary DEX files and resource entries are byte-preserved. The first
  experimental build accidentally omitted the adapter's `classes2.dex`; this
  was corrected before the passing controls, and a regression check now checks
  every non-replaced ZIP entry. Initial malformed HDC staging directories were
  moved to recoverable `*.bad-hdc-layout` locations.
- [Offline validator](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/validate.py):
  seven build/evidence checks pass. These are not seven independent live runs.
  [Validation and SHA-256 inventory](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/validation.json)
  record the exact isolated outputs. Staging was also tested to refuse writes
  while repair processes map its runtime.
- This is not complete socket/JNI coverage: direct-buffer traffic, asynchronous
  close races, multicast, Unix-domain overloads and GC stress remain untested.
  The small UDP extension does not yet integrate Android's asynchronous-close
  monitor; it is not a replacement for a complete upstream libcore native port.
  Existing signed-JAR provider initialization errors in Toutiao still reference
  missing Conscrypt; the observed feed recovery does not establish full runtime
  compatibility. Article navigation and native-scale latency parity remain
  separate acceptance work.

Run instructions are in the [repair README](/home/dspfac/bridge-build-arm64/ime-network-fix-20260904/README.md).
Do not promote this combined candidate as an IME-complete baseline.

Final device state: isolated repair parent 2744 / Toutiao child 3328 remain
available. Disposable diagnostic/control parents were stopped. The old default
parent and review parent were not replaced, and v142's device MD5 was rechecked
as `20e48b90b6b01468f155ed7c48097e70`.

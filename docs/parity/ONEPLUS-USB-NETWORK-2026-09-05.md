# OnePlus USB networking — 2026-09-05

The OnePlus 6 (`cfb7c9e3`, Android 15) now has working IPv4 networking through Windows over USB. Wi-Fi association with `fw-internet` remains unresolved; it is no longer required for Android reference tests.

## Verified

- Windows relay plus ADB reverse and the Android gnirehtet VPN. Android reports the VPN connected and validated.
- Normal TLS verification: `https://example.com/` and `https://www.toutiao.com/` both return HTTP 200 from the phone.
- The unchanged gap-control APK completes Java HTTPS with a standard four-certificate peer chain, and WebView loads example.com with JavaScript/DOM completion.
- Unchanged Toutiao 13.9.0 displays a populated feed, accepts the `OpenHarmony` search query, and displays search results. These observations are functional checks, not matched content or physical-presentation timing measurements.
- Both restart scripts were exercised: stop, start, then HTTPS 200 again.

## Restart

Files are in `C:\Users\dspfa\Dev\gnirehtet-v2.5.1`:

```powershell
.\start-oneplus.ps1
.\stop-oneplus.ps1
```

Run the start script after a USB reconnect. It verifies the pinned relay/client hashes, checks the selected ADB device, reads Windows Wi-Fi IPv4 DNS, reuses only the expected relay executable, and restores the Android tunnel. `-HostInterface` selects another Windows network interface. `stop-oneplus.ps1 -StopRelay` also stops the verified relay process; otherwise the relay remains available.

This uses the official [Genymobile gnirehtet v2.5.1 Windows release](https://github.com/Genymobile/gnirehtet/releases/tag/v2.5.1). The downloaded archive SHA-256 is `7f5b1063e7895182aa60def1437e50363c3758144088dcd079037bb7c3c46a1c`. No CA certificates were added and TLS verification was not bypassed.

## Wi-Fi finding and measurement limits

The phone repeatedly fails enterprise EAP authentication before obtaining a Wi-Fi IP. Its saved PEAP/MSCHAPV2 profile has no selected CA certificate or domain suffix; the settings editor shows “Please select” for CA. Windows connects using a profile with a pinned CA. Incomplete trust configuration is supported by the evidence, but the logs do not conclusively distinguish it from credential rejection. Credentials and the saved Wi-Fi configuration were not changed.

The tunnel supplies IPv4 TCP/UDP, not IPv6 Internet access. Label subsequent measurements as USB VPN through Windows; tunnel latency cannot be treated as native Wi-Fi performance. Android may print `Active default network: none` for the physical network while its VPN is validated and functional. `measure_oneplus.py` now records the validated VPN separately.

Several OH-specific reflection probes in the same diagnostic APK are inaccessible on stock Android and are not valid Android parity gates. The Android run also records asynchronous-close assertion failures; those are retained for separate investigation and do not invalidate the successful HTTPS checks.

Evidence: `/home/dspfac/bridge-build-arm64/gap-repair-20260904/wifi-20260905/`, especially `tunnel-results.json`, `tunnel-https.log`, `control-online.log`, `toutiao-query-late.xml`, and `tunnel-restart-https.log`.

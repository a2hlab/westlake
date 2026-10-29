# Hong Kong top 1000: plan and progress (2026-10-27, updated 2026-10-28)

## The goal

Make the 1000 most popular Android apps in the Hong Kong market work on OpenHarmony through
Westlake, with Google Play services supplied by microG. Budget is not the constraint; the order of
work follows measured reach across the corpus, not the order apps happen to be launched in.

Why this target: "draws its first screen" was about 96% saturated on the earlier 371-app corpus,
which was mostly F-Droid apps, and drawing is not working. Commercial apps are where the value is,
and they differ in two ways the old corpus hid. 29 of 30 sampled commercial apps reference Google
Play services (sign-in and push 29, location 24, Play Integrity 22, Maps 10) against 4 of 341 F-Droid
apps, and 30 of 30 reference video decoding, which did not work at all.

## The plan

1. **Corpus.** A ranked list of 1000 HK apps (706 apps, 294 games), fused from all 102 HK Play charts
   (top free and top grossing, overall and per category). Replace the ranking with purchased
   usage data when it is bought.
2. **Static baseline.** Map every APK with the harness's gap map (manifest, dex, native libraries)
   so each open row carries its reach: how many of the 1000 need it.
3. **Device rounds.** Launch the top 100 on the board for every candidate stack, scored by lifecycle
   stage (runtime-init, bound, activity, view, drawing) against the previous round; a change becomes
   the default only after a round shows no regressions.
4. **Fix by reach.** Group the blockers by cause and fix the largest groups first. Each port gets
   an app-independent acceptance test (a probe line that flips from fail to pass) before it is
   checked on apps.
5. **Google services through microG.** GmsCore and the microG Companion run as guest packages
   inside each app's process, as the WebView does, with their real certificates
   (`docs/MICROG-INTEGRATION-DESIGN-2026-10-24.md`). Phases: core services, push while running, push
   while closed, then location, Maps and attestation.
6. **Widen.** Rounds over ranks 101-1000, then functional checks (sign-in, playback, push) beyond the
   first screen.

Needed from outside engineering: a board fleet (about 10, for nightly 1000-app rounds), a clean HK
network for the boards (the board network resolves some domains wrongly), Google and app test
accounts with HK numbers, and market data.

## Progress

### Corpus and static baseline (done)

- 982 of the 1000 downloaded with arm64 code, from APKPure, Google Play and one from Huawei
  AppGallery. The 18 missing are watch faces, Taiwan-region games, and delisted or region-locked apps.
  APKPure often ignores the requested ABI, so each download is checked and v7a-only copies are
  fetched again.
- All 982 mapped (harness benchmark `2026-10-25-hk-top1000-static-baseline`). Reach of Google
  services: 957 use some part, push 861, location 811, Billing 875, Play Integrity 531.
- The static draw/blocked rule fitted on the old corpus does not hold for commercial apps (52% right
  against 60% for "always draws"), so device rounds decide.

### Device rounds, top 100

| round | stack | drawing | change |
|---|---|---|---|
| first | build 120 | 59 of 98 | baseline |
| microG | build 123 (microG guests) | 66 of 99 | +7 |
| media | build 124 + media natives + new shim | 72 of 99 | +6, 9 advanced |

### Device rounds, ranks 101-300

| round | stack | drawing | change |
|---|---|---|---|
| first | build 124 + media natives | 118 of 196 (60%) | baseline |
| rerun of the 78 not drawing | build 124, shim with the 10-28 fixes | 15 of the 78 now draw | about 133 of 196 (68%) |

The 15: Alipay, Life360, TP-Link Tapo, StarMaker, Telegram X (Challegram), Magic Tiles, a Unity
title from Cocone (it draws its own network error dialog, likely the board network), two short-drama
apps, two PDF readers, two cleaners, an AI language tutor and an AI photo app. The 118 that drew
before were not relaunched in the rerun.

What moved apps, in order:

- **Process identity (build 122).** The process name, `ApplicationInfo.processName` and the
  `restrictions` service: Adobe Acrobat and FuSu draw.
- **microG guests (build 123).** PairIP's Java licence check (224 of 982 apps) binds the Play Store's
  licensing service and showed an undrawn error dialog when nothing answered. With the microG
  Companion answering, Grok, All Browser, Crushie, TransitNow and QuickDoc draw. This is a real
  licence answer, not a bypass.
- **Shared libraries apps declare (build 124).** `org.apache.http.legacy`, built from pinned AOSP
  source, which 489 of 982 apps declare: YouTube, JOOX, Facebook and Messenger needed it.
- **Native interfaces.** libnativewindow (Maps, YouTube and 10 more), `android_get_device_api_level`,
  ASurfaceControl answered as absent, missing AMediaCodec calls, and the APK's own copies of a
  written library's dependencies (Messenger).
- **Media.** MediaMetadataRetriever, MediaExtractor and MediaCodec video over OH's media APIs; before
  this, no app could decode video. The media probe passes 7 of 8 checks.

Board findings along the way: the hardware H.264 decoder lists Main and High but decodes only
Baseline, so Main and High streams (nearly all real video) go to OH's software decoder after a
one-frame self-test per profile. There is no software HEVC decoder. Surface output reports size-0
frames, which players drop. Concurrent `getgrnam` calls corrupt musl's heap.

A check of 14 video-heavy apps after the decoder change showed no regressions, with 11 drawing.

What moved apps in ranks 101-300 (2026-10-27 to 10-28):

- **ImageReader** over an OH consumer surface, with Android's public format map: the media probe
  now passes 8 of 8. ExoPlayer's `setVideoScalingMode` and `setOutputSurface` calls answered.
- **Loading the C library by path.** OH's `/system/lib64/libc.so` is a link to musl's dynamic
  linker; `System.loadLibrary("c")` mapped a second musl and the process died without a word
  (StarMaker, Magic Tiles, the Unity title). Such a load now gets the running libc.
- **Thread-specific keys.** musl indexes keys 0-127 without a check; bionic checks. A key of 128 from
  an ads SDK corrupted memory in Magic Tiles; out-of-range keys are now refused.
- **Linker scripts named .so.** An APK's text `libc++.so` was staged as a library and shadowed a
  system dependency; non-ELF files are no longer staged.
- **EGL through eglGetProcAddress.** Engines that take every EGL entry point from
  `eglGetProcAddress` on a `libEGL.so` handle (Cocos) now get Westlake's window handling.
- **Earlier the same week:** ProtonVPN's Go library crash on the new stack is gone (it draws), the
  WebView start-up hold is released for ads SDKs that start from a provider, and Rive's GL pointers
  are bound at staging.

### Where the rest stop (ranks 101-300, 63 apps)

- **App protection, about 20, not being worked on.** PairIP's native library (10, crashing before the
  Application exists or stalling in its VM), commercial packers (Ijiami, Bangcle, Baidu, DexProtector
  and others), and one runtime-protection library that HSBC, PayMe and Octopus share, which installs
  its signal handlers and then exits.
- **Fixed, being checked on the board now:**
  - the process name in `/proc/self/cmdline`, which stayed the host's name after the app renamed its
    process: a string decoder that checks it failed, and SDKs use it to recognise the main process
    (the likely cause of several "not initialised" failures: MMKV, Kotlin `lateinit` properties);
  - `Process.readProcLines`, missing (Youku);
  - null constructor entries in libraries an app downloads at run time, which the launcher already
    cleans for packaged ones (Honor of Kings);
  - the in-process Settings provider installed after the app's own providers, so a provider that
    read a setting at start-up failed (Yandex Browser). This one is in framework build 125.
- **Draws a view but not a frame (14):** individual causes. Cocos games create their surface and
  render frames that never show; Flutter's image view needs `Image.getHardwareBuffer`; a few resume
  their activity and never draw.
- **Per-app:** Unreal Engine's `NativeActivity.loadNativeCode` (PES), Roblox, Netflix's native
  crash, Flightradar24 opening the Play Store from its splash screen, Lottie failing to parse an
  animation, and a vendor GPU driver abort.

## Next

1. **Check the four fixes above** on their apps and on apps that already draw, then make build 125,
   the new shim and the new runtime override the defaults.
2. **Ranks 301-1000.** Rounds of about 200 overnight on the current stack; group the blockers by
   cause as before.
3. **Video playback, not just decoding.** A probe check that plays the clip and counts rendered
   frames; software decoder speed at 720p and 1080p on the board; how often HEVC appears.
4. **microG phase work.** Sign-in and push, once test accounts exist.
5. **Draws a view but not a frame:** the Cocos surface that renders but never shows, Flutter's
   hardware-buffer image view, and the keep-screen-on window flag, whose library is not staged.
6. **Per-app crashes** after the shared causes are cleared.

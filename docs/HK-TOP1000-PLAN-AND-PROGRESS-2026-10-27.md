# Hong Kong top 1000: plan and progress (2026-10-27)

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

### Where the rest stop

- **Before the app starts (11):** mostly PairIP's native library (`libpairipcore.so`, 78 of 982
  apps), which crashes before the Application is created. Not being worked on.
- **Draws a view but not a frame (8)** and **bound or at the first activity (7):** individual causes,
  including myTV SUPER (a missing class at start-up), DramaWave (finishes its own activity) and
  TataLand (black after its intro video).
- **Native crashes in the app's own libraries:** ProtonVPN (its Go library, a regression on the new
  stack), Kling (Cronet start-up), KKBOX, Meitu, Douyin, Bigo.

## Next

1. **ProtonVPN regression.** Bisect the shim and the runtime override with two launches, and fix the cause.
2. **Ranks 101-300 overnight.** The first fresh evidence on the new stack; group its blockers by cause.
3. **ImageReader port** over OH_NativeImage (756 of 982 apps; camera, scanners, GPU readback), with
   the probe check as its acceptance test.
4. **Video playback, not just decoding.** A probe check that plays the clip and counts rendered
   frames; software decoder speed at 720p and 1080p on the board; how often HEVC appears.
5. **microG phase work.** Sign-in and push, once test accounts exist.
6. **Per-app crashes** after the shared causes are cleared.

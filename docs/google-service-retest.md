# Retest of the 26 apps with recorded Google service refusals

All **26/26** selected packages were retested. **25/26** primary
processes were alive at baseline capture, **26/26** had at least one successful
Google service callback, and **0/26** had a sampled status-9 callback.
**0** had no sampled callback. These are connection/startup
results, not 26 full-app passes. FotMob's interactive Google sign-in **failed** at
Westlake's cross-package pending-activity routing; no account picker was reached.

Baseline runs span **2026-11-16T07:18:11+00:00** to **2026-11-16T08:25:49+00:00**.
Other sampled callback codes appeared in: 13=2 apps, 16=1 app, 23=17 apps.
Status categories overlap when an app has both successful and refused services.

This report compares a fresh build-11 board run with the retained post-restriction
logs for 26 HK-corpus packages. It measures startup, sampled Google service
connections and the displayed screen. FotMob additionally received an explicitly
authorized Google sign-in attempt. It is not a new 1,000-app acceptance run.

The earlier failures were observed on Westlake's implementation of an official-GMS
sandbox approach. They were not tests of GrapheneOS itself. GrapheneOS runs the
official Google apps with its compatibility layer as ordinary sandboxed apps;
Westlake additionally has to implement Android contracts on OpenHarmony.
[GrapheneOS documentation](https://grapheneos.org/usage#sandboxed-google-play).

## What the status comparison means

For broker connection callbacks, Google defines `0` as a successful connection,
`9` as `SERVICE_INVALID`, `13` as `CANCELED`, `16` as `API_UNAVAILABLE`, and
`23` as `API_DISABLED`. FotMob's displayed sign-in error uses a separate API/UI
context and must not be interpreted merely by matching the same number.
A service connection is not
proof of a returned location, displayed ad, account login, purchase or successful
integrity verdict. [ConnectionResult reference](https://developers.google.com/android/reference/com/google/android/gms/common/ConnectionResult).

The historical selection contains 26 packages with a status-9 callback in the
65 retained post-cutoff logs for 27 packages. Twenty-three of the 26 also had a
saved drawing score. The larger audit did not establish that all Google-dependent
apps, Maps or location were broken. The previously quoted 15% was not
reproducible as a validated failure rate; startup API exposure alone does not
establish that an app is broken.

Current official GMS had recorded `uncertified_status=1` before and after each
completed baseline, without a local override. Earlier restriction investigations
recorded 3. This test did not manually register a GSF ID, change the policy value,
or obtain certification. It also did not reproduce the old failure and then
change only one variable. Differences can reflect host state, runtime fixes,
configuration or time. **An improvement here does not prove that the token proxy
removes Google's registration restrictions.**

## Method and provenance

- Westlake source: `51d53662e1f13749b8bcbe074cc19bbdf8b9ece0`.
- Runtime adapter SHA-256:
  `59864362f89b0f9d01a02bec9e1582b67d988cd5c546f22ec2321355f03ff2f3`.
- Manifest base: `b76ad3e8d48699f15e05fc52b10209edb2eeae0d`, with 22 original
  corpus input pins added to the four already present for this target set.
- All 26 APK inputs were reconstructed from existing archives and verified
  against pinned hashes, sizes and signing certificates. No APK was modified
  or re-signed. Per-app APK and certificate hashes are in the evidence JSON.
- Each app received fresh private data and its own runtime namespace. Apps ran
  sequentially, with a requested 90-second observation after the launcher
  completed; early deaths retain their actual observation duration. No baseline
  UI taps were made. FotMob's later interaction is a separate result.
  An initial host runner was interrupted during FreeReels setup before its
  baseline capture. That exact app namespace was stopped and its incomplete
  artifacts retained privately; the table uses the subsequent fresh 90-second
  run. The final batch used a detached host runner.
- Apps connected to the existing official GMS and Store hosts. MicroG retained
  the account credential. Corpus apps had no direct microG auth connection.
  The Google hosts were kept running and their adapter hashes checked before
  and after each baseline. The test retained the existing per-app native-library
  remedies from the corpus map.
  Each retained client runtime's adapter file was also hashed after its run and
  matched build 11; that check is distinct from the live Google host checks.
- Per-process input, private Google logs and enforced channel identity were
  enabled. GNSS was disabled as in the existing board fixture. This run does not
  test location delivery. Native test processes share UID 20010056; production
  per-app security isolation is not established by this fixture.
- Callback records are sampled at power-of-two counts. Their counts are not the
  number of real API operations. `afterService` is process-global, so attribution
  to a specific service is approximate during concurrent requests. Interface
  invocation records support that a path was reached, but a transport reply of
  zero does not establish the API's semantic result.
- Screenshots were manually reviewed. Private raw logs, account-bearing captures
  and disassembly remain local. Public evidence contains selected numeric/status
  fields, public package identifiers, hashes, and authored UI observations only.
  A global display capture can expose a previously open app when the target
  loses its window/focus. Such captures are explicitly marked as not showing
  the target and never counted as target UI success.

## FotMob Google sign-in: a remaining local blocker

FotMob `239.17834.20261016` previously had a service-212 identity refusal. In the
fresh baseline, service 212 connected with status 0 and the app invoked
`ISignInService`. That recovery did not complete sign-in.

The actual interaction was **Already a user? Sign in → Sign in with Google**.
The app displayed **“13: There was a problem signing in with Google. Please try
again.”** The runtime emitted:

```text
[WL-PLAY-TRACE] out android.content.IIntentSender code=5721153 -1 x1
[WL-REMOTE-ACTIVITY] time=1794814526536 pending_activity_rejected type=SecurityException
```

Transaction `5721153` (`0x574c41`) is Westlake's private same-app activity-intent
preparation protocol. `SocketBinder.Proxy.onTransact` rejects a foreign or
unverified hop, and `LocalIntentSenders.SenderBinder` requires a verified caller
from the same app. `ActivityTaskManagerAdapter.startActivityIntentSender` also
resolves the prepared activity only within the caller's app. FotMob's Google
identity flow needs a cross-package sender, which this implementation does not
yet route. No Google account picker, delegated FotMob token, or successful result
was observed. The error-13 UI is a separate app-level outcome from broker status
9; it is not evidence of a new Google registration refusal.

An exact private delegated grant had been added for the verified FotMob APK
signer, the server OAuth client actually referenced by its compiled login code,
and the existing narrow identity scopes. No wildcard grant or direct-client
credential access was added. That grant cannot fix an activity route rejected
before the Google UI and account-selection witness exist.

The Google sign-in test stopped at the failure. No new account was created and
no additional credential was entered. After the captured error screen, FotMob
also exited with native `SIGTRAP` / `TRAP_BRKPT` on thread `Chrome_InProcRe`, about
19.5 seconds after the rejection. The crash report's top frames were unmapped;
the root cause of that later failure is not established. Its baseline was alive at 90 seconds, which must not
be read as survival through the later interaction.

The next implementation should execute a foreign `PendingIntent` through its
verified creator, preserving the capability, original caller, activity result
and cancellation relationship. It must not make the existing same-app intent
extraction operation available to foreign apps. Required checks include creator
and caller identity, one-shot/cancelled sender semantics, immutable fill-in
behavior, URI-grant restrictions, death/expiry cleanup and a live account
selection witness. Denial cases and a real FotMob picker/result round must be
validated before claiming that modern identity flows work.
Fixing the activity route would expose the next stage; FotMob ID-token issuance,
consent handling and app-server acceptance were not reached in this test.

Spotify's earlier `RESULT_OK` and transition to its Create account form remain
valid evidence for that tested legacy flow; they do not cover FotMob's different
pending-intent route. See [Spotify delegation validation](google-auth-delegation-validation.md).

## Per-app baseline results

The status column lists all sampled callback codes for the app. It is not a
pass/fail grade. Startup/onboarding screens do not establish full business flows.

| Rank | App / package | Alive at capture | Observed callback statuses | Visible result / remaining gap |
| ---: | --- | --- | --- | --- |
| 8 | DramaWave - Dramas & Reels<br>`com.dramawave.app` | yes | 0, 23 | Device rejection notice: please use a real device to log in |
| 42 | BlinkDrama: Short Dramas & TV<br>`com.blink.drama.player` | yes | 0, 23 | Episode player controls and gift icon, but video area black at capture |
| 62 | FreeReels - Dramas & Reels<br>`com.freereels.app` | yes | 0, 23 | Device rejection notice: please use a real device to log in |
| 100 | QuickDoc: PDF Reader Tools<br>`com.quickdoc.pdfreader` | yes | 0 | Language selection screen; ad placeholders remained empty. Document opening/editing not tested. |
| 114 | VibeShort: AI Dramas & Reels<br>`com.vibeshort.visualnovel.android` | yes | 0, 23 | Dark, empty app area at capture despite process survival and window relayouts; content/playback not established. |
| 118 | Document Reader & PDF Editor<br>`all.documentreader.filereader.office.viewer` | yes | 0, 23 | Full-screen advertisement visible, with a PDF-update warning creative and ad-choice marker; provider not established. Document opening/editing not tested. |
| 136 | TradingView: Track All Markets<br>`com.tradingview.tradingviewapp` | yes | 0, 23 | TradingView splash screen remained at capture. Charts, market data and account flows not reached. |
| 214 | MEGA<br>`mega.privacy.android.app` | yes | 0, 23 | Onboarding carousel with Sign up and Log in rendered. Account access and file transfer not tested. |
| 221 | FotMob - Soccer Live Scores<br>`com.mobilefootie.wc2010` | yes | 0, 16, 23 | Welcome/Quick Setup screen rendered. Existing-user Sign in -> Sign in with Google -> error 13. No account picker. |
| 389 | Remote for Smart Samsung TV<br>`com.boost.samsung.remote` | yes | 0, 23 | Subscription offer screen rendered; no trial or purchase selected. TV discovery, remote control and casting not tested. |
| 527 | PixAI: AI Anime Art Generator<br>`io.mewtant.pixaiart` | yes | 0, 23 | Welcome/Get Started screen with artwork rendered. reCAPTCHA execution, login and image generation not tested. |
| 544 | iOS Control Center - Panel<br>`com.bytebloom.control.center.ioscontrol.bb` | yes | 0 | Paid theme preview with Apply Theme rendered. No theme applied or purchase made; system-control behavior not tested. |
| 558 | Easy Drum - Real Music Drum<br>`easy.drum.real.music.drum.set` | yes | 0 | Target app not visible: global display capture showed the retained Spotify signup screen. Easy Drum process and service connections survived; target UI, audio and input not established. |
| 578 | Real Drum electronic drums set<br>`br.com.rodrigokolb.realdrum` | yes | 0, 13, 23 | Target app not visible: global display capture showed the retained Spotify signup screen. Real Drum process survived; target UI, audio and Play Games flow not established. |
| 628 | Atom Browser<br>`com.atom.browser.surf` | yes | 0, 23 | Browser home with default-browser prompt rendered. Default was not changed; website navigation/downloads not tested. |
| 637 | 蛋仔派對<br>`com.netease.eggypartyhmt` | yes | 0, 13 | Target game not visible: global display capture showed the retained Spotify signup screen. Primary process survived; Play Games callback 13; gameplay and game UI not established. |
| 647 | Car Play Sync for Android Awto<br>`com.genieeplus.carplay.sync` | yes | 0, 23 | Target app not visible: global display capture showed the retained Spotify signup screen. Primary process survived; vehicle pairing and screen/control integration not tested. |
| 652 | Broken Screen: Magic Wallpaper<br>`com.azmobile.broken.screen.wallpaper` | yes | 0 | Subscription/free-trial offer rendered. No trial started; wallpaper download/apply and billing not tested. |
| 669 | Perfect365: Face Makeup Editor<br>`com.arcsoft.perfect365` | yes | 0, 23 | Get Started/existing-account welcome screen rendered. Camera, editing and account login not tested. |
| 671 | Emoji Font Keyboard & Themes<br>`com.asianmobile.fontskeyboard` | yes | 0 | Full-screen ad/player placeholder with play and sound icons plus ad-choice marker. Playback and keyboard enable/input not established. |
| 731 | Football Live Scores & News<br>`com.soccer.football.livescores.news` | yes | 0 | Premium/free-trial offer screen rendered. No purchase submitted; live scores, news and alerts not tested. |
| 870 | Spinner Merge Blitz: Spin Top<br>`com.YoBroStudios.MergeSpinner` | yes | 0 | Spinner Merge privacy dialog visible over retained Spotify background. Continue not selected; main game scene and gameplay not established. |
| 894 | Artrace: AR Easy Draw & Sketch<br>`com.draw.sketch.ardrawing.trace.anime.paint` | no | 0, 23 | Primary process crashed after 37.5 seconds of observation: SIGSEGV in libpag.so on tgfx_JNIEnviron thread. Root cause unresolved; no final live-app screenshot or drawing/camera test. |
| 899 | Getcontact<br>`app.source.getcontact` | yes | 0, 23 | Onboarding with Accept & Continue rendered. No phone verification submitted; contacts, caller identification and alerts not tested. |
| 964 | Dopples World<br>`com.tutotoons.app.dopplesworld` | yes | 0 | Target game not visible: global display capture showed the retained Spotify signup screen. Primary process survived; game scene, gameplay and audio not established. |
| 968 | Wattpad - Read & Write Stories<br>`wp.wattpad` | yes | 0, 23 | Signup/existing-account screen rendered, including Google option. No signup/login attempted; story reading and synchronization not tested. |

## Previously refused services

The table compares only app/service pairs that had historical status 9. Status
columns overlap for mixed results. A missing callback or an unrequested service
is unobserved, not recovered. Per-service callback attribution is approximate;
see the method section above.

| Service | Historically refused apps | Requested again | Status 0 observed | Status 9 observed | Other status observed | No callback observed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 GAMES | 2 | 2 | 0 | 0 | 1 | 1 |
| 8 ADMOB | 17 | 17 | 16 | 0 | 1 | 1 |
| 14 WEAR | 1 | 1 | 0 | 0 | 1 | 0 |
| 51 PHENOTYPE | 2 | 2 | 2 | 0 | 0 | 0 |
| 93 MEASUREMENT | 26 | 26 | 25 | 0 | 1 | 1 |
| 116 GASS | 20 | 20 | 19 | 0 | 1 | 1 |
| 126 SMS_RETRIEVER | 1 | 1 | 1 | 0 | 0 | 0 |
| 161 CAST_API | 1 | 1 | 1 | 0 | 0 | 0 |
| 212 AUTH_API_IDENTITY_SIGNIN | 1 | 1 | 1 | 0 | 0 | 0 |
| 258 AUTH_BLOCKSTORE | 4 | 4 | 3 | 0 | 0 | 1 |
| 270 CLIENT_TELEMETRY | 10 | 10 | 2 | 0 | 10 | 0 |
| 300 APP_SET_ID | 26 | 26 | 25 | 0 | 0 | 1 |
| 322 DEVICE_PERFORMANCE | 1 | 1 | 1 | 0 | 0 | 0 |
| 326 TIME | 1 | 1 | 1 | 0 | 0 | 0 |
| 380 RECAPTCHABASE | 1 | 1 | 1 | 0 | 0 | 0 |


## Evidence and remaining validation

- [Per-app JSON](../tests/google-auth/service-retest11/apps.json) contains exact
  APK/signing hashes, earlier source-log references, current callback sets,
  native signal markers and manually reviewed UI descriptions.
- [Per-app CSV](../tests/google-auth/service-retest11/apps.csv),
  [aggregate counts](../tests/google-auth/service-retest11/summary.json),
  [service comparison](../tests/google-auth/service-retest11/services.json),
  [sanitized sampled IPC events](../tests/google-auth/service-retest11/traces.json),
  [provenance](../tests/google-auth/service-retest11/provenance.json), and
  [FotMob interaction](../tests/google-auth/service-retest11/fotmob-signin.json).

The independent account/feature work still needed includes modern identity
pending-intent routing, completed logins on existing app accounts, actual
location fixes, map output, provider-attributed ads, push delivery, billing,
integrity-verdict acceptance, media playback, and each app's main user journey.
Splash screens, empty content, app device-rejection notices and later crashes
remain failures or gaps even when Google service connections succeed.

This retest does not establish universal Play services support, certification,
a registration workaround, or production sandbox isolation. The two-path
authentication result remains specific to the previously validated Spotify
handoff and the tested token policy; FotMob exposes an additional contract gap.

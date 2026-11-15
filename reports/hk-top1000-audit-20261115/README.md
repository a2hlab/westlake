# Hong Kong top-1,000 app evidence audit — 2026-11-15

All 1,000 ranked entries have an individual record. **785 have an accepted drawing result in their latest saved launch, 193 have a lower result, and 22 have no scored launch. This is not evidence that 785 apps fully work today.** The latest launch results mix builds and dates from November 6–14. The fresh work in this audit is an APK dependency scan and evidence reconciliation, not a new 1,000-app board run.

Open [the searchable offline report](index.html), [all 1,000 detailed descriptions](all-apps.md), or [the complete CSV](apps.csv). [apps.json](apps.json) carries descriptions plus structured evidence; [apps-evidence.json](apps-evidence.json) is the underlying ledger.

## Scope and meaning of “running”

The list is the project's frozen 1,000-package HK corpus: 706 apps and 294 games, assembled from 102 HK Play charts sampled on October 23. Its rank is a combined chart score, not a current market-share or usage ranking. Every listed rank is accounted for; 982 local archives were freshly scanned, including nested APK splits. Eighteen have no local archive. The download registry has 980 records, which is a different count from available archives.

| Latest saved launch result | Apps | What it establishes |
|---|---:|---|
| drawing | 785 | The saved scorer accepted app drawing in that run; this can include screenshot/hilog overrides |
| view | 56 | A view was reached, but the final app drawing result was not accepted |
| activity | 2 | Activity creation reached; no accepted drawing result |
| bound | 16 | Application bind reached; no accepted activity/drawing result |
| runtime-init | 119 | Runtime initialization reached; app progressed no further in that score |
| unscored | 22 | No retained scored launch |

829 apps drew at least once historically. Forty-four of those have a lower latest result. Builds, sample duration and test paths differ, so this is not a controlled regression count. Drawing, Google refusal and interaction failure are overlapping dimensions: **23 of the 26 apps with observed Google refusals also have a latest drawing score.**

## What was actually tested

- 978 packages have saved launch scores across 301 distinct scored rounds. Repeated progress tables within a round are deduplicated to the last row per package.
- 515 packages have exploration records: latest result survived for 315, ended during exploration for 176, and had mostly empty view trees for 24. The remaining 485 have no exploration record. Exploration taps do not prove a meaningful user journey; empty trees do not prove empty pixels.
- Only six packages have retained feature-specific scripted checks. Their latest checks total 16/16 assertions on November 11. These assertions are limited UI markers/actions, not login, payment, playback or end-to-end acceptance.
- This audit parsed 1043 selected raw app logs: the latest scored log per app, the complete l8f sample, and retained logs after the reported restriction time. Earlier historical raw Google traces outside that selection were not exhaustively rescanned.
- No complete, version-matched, current-build business-flow acceptance is established for the corpus by these records.

| App | Latest launch score | Latest scripted assertions | Assertion scope |
|---|---|---:|---|
| Spotify: Music and Podcasts | drawing | 2/2 | consent: ; wait: Sign up free |
| Instagram | drawing | 3/3 | wait: Continue; consent: Continue; wait: Get started |
| WhatsApp Messenger | drawing | 2/2 | consent: Agree and continue; wait: Link as companion device |
| YouTube | view | 5/5 | consent: ; wait: Home; tap: Shorts; wait: reel_watch_fragment_root; back:  |
| Uber - Request a ride | drawing | 1/1 | wait: embedded_web_view |
| Rakuten Viber Messenger | drawing | 3/3 | wait: Start now; tap: Start now; wait: Enter your phone number |

YouTube illustrates why these columns stay separate: its checks passed 5/5, while its saved final score is `view`, with a stall and a note that the app left/died before the final screenshot. Spotify's 2/2 includes reaching “Sign up free”; it does not establish music playback. Uber's 1/1 does not establish location or booking a ride.

## Google restriction: observed facts and limits

The board research reports an uncertified-state expiry at **2026-11-14 15:36:46 UTC**. The l8f cohort ran earlier, 05:08–13:14 UTC, and contains 335 packages, 308 drawing. Later s251a/s251b permission A/B rounds cover 23 packages each, with 21/20 drawing. They are targeted samples, not a new full-corpus score.

There are **65 retained post-cutoff logs for 27 ranked packages**. Twenty-six packages contain at least one callback with **ConnectionResult 9, SERVICE_INVALID**. Telegram X's later logs have only unidentified-interface trace activity and no identified service-broker callback, so its Google service behavior remains unobserved. The count is **26 observed affected packages**, not 26% of the corpus and not an estimate of population prevalence. Individual app logs do not themselves contain the certification warning text detected by this parser; the attribution to the board's uncertified gate comes from the separate board/code investigation.

Google defines status 9 as invalid Play services/authenticity, not a universal code for certification failure. The association with this board's restriction therefore relies on the independently recorded board state. Status 0 proves connection to a service, not successful business output. [Google ConnectionResult reference](https://developers.google.com/android/reference/com/google/android/gms/common/ConnectionResult). Device certification is also distinct from Play Protect malware scanning. [Google certification guidance](https://support.google.com/googleplay/answer/7165974).

The strongest post-cutoff counts are MEASUREMENT 26, APP_SET_ID 26, GASS 20 and ADMOB 17. The targeted set also has one identity-sign-in refusal. There is no sampled post-cutoff location-service refusal here; that means **not observed**, not “location works.” Cached/install-time modules can still work: PR 166 records Cronet loading, so “all Google modules are blocked” is too broad.

### Fresh SDK scan versus runtime evidence

| Feature family | APKs with matching DEX types, of 982 | l8f apps requesting related broker IDs, of 335 | Post-cutoff apps with related status 9, of 27 logged |
|---|---:|---:|---:|
| Google sign-in / identity | 953 | 37 | 1 |
| Google location | 816 | 35 | 0 |
| Google Maps SDK | 182 | not measured here | not measured here |
| Ads API types excluding Advertising ID | 542 | 72 | 21 |
| Advertising ID | 785 | not measured here | not measured here |
| Play Integrity | 535 | not measured here | not measured here |
| Legacy SafetyNet | 26 | 2 | 0 |
| Google Wallet / Pay | 70 | 1 | 0 |
| Play Billing | 880 | not measured here | not measured here |
| Firebase Cloud Messaging | 864 | not measured here | not measured here |
| reCAPTCHA | 202 | 6 | 1 |
| Play Games | 173 | 60 | 2 |

These columns are separate measurements, not a funnel over matched feature invocations. Service-ID mappings are explicitly listed in [google-features.csv](google-features.csv). Maps, FCM, Billing, Integrity and Advertising ID also use providers/direct interfaces or other paths, so no broker count is supplied where this parser cannot establish one. Zero broker refusals does not establish feature success. The Ads count excludes the identifier-only namespace; the broader `gms:ads` prefix matches 818 archives and should not be called a count of apps that display ads.

The very high sign-in presence count includes SDK type references bundled transitively, such as GoogleSignInAccount. Type presence is not a reachable call, enabled UI feature or confirmed dependency at runtime. Reflection, obfuscation, web flows and native code can evade the scan. Fresh DEX hashes and example descriptors are in [google-static-scan.json](google-static-scan.json). Counts may overlap and must not be summed.

### The quoted “15%” is not a validated block rate

Within l8f, 64/335 apps requested sign-in IDs 91/212 or location ID 23. Adding ADMOB 8 and GASS 116 gives **133/335**, which is 13.3% of all 1,000 ranks and only an observed startup-exposure floor for those exact services. Adding games, wallet and other services changes it again. Considering every service outside the reported allowlist yields **320/335**, dominated by measurement and app-set/telemetry requests. Those are potential exposures under the later gate, not 320 demonstrated broken apps. The 15% claim cannot be reproduced without specifying the exact API set and denominator.

Registering the board cannot be assumed to solve every gap. PR 168's board test shows the corrected Gservices replacement semantics, yet Google still sends `uncertified_status=3`. Registration outcome, service access and Play Integrity backend acceptance each need separate evidence.

### All 26 packages with observed post-cutoff service refusal

| Rank | App | Package | Latest launch | Refused services across post-cutoff logs |
|---:|---|---|---|---|
| 8 | DramaWave - Dramas & Reels | `com.dramawave.app` | drawing | ADMOB, MEASUREMENT, GASS, CLIENT_TELEMETRY, APP_SET_ID |
| 42 | BlinkDrama: Short Dramas & TV | `com.blink.drama.player` | drawing | ADMOB, MEASUREMENT, CLIENT_TELEMETRY, APP_SET_ID |
| 62 | FreeReels - Dramas & Reels | `com.freereels.app` | drawing | MEASUREMENT, GASS, CLIENT_TELEMETRY, APP_SET_ID |
| 100 | QuickDoc: PDF Reader Tools | `com.quickdoc.pdfreader` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 114 | VibeShort: AI Dramas & Reels | `com.vibeshort.visualnovel.android` | drawing | ADMOB, MEASUREMENT, GASS, CLIENT_TELEMETRY, APP_SET_ID |
| 118 | Document Reader & PDF Editor | `all.documentreader.filereader.office.viewer` | drawing | MEASUREMENT, GASS, APP_SET_ID |
| 136 | TradingView: Track All Markets | `com.tradingview.tradingviewapp` | drawing | PHENOTYPE, MEASUREMENT, SMS_RETRIEVER, APP_SET_ID |
| 214 | MEGA | `mega.privacy.android.app` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 221 | FotMob - Soccer Live Scores | `com.mobilefootie.wc2010` | drawing | ADMOB, WEAR, MEASUREMENT, GASS, AUTH_API_IDENTITY_SIGNIN, AUTH_BLOCKSTORE, CLIENT_TELEMETRY, APP_SET_ID |
| 389 | Remote for Smart Samsung TV | `com.boost.samsung.remote` | drawing | ADMOB, MEASUREMENT, GASS, CAST_API, APP_SET_ID |
| 527 | PixAI: AI Anime Art Generator | `io.mewtant.pixaiart` | drawing | ADMOB, MEASUREMENT, GASS, CLIENT_TELEMETRY, APP_SET_ID, DEVICE_PERFORMANCE, RECAPTCHABASE |
| 544 | iOS Control Center - Panel | `com.bytebloom.control.center.ioscontrol.bb` | drawing | ADMOB, MEASUREMENT, GASS, AUTH_BLOCKSTORE, APP_SET_ID |
| 558 | Easy Drum - Real Music Drum | `easy.drum.real.music.drum.set` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 578 | Real Drum electronic drums set | `br.com.rodrigokolb.realdrum` | drawing | GAMES, ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 628 | Atom Browser | `com.atom.browser.surf` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID, TIME |
| 637 | 蛋仔派對 | `com.netease.eggypartyhmt` | view | GAMES, MEASUREMENT, APP_SET_ID |
| 647 | Car Play Sync for Android Awto | `com.genieeplus.carplay.sync` | view | ADMOB, MEASUREMENT, GASS, AUTH_BLOCKSTORE, APP_SET_ID |
| 652 | Broken Screen: Magic Wallpaper | `com.azmobile.broken.screen.wallpaper` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 669 | Perfect365: Face Makeup Editor | `com.arcsoft.perfect365` | drawing | PHENOTYPE, MEASUREMENT, GASS, CLIENT_TELEMETRY, APP_SET_ID |
| 671 | Emoji Font Keyboard & Themes | `com.asianmobile.fontskeyboard` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 731 | Football Live Scores & News | `com.soccer.football.livescores.news` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 870 | Spinner Merge Blitz: Spin Top | `com.YoBroStudios.MergeSpinner` | drawing | MEASUREMENT, APP_SET_ID |
| 894 | Artrace: AR Easy Draw & Sketch | `com.draw.sketch.ardrawing.trace.anime.paint` | view | MEASUREMENT, GASS, AUTH_BLOCKSTORE, CLIENT_TELEMETRY, APP_SET_ID |
| 899 | Getcontact | `app.source.getcontact` | drawing | MEASUREMENT, CLIENT_TELEMETRY, APP_SET_ID |
| 964 | Dopples World | `com.tutotoons.app.dopplesworld` | drawing | ADMOB, MEASUREMENT, GASS, APP_SET_ID |
| 968 | Wattpad - Read & Write Stories | `wp.wattpad` | drawing | MEASUREMENT, CLIENT_TELEMETRY, APP_SET_ID |

## Launch blockers and API adaptation gaps

The 193 non-drawing latest scores comprise 124 native-crash, 16 stall, 12 app-framework, 10 self-exit, 9 no-frame, 2 native-loading, 2 self-finish, 2 self-kill, 1 native-upcall, 1 native-symbol and 14 unclassified results. These are saved scorer labels, not newly verified root causes. In particular, **124 native crashes does not mean 124 Bionic/musl defects**; causes may include app code, runtime, graphics, bridge behavior or external libraries.

The old map120 scan contains **38,030 non-supplied, non-C0 candidate rows across apps, covering 553 unique candidate IDs**. These include framework natives, package manager, system services, external dependencies and native symbols. They are from baseline commit `e8d4b20272fff46b97c49737129f373ff43a1f34`, not today's implementation. One row may cover an API family or policy, and the same row recurs across many apps. Neither number is a count of APIs newly implemented, APIs currently missing, or apps currently broken. Every row is retained in [historical-api-gaps.csv](historical-api-gaps.csv); per-app records show categories and source references.

Current cross-app implementation gaps, based on open PR descriptions retrieved for this audit (author-reported board results, not independently rerun here):

| Area | Recorded implementation/test | What remains unproven |
|---|---|---|
| Location — [PR 164](https://github.com/a2hlab/westlake/pull/164) | Requests reach OH location; permission/switch paths exercised | No fix delivered to an Android listener; no network provider and vendor GNSS failure on this board. GNSS also disrupted WiFi; fallback disables GNSS use |
| Notifications — [PR 165](https://github.com/a2hlab/westlake/pull/165) | Eight probe notifications shown/cancelled in OH | Actions/content intents, icons/pictures and dismissal feedback; this does not prove FCM delivery |
| Play modules — [PR 166](https://github.com/a2hlab/westlake/pull/166) | Read-only descriptors/copies checked; install-time Cronet module loaded | Downloaded ads module end-to-end loading remains gated; copy mode is opt-in |
| Permissions — [PR 162](https://github.com/a2hlab/westlake/pull/162) | 23-app phase-A A/B, 21 versus 20 drawing | Enforcement switch remains off; full-corpus A/B and per-app runtime permission semantics remain incomplete |
| Gservices — [PR 168](https://github.com/a2hlab/westlake/pull/168) | Full replacement removed stale keys on build 264 | Server still returns uncertified status 3; a lifted restriction has not been demonstrated |

These PRs were OPEN at retrieval; availability on a developer board does not mean merged default support. Raw snapshots and head commits are in [implementation-pr-snapshots.json](implementation-pr-snapshots.json).

## All unscored packages

| Rank | App | Package | Evidence gap |
|---:|---|---|---|
| 126 | K Cash | `com.konew.fintech.corp` | archive missing |
| 219 | StardustTV - Movies & Dramas | `com.shdj.test` | archive missing |
| 241 | myTV SUPER | `com.tvb.mytvsuper.atv` | archive present; target-abi-unavailable |
| 242 | Fuban | `com.blessedmate.fabian` | archive missing |
| 359 | Tabelog - Food Guide in Japan | `com.kakaku.tabelog.tourists` | archive missing |
| 368 | Quranify: Quran Audio Player | `com.mchutov.Quranify` | archive missing |
| 377 | PER23 Digital Watch Face | `com.watchfacestudio.per023pro` | archive missing |
| 395 | 瑪奇 Mobile | `com.nexon.devcat.mm.tw` | archive missing |
| 420 | DAZN - Watch Live Sports | `com.dazn` | archive present; no-packaged-native-libraries |
| 447 | 惡鬼狩獵：終夜 | `com.egsl.zhy.gp1` | archive present; target-abi-unavailable |
| 564 | Prestige: Watch Faces Wear OS | `com.alexeymorozua.prestige` | archive missing |
| 579 | OkCupid Dating: Date Singles | `com.okcupid.okcupid` | archive missing |
| 597 | Fallout Pip-Boy SE Watch Face | `com.facer.pipboy_se` | archive missing |
| 717 | TAG HEUER CARRERA 3 Watch Face | `com.watchfacestudio.lbclassic06` | archive missing |
| 792 | Blackmagic Camera | `com.blackmagicdesign.android.blackmagiccam` | archive missing |
| 796 | Architect: Land of Exiles | `com.hybeim.architect` | archive present; target-abi-available |
| 800 | Runna: Running Plans & Coach | `com.runbuddy.prod` | archive missing |
| 924 | 異環 | `tw.com.iwplay.nte` | archive missing |
| 926 | 勝利女神：妮姬 | `com.gamamobi.nikke` | archive missing |
| 963 | DanceHit: AI Dance Generator | `com.scaleup.dancehit.ai` | archive missing |
| 969 | Pokémon Champions | `jp.pokemon.pokemonchampions` | archive missing |
| 996 | 道友請渡劫 | `com.skyhorse.dyqdj.tw.gp` | archive missing |

## How to close the gaps

1. Freeze the candidate Westlake build, Play services version, permission/module switches, APK hashes and board Gservices state. Capture before/after state for any registration experiment.
2. Rerun the 978 available previously scored apps on that one build; acquire the missing 18 and resolve the four archive-present unscored entries. Score launch separately from core behavior.
3. Start deterministic feature tests with the 26 refusal-observed apps, the l8f sign-in/location group and finance/transport/messaging apps. Test sign-in tokens, location fixes, map tiles, push after process death, ad callbacks, sandbox billing and backend integrity decisions separately.
4. Reproduce the 193 launch findings, prioritize the 124 native crashes with symbolized evidence, and rescan API gaps against the candidate implementation. Do not treat the old map120 counts as the current backlog.
5. Add full core-flow assertions for the 994 apps without retained scripted checks, plus interaction coverage for the 485 with no exploration. Measure memory, CPU, battery and background behavior in controlled longer runs; the launch corpus does not answer those questions.

## Evidence provenance and reproducibility

Input root aliases in the files: `corpus/` = `/home/dspfac/a2hlab/source-closure/inputs/corpus-hk`; `logs/` = `/home/dspfac/a2hlab/source-closure/inputs/logs` (an existing symlink into the external-disk log archive); `audit/` = this report directory. Source files were read only; no board session was started or changed. The collector retained source SHA-256, size, file timestamp and stability in [source-hashes.json](source-hashes.json). All 4,621 source reads were stable during each read; this is a rolling evidence snapshot, not an atomic snapshot of the live project. File mtimes are ordering proxies, not trusted launch timestamps. Package identities join the data; current registry versions are not proof of the APK version used in an older round.

The DEX scan completed at 2026-11-15T18:09:09.485028+00:00. Runtime traces and scores are historical. SDK namespace rules are preserved in the scan output; older November 13 intent-action scans use a different taxonomy and are not directly comparable. Service-ID names come from harness commit `6a22d743fa487797943199355990d712977b4c95`. The restriction-state source is `research_notes/Play certification workarounds and registration/coordinator_verified_facts.md`, supplemented by the retrieved PR 166/168 board descriptions.

Scripts are under `../../research_notes/hk-top1000-audit-20261115/`: `scan_google.py`, `collect_evidence.py`, then `render_report.py`. Run them in that order. [artifact-hashes.json](artifact-hashes.json) identifies the generated snapshot and scripts. [validation.json](validation.json) records reconciliation checks. No raw account identifiers, coordinates or raw UI hierarchy text are copied into this report.

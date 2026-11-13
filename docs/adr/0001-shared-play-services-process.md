# ADR-0001: Run unmodified Play services as one shared, unprivileged Westlake process

- **Status:** accepted (transport and design; the legal questions below remain open)
- **Amended 2026-11-13:** decision 2 no longer holds for Play services' own processes. With
  `:persistent` and `.unstable` folded into one process, 13 of the 15 API families that apps reach
  through Play services' broker answered SERVICE_INVALID (measurement: 0 of 300 apps connected,
  build 225). Running its declared processes as [ADR-0002](https://github.com/a2hlab/westlake/pull/107)
  describes connected every family the first samples reached (measurement: 15 of 15 apps, builds 230
  and 231, [PR #157](https://github.com/a2hlab/westlake/pull/157), open). Full-corpus results follow.
- **Date:** 2026-11-03
- **Extends:** [the microG integration design](../MICROG-INTEGRATION-DESIGN-2026-10-24.md), whose "gmsd" this makes the target

## Context

Apps in the Hong Kong top 1000 depend on Google Play services. Today microG runs as a guest inside
each app's process. That is simple and is getting apps to their first screen, but it has limits the
design already records: nothing runs while no app runs, several copies run at once, credentials sit
inside each app, and sign-in depends on DroidGuard. 224 of the 982 scanned apps also ship PairIP's
licence check, which microG answers only by staying silent.

GrapheneOS shows another way. It installs Google's own Play services and Play Store as ordinary
apps with no privileges, and teaches the OS to cope (`GmsCompat`, `GmsHooks`, `PlayStoreHooks` and
the `gmscompat_config` table). This ADR takes that method. It does **not** port GrapheneOS: that is a
whole OS on real Android for Pixel hardware, while Westlake runs APKs directly on OpenHarmony, with
no container, no VM and no second Android system.

**What has been measured on the test board (build 133):**
- Play services 24.40, unmodified and with its signature verified, runs as its own Westlake process.
  It starts its persistent services, shows its Google settings screen, and stayed up for a 5-minute
  run. Three fixes got it there (westlake #65).
- Its crash handler ends the process silently on any uncaught exception, with no crash dump.
- Westlake reports API level 34, so Play services 26.x is refused as needing a newer SDK.
- Not tried at the time of this decision: sign-in, push, any other app using this process, or a run
  longer than 5 minutes. See Results for what has been measured since.

**How the two systems differ:**

| | GrapheneOS | Westlake |
|---|---|---|
| Android framework services | Real system_server | In-process adapters |
| Calls between apps and Play services | Kernel binder and the real activity manager | Local `Binder` in practice: AOSP's binder library is linked (`libwestlake_binder`) and `/dev/binder` is mapped, but no first handle exists between Westlake processes |
| Who is calling | A separate uid for each app | One OH uid for every app |
| Starting apps and processes | Real activity manager | appspawn-x direct launch |

The middle two rows are the core of the work. GrapheneOS gets them free, and Westlake has to build
them.

## Decision

1. **Make unmodified, Google-signed Play services a selectable backend.** It is never modified or
   re-signed. microG stays the default until the new backend passes the milestones below, and the
   launcher selects the backend per run.
2. **Run it as one shared process per device**, started by appspawn-x as an ordinary Westlake
   process with no activity (`gmsd`). Its multi-process components (`:persistent`, `:ui`,
   `.unstable`) start in this one process, as they do in the current spike.
3. **Keep it unprivileged.** It gets no OH permission or access an ordinary Westlake app lacks.
   Anything it expects from a privileged position gets a defined answer, or is left unavailable.
4. **Keep compatibility answers as data.** Extend `ServiceAnswers` towards what `gmscompat_config`
   holds: stubs, Gservices and Phenotype flags, components to disable, permission answers, and the
   maximum supported version of Play services. Use GrapheneOS's table as a reference for what to
   check, and check its licence before reusing any of it.
5. **Connect apps with a userspace binder over Unix sockets**, created by appspawn-x, one channel
   per app process:
   - Parcels cross as bytes, with a table for binder objects and SCM_RIGHTS for file descriptors.
   - appspawn-x knows which package it spawned, so it tells `gmsd` who owns each channel.
   - Each channel gets a virtual uid. `Binder.getCallingUid` and `getPackagesForUid` in `gmsd` answer
     from it, so Play services' caller and signature checks see separate apps.
   - The activity manager adapters route binds, starts, broadcasts, provider lookups and
     PendingIntents for Play services packages through the channel.

   Unmodified Play services rules out the design's narrow push and token protocol, because client
   libraries use many AIDL interfaces and pass binders and file descriptors.
6. **Add no attestation spoofing.** Play Integrity and device checks get honest answers. This work
   does not touch PairIP's native library or any other anti-tamper code.
7. **Keep Google's APKs out of every repository.** They stay on the test board and in private local
   inputs. Nothing here decides whether they can ship in a product.

## Consequences

**Expected gains, none yet proven here:** real sign-in, licence checks, in-app purchases, Asset and
Feature Delivery and Play Games; one owner for push and accounts; credentials out of app processes;
and a single place to fix Play services behaviour.

**Costs and risks:**
- The binder is the largest new component, and every Play services call will depend on it.
- `framework/android-runtime` holds an older binder stub whose comments say no binder library exists.
  Those comments are out of date.
- `gmsd` needs a process that outlives apps. The board image can allow that. A commercial device may
  freeze or refuse it, as the microG design notes.
- All apps share one OH uid and one file sandbox until appspawn-x has per-app mounts, so `gmsd`'s
  data is not yet protected from apps. It must not hold real user credentials before then.
- Play services is a moving target. GrapheneOS caps versions and updates its table often, and we will
  need the same habit. Running current versions also means reporting a newer API level, which could
  affect every app, or staying on an older build that Google's servers may treat differently.
- Memory and battery use for an always-on process have not been measured.
- Apps needing a certified device will still fail, as they do on GrapheneOS.

**Unchanged:** microG in-process remains the default, so current results are not at risk from this
work until a switch is decided.

## Alternatives considered

- **Keep only microG in each app.** Cheapest, and it stays as the default. It can't give one owner
  for push and accounts, or anything microG hasn't reimplemented.
- **Real Play services inside each app's process.** It needs no binder, but runs many heavy copies at
  once and shares one data directory between them. It also keeps credentials in every app.
- **A container or VM running GrapheneOS or Android.** Rejected: Westlake exists to run APKs without a
  container.
- **Kernel binder between Westlake processes.** The board's policy allows `binder` `call` and
  `transfer` between `normal_hap` processes, but not `set_context_mgr`. OH already owns the driver's
  context manager, and there is no `binderfs`. So two processes have no way to exchange a first
  handle except through OH's own IPC, and Android's and OH's binder use would share a driver. This is
  the fallback if sockets prove too slow, and it needs its own experiment first. Its appeal is that
  `libwestlake_binder` is AOSP's full binder library, so Parcels, binder objects, file descriptors
  and death notices would work with little new code, if a first handle can be exchanged. The obvious
  route is closed: of the 370 services listed in the board's `service_contexts`, policy lets none of
  `normal_hap`, `su` or `appspawn` add a service to samgr. The remaining idea, OH's ability connection,
  gives a handle only on OH IPC's own binder file descriptor, which `libwestlake_binder` doesn't use.
- **microG in the shared process, behind the same binder.** It avoids the question of Google's APKs
  and is a valid first test of the binder. It stays available if the legal answer is no.

## Open questions

1. **Legal:** may Google's APKs run on test boards, and may they ever ship in a product? This needs a
   legal answer before anything beyond the test board.
2. **API level:** report a newer level to all apps, per process, or stay on older Play services?
3. **Sign-in:** does it work with a real account, given that device checks will not be spoofed?
4. **Commercial devices:** how does a long-lived process stay alive there?

## Milestones and how we will know

| # | Milestone | Passes when |
|---|---|---|
| 1 | Stable alone | `gmsd` runs with no activity for 24 hours without its process ending |
| 2 | Binder | One test app calls a service in another process, passing a binder, a callback and a file descriptor |
| 3 | First real client | A real app's `isGooglePlayServicesAvailable` returns success through the channel |
| 4 | Caller identity | Play services reports the correct package and signature for two different apps at once |
| 5 | Sign-in | A test account signs in, and an app receives a token |
| 6 | Push | A test message reaches a running app, then one that was closed |
| 7 | Compare with microG | On the same app list, no fewer apps draw than with microG, and the Play-dependent apps improve |

Stop and re-decide if milestone 2 shows that binder objects can't cross reliably, or if milestone 5
fails for a reason that honest device answers can't fix.

## Results

**Milestone 2 passed on the test board (2026-11-04).** `SocketBinder` ran between two Westlake
processes spawned by one appspawn-x under enforcing SELinux. All checks in `tests/socketbinder`
passed: binder objects and their identity and release, callbacks on the waiting thread, file
descriptors both ways, a binder and a descriptor inside a `Bundle`, exceptions, 2,000 ordered oneway
calls, the size limit and death notices. A small call took 258 microseconds, with no kernel binder
figure to compare yet.

**Milestone 3 passed on the test board (2026-11-04).** Play services ran with no activity as its own
process, and a real app ran beside it. Google's client library inside that app returned
`isGooglePlayServicesAvailable` = 0. Real binders then served Phenotype, measurement, push
registration and wearable calls, and the app received a push token from Google.

**Milestone 5 passed on the test board (2026-11-05).** A test account signed in through Google's own
screens in Play services, with no device check spoofed. A separate test app then asked Play services'
token service, through the channel, and received an account token. An OAuth scope answered
`UnregisteredOnApiConsole` for that unregistered app, so an OAuth token is still to be shown with a
registered one. Text reached the page through a test-only path, because the on-screen keyboard does
not appear in Westlake yet.

**Push reaches a running app (2026-11-05), half of milestone 6.** Play services held its connection to
`mtalk.google.com:5228`, a test app got a token from a test Firebase project, and a message sent through
Google's FCM API arrived in that app's manifest receiver through the channel. A closed app can't be
started yet, so the milestone isn't passed.

**What push needed:** `NOT_VCN_MANAGED` and `NOT_BANDWIDTH_CONSTRAINED` in the network capabilities, which
requests built the ordinary way ask for by default, so those requests never matched in any app; an answer
for who made a `PendingIntent` an app hands over; and broadcasts passed to the app they target.

**What sign-in needed:** an account store (`LocalAccounts`), a store for check-in's Gservices values
(`LocalGservices`), content change notifications within the process (`LocalContentObservers`, which
apps elsewhere don't get yet), `startActivityIntentSender` starting the sender's own intent, and answers
for the `safety_center` and `grammatical_inflection` services.

**Also measured:**
- Play services' check-in with `android.googleapis.com` succeeds, with no spoofed answers. It starts
  from the boot broadcasts, which the host now sends to its own package.
- The availability check needs a Google-signed Play Store package, and check-in looks for Services
  Framework. Both are package details only in this process. Running Play Store's code there ended the
  process for every app when billing was bound, and Services Framework's provider needs an Application
  while Play services' own is still attaching. So billing and licence checks report unavailable.
- Two apps at once get separate virtual uids, and the process sees each one's package and certificate
  through the APIs Play services uses (`getCallingUid`, `getPackagesForUid`, `getPackageInfo`). Play
  services' own code reporting a caller has not been observed yet, so milestone 4 isn't passed.

**Limits of what exists:**
- An app states its own package and nothing verifies it. The kernel reports only a pid, all processes
  share one uid, and OH's `hidepid` hides one app's `/proc` entries from another. The channels are found
  through an abstract socket name. Decision 5's channels from appspawn-x must replace this before any
  real credentials are used.
- Each process is launched separately with its own appspawn-x, not by one appspawn-x as decision 5
  describes.
- Starting a closed app for a push message is not built.
- Play services used a full core until it had the `com.android.location.provider` library (built from
  AOSP and staged like other `<uses-library>` jars). With it, the process was about idle at the sampled
  points.
- `LocalAccounts` stores tokens unencrypted, like Android's accounts database, in a sandbox shared with
  every app. Each launch's new stage replaces it, so accounts don't yet survive a restart of the process.

## Next: the Play Store as a second shared process (plan, not built)

GrapheneOS installs Play services and the Play Store as two ordinary apps. Here the Play Store's code
must not run inside the Play services process: binding billing there ended the process every app
depends on (above). So the Play Store gets its own shared process, launched like Play services with no
activity, which apps connect to as they connect to Play services. What it would answer, from its
manifest (53.5.48, 117 services in five processes, run here as one): Play Integrity and express
integrity, licensing, in-app billing, in-app updates, split install and asset delivery, install
referrer, in-app review. In the HK top-1000 round, 45 of the first 160 apps asked for integrity, 10 for
in-app updates and 5 for split install, and got no service.

**Already in place:** the genuine, unmodified Play Store APK is staged beside Play services as package
details. Each connecting app gets its own virtual uid, so a second host sees its caller through
`getCallingUid` / `getPackagesForUid` / `getPackageInfo` exactly as Play services does. The account's
authenticator and Gservices answer from the Play services process.

**What has to change:**
1. **Routing by package.** `WESTLAKE_PLAY_SERVICES_CONNECT` names one socket and `PLAY_PACKAGES` routes
   Play services, Services Framework and Play Store to it. A client needs a socket per package
   (`com.android.vending` to the Play Store host, the rest to Play services).
2. **A host that is also a client.** The Play Store process serves apps and is itself a client of Play
   services (accounts, DroidGuard, Gservices, check-in). Today a process is one or the other, and
   `ClientSource` would claim the host's own package as remote; it must skip it. `isHost()` means
   "the Play services host" in `GuestHost`, `provider()` and boot announcement, which must stay so.
3. **Accounts across processes.** The account manager is `LocalAccounts` in any process with
   `WESTLAKE_PLAY_SERVICES_SERVE`, a store of its own with Play services' authenticator class. The Play
   Store host would get an empty store and no authenticator. Its account manager must be a proxy to
   the Play services host's `LocalAccounts`: accounts by type, user data, `getAuthToken` through the
   Google authenticator.
4. **Licensing moves.** Companion's stand-in for the licensing service (`GuestPackages.answersLicensing`)
   is removed once the real one answers.

**What it will honestly answer:** integrity verdicts come signed from Google. Device integrity will
fail (the board is not a certified device, and nothing will claim it is). App recognition is by binary
and certificate. Licensing and `appLicensingVerdict` depend on the account's entitlement: an app placed
by the launcher has no installer (`getInstallSourceInfo` reports none), so a free app the account has
never acquired is unlicensed, which is the truth. Installing apps through the Play Store (phase B, needs
Westlake to accept installs into its package system) is what makes that entitlement real.

**Order:** routing by package and the dual-role host, then the account proxy, then start the Play Store
host with no activity and watch it, then point integrity, licensing and billing binds at it, then
remove Companion's stand-in. Needs the test account signed in on the board.

## Sources

- GrapheneOS usage guide, sandboxed Google Play: https://grapheneos.org/usage
- GmsCompat app and config: https://github.com/GrapheneOS/platform_packages_apps_GmsCompat
- Framework hooks: https://github.com/GrapheneOS/platform_frameworks_base (`core/java/com/android/internal/gmscompat`)

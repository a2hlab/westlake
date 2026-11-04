# ADR-0001: Run unmodified Play services as one shared, unprivileged Westlake process

- **Status:** accepted (transport and design; the legal questions below remain open)
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
- Not tried: sign-in, push, any other app using this process, or a run longer than 5 minutes.

**How the two systems differ:**

| | GrapheneOS | Westlake |
|---|---|---|
| Android framework services | Real system_server | In-process adapters |
| Calls between apps and Play services | Kernel binder and the real activity manager | Local `Binder` only, inside one process |
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
- Its Parcel handling needs checking: this ADR assumes binder objects and file descriptors can be
  read out of Westlake's Parcel, which has not been tested across processes.
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
  the fallback if sockets prove too slow, and it needs its own experiment first.
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

## Sources

- GrapheneOS usage guide, sandboxed Google Play: https://grapheneos.org/usage
- GmsCompat app and config: https://github.com/GrapheneOS/platform_packages_apps_GmsCompat
- Framework hooks: https://github.com/GrapheneOS/platform_frameworks_base (`core/java/com/android/internal/gmscompat`)

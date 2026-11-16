# Westlake on Debian-family mobile Linux: port guide and plan

- **Status:** proposal. Nothing is built; the effort figures are order-of-magnitude judgments, not measurements.
- **Date:** 2026-11-11. Same day: OH's musl becomes part of Westlake (section 4, phase 0a); readiness for porters
  outside the project (section 11).
- **Builds on:** [ADR-0001](adr/0001-shared-play-services-process.md) (shared Play services process), the HK top-1000
  work and the harness's gap map.

## Verdict

Porting Westlake to Debian-family mobile Linux is feasible, and in several ways Linux is an easier host than
OpenHarmony.

**What carries over:**
- ART and the dex/AOT pipeline;
- the Android framework and its in-process adapters;
- the Bionic-compatibility work;
- SocketBinder and the Play services host design;
- the harness's scoring and gap maps.

**What must be rebuilt:** the layer that talks to OH. That covers surfaces, windows, ability starts, bundle queries,
permission tokens, audio, input, spawning and logging.

**Two decisions shape the port:**
- **The libc the Westlake process runs on.** Recommended: OH's musl becomes part of Westlake (section 4). Westlake
  pins its source, builds and patches it, and ships it in Westlake's runtime. On Linux it is the libc and loader of
  every Westlake process; on OH it is first the build sysroot, then, after a measured switch, the runtime libc too.
- **Where package, window and activity management live.** Recommended: implement those roles inside Westlake, behind
  OH-shaped interfaces with a second, Linux backend. Porting OH's own system services to Debian would mean rebuilding
  most of OH's userspace (section 5).

## 1. Where Westlake depends on OH today

| Role | OH mechanism | Linux equivalent | Carries over? |
|---|---|---|---|
| Spawning | appspawn-x and the host HAP `org.westlake.imehost`; children's stderr under `/data/service/el1/public/appspawnx` | A fork server (zygote-like) as a systemd user service, plus user namespaces | Namespace code (`source_app_namespace`) yes; spawner no |
| Activity starts | OH ability records, then an in-process launch. OH refuses starts of other abilities (2097205) | The in-process stack as now, plus a session broker for intents between apps | Mostly |
| Windows and surfaces | RenderService nodes, BLAST queues, AnwShim over NativeWindow, OH vsync; stacking by creation order | Wayland: `xdg_toplevel`, subsurfaces, `wl_egl_window` or dmabuf, frame callbacks | No: new backend |
| GPU | OH EGL/GLES (GLES 3 missing from the GLES2 library, mapped by the shim), Mali blob | Mesa (freedreno/turnip, panfrost, v3d) | The interposition approach does |
| Package queries | `OH_PMAdapter` bridging, staged inputs | Westlake's own package database | Java logic yes |
| Permissions | Android permissions checked through OH's AccessTokenManager (`ohos.permission.android.*`) | A Westlake-held permission store and prompts | No |
| Input and IME | Tap mailbox; no on-screen keyboard | `wl_seat`, `text-input-v3` with the shell's keyboard (squeekboard) | No, but gains a real keyboard |
| Calls between processes | SocketBinder; no first binder handle between Westlake processes on OH (ADR-0001) | SocketBinder as now, or real binder through binderfs | Yes |
| Audio and media | OH audio and AVCodec/AVPlayer; OpenSL ES ABI fixes | PipeWire; GStreamer or FFmpeg with V4L2 decoders | Partly |
| Logs and crashes | hilog, faultlog | journald or stderr, systemd-coredump | Trivial |
| Security labels | OH SELinux hap labels (appdat, `data_app_el2_file`, fifo and symlink denials) | Mostly gone; AppArmor optional | The workarounds go away |
| libc and loader | OH musl with namespaces (`dlopen_ns`) and many musl-specific fixes, from the platform and OH's SDK | Westlake's own build of OH's musl (section 4) | Yes, once Westlake owns it |

## 2. Measured OH coupling

The count covers source files referencing OH platform APIs: OH NDK headers, `OH_*` and `OHOS::` symbols, `ohos.*`
names, hilog, samgr, RenderService, access tokens and appspawn. It was taken on westlake `main` at e74934f, over
`framework/`, `native/`, `apps/`, `tools/` and `framework-tools/` (not `historical/` or `third_party/`).

It is a heuristic. A few hits are Westlake's own `OH_*` macros, which still mark OH-only paths.

**Overall:** 229 of 596 source files (38%) reference OH, but only 4,339 of 168,554 lines (2.6%) do. The OH surface is
wide but thin.

| Component | Files | Lines | Files touching OH | OH lines | What the OH lines are |
|---|---:|---:|---:|---:|---|
| framework/window | 54 | 25,528 | 48 | 1,180 | `OHOS::` C++ (RenderService, window manager), native windows |
| framework/javacore-shim | 20 | 10,667 | 7 | 591 | OH media: `OH_AVCodec`, `OH_AVFormat`, `OH_AVPlayer` |
| framework/activity | 49 | 20,950 | 33 | 525 | Ability records and starts |
| framework/core | 36 | 11,485 | 22 | 433 | `OH_AVPlayer`, `OH_AudioRenderer`, HiLog |
| framework/surface | 15 | 4,167 | 14 | 424 | Native window, buffers, RenderService surfaces |
| framework/android-runtime | 58 | 25,984 | 22 | 271 | HiLog, native window, `OH_NativeVSync` |
| framework/package-manager | 34 | 10,525 | 20 | 227 | Bundle bridging |
| framework/appspawn-x | 66 | 17,085 | 19 | 216 | The spawner itself |
| framework/webview-shim | 21 | 17,055 | 6 | 172 | OH audio (`OH_AudioRenderer`, `OH_AudioCapturer`) |
| framework/hwui-shim | 15 | 2,366 | 7 | 108 | Native window and buffers |
| native/hwui | 9 | 773 | 7 | 73 | |
| framework/contentprovider | 5 | 1,789 | 5 | 50 | |
| framework/broadcast | 6 | 828 | 6 | 29 | |
| Everything else | 208 | 19,352 | 13 | 40 | |

**What this means:** the Linux backend's work is concentrated in a few places:
- window, surface and hwui-shim (graphics and windowing);
- javacore-shim, core and webview-shim (media and audio);
- activity and package-manager (system roles);
- appspawn-x (spawning).

The rest of the framework is platform-neutral already. `tools/oh_coupling.py` reproduces the table; rerun it after the
seam work in phase 0.

## 3. Targets

**Distributions:**
- **Mobian / Debian mobile:** glibc, Phosh (on the wlroots-based phoc), PipeWire, systemd. System services come over
  D-Bus: ModemManager, GeoClue, iio-sensor-proxy, UPower.
- **postmarketOS:** Alpine, so musl, the closest libc to OH's.
- **Droidian and Ubuntu Touch on Halium:** the GPU driver is an Android vendor blob loaded through libhybris. A poor
  first target.

**Hardware.** Mainline Mesa quality decides what is usable:
- **OnePlus 6/6T (sdm845, Adreno 630):** GLES 3.2, and Vulkan through turnip; supported by Mobian and postmarketOS. The
  best phone target. The project's OnePlus 6 is the Android baseline device and the 6T the Frida device, so
  repurposing either has a cost; a third unit avoids it.
- **PinePhone Pro:** Mali with panfrost, GLES 3.1. Workable.
- **PinePhone:** Mali-400 with lima, GLES 2 only. Too weak for many apps.
- **Librem 5:** etnaviv, limited.
- **Desk spike:** a Raspberry Pi 5 on Raspberry Pi OS (Debian, Wayland, v3d with GLES 3.1).

**Prior art:**
- [Waydroid](https://waydro.id/): full LineageOS in an LXC container using binderfs. That is the container route
  Westlake set out not to take.
- [Android Translation Layer](https://gitlab.com/android_translation_layer/android_translation_layer): no container.
  It runs a standalone ART with a Bionic-to-glibc translation library and GTK. Its app coverage is small, but it shows
  the approach works on Linux, and how costly the glibc route is.

## 4. Decision 1: the process's libc — OH's musl becomes part of Westlake

**Recommended: OH's musl becomes a Westlake component.** Westlake's build makes it from source, and Westlake's runtime
directory ships it. It stops being a library Westlake finds on the platform.
- **On Linux,** it is the libc and dynamic loader of every Westlake process.
- **On OH,** it is first the build sysroot, then, after a measured switch, the runtime libc as well.

**Why:**
- **Every musl/Bionic fix carries over:** relocation renumbering, TLS slots, the recursive-mutex initializer,
  `DT_INIT`, `IRELATIVE`, fopencookie and failed-dlopen clean-up. Owning the libc lets them become fixes in the libc
  instead of workarounds around it (below).
- **Android-style linker namespaces stay.** OH's loader has them (`dlopen_ns`); glibc has no good equivalent.
- **musl's struct layouts already match Bionic's on arm64:** `pthread_mutex_t` is 40 bytes in both, against 48 in
  glibc aarch64.
- **The result doesn't depend on the distribution:** Debian, postmarketOS, desktop and embedded Linux alike.
- **It ends the dependency on OH's SDK** for the native sysroot and headers, which today come from the SDK rather than
  from anything the project holds.

### What "part of Westlake" means

1. **Source.** The manifest pins OpenHarmony's musl fork at the release matching the board (OH 6.1). The fork is
   upstream musl plus OH's porting layer, which includes the namespace-aware loader. `fetch_sources.py` fetches it
   hash-locked, as it does the AOSP sources.
2. **Build.** A standalone build with Westlake's clang for aarch64, outside OH's GN build. It produces `libc.so` (musl's
   libc is also its loader, `ld-musl-aarch64.so.1`), the crt objects and the headers. That set is the sysroot for every
   native Westlake component: ART, the shims and appspawn-x. There are two profiles, `oh` and `linux`.
3. **Westlake's patches live with it**, in the westlake repo (`native/musl/`), each with a test. They are the fixes
   Westlake carries today as shims and staging workarounds for OH's musl:
   - the buffered fopencookie refill that overwrites itself (Meta's superpack);
   - the crash in clean-up after a failed dlopen, while holding the loader lock;
   - skipped `DT_INIT` (packed Unity and ACE games);
   - rejected `IRELATIVE` relocations (Chromium-based browsers);
   - `DT_ANDROID_RELR`, which staging renumbers to `DT_RELR` today;
   - Bionic's 0x4000 recursive-mutex initializer, which musl reads as a normal mutex;
   - no shortname for libraries loaded by path;
   - Bionic's TLS slots above `TPIDR_EL0`.

   Some are Android compatibility rather than bugs: Android relocation formats and Bionic's TLS layout. Those apply per
   namespace or per object, so ordinary Linux libraries see plain musl.
4. **Platform hooks behind the profile.** OH-only hooks call OH in the `oh` profile and something local in `linux`:

   | Hook | `oh` profile | `linux` profile |
   |---|---|---|
   | Logging (hilog; the loader's `MUSL-LDSO` lines) | hilog | stderr or journald |
   | Parameter service | OH parameters | Westlake's property store |
   | Crash-dump signal chain | faultloggerd | plain signal handlers |
   | Malloc dispatch hooks | as OH | off |

5. **Use on Linux.** The loader in Westlake's runtime directory is the interpreter of every Westlake process: its
   `PT_INTERP`, or processes start through the loader.
   - Mesa, Wayland, PipeWire and the D-Bus client libraries are built against it into the same directory. Mesa on musl
     is routine (Alpine ships it), with one private Mesa per GPU family, as Flatpak's GL extensions do.
   - The host provides only kernel and socket interfaces: the DRM render node, binderfs, and the Wayland, PipeWire and
     D-Bus sockets.
6. **Use on OH, in two steps.**
   - *Sysroot only.* Westlake compiles against its own musl. An ABI check compares it with the board's `libc.so`:
     exported symbols, struct sizes and the namespace configuration. Processes still run on the system libc, and the
     shims stay.
   - *Runtime, measured.* Westlake processes also start through Westlake's loader on OH, so the fixes apply at the
     source on both platforms and the shims can go.
     - OH's system libraries that Westlake loads (EGL/GLES, the RenderService client, audio, hilog) then run on
       Westlake's build of the same libc, so it must match the board's ABI.
     - Switch only after a full corpus round shows no regression, and keep the system-libc path as the fallback.
7. **Tests:**
   - musl's libc-test;
   - the loader's namespace tests;
   - one test per Westlake patch;
   - the harness's calibration flows, on both profiles.
8. **Licences.** Upstream musl is MIT-licensed. Check the licence of OH's porting layer before shipping it beyond the
   test board.

**Costs:**
- the standalone build and the Linux hooks;
- moving each workaround into a patch;
- an ABI check against each board's libc on OH;
- a private Mesa per GPU family on Linux.

### Alternatives

**Option 2: retarget to glibc.** It fits Debian natively, but the compatibility layer has to be redone:
- Android's linker namespaces have no good glibc equivalent: `dlmopen` duplicates libc in each namespace.
- Struct layouts differ. `pthread_mutex_t` is 40 bytes in Bionic arm64 and in musl, but 48 in glibc aarch64.
- Bionic's TLS slots collide with glibc's static TLS.
- The stdio globals differ.

Effort: large to very large.

**Option 3: Bionic as the process libc.** Mesa, Wayland and the rest would need unusual Bionic builds, for little gain
over Westlake's own musl.

## 5. Decision 2: package, window and activity management

- **A. Port OH's services** to Debian: bundle framework, ability runtime, window manager, RenderService, samgr,
  appspawn, access tokens and hilog. They only work as a set: OH IPC with samgr, init and the parameter service, the
  display drivers, the GN build and OH's SELinux. That amounts to rebuilding OH's userspace on Debian. **Not
  recommended.** If that is the goal, run OpenHarmony on the device instead.
- **B. Implement the roles inside Westlake** behind OH-shaped interfaces, with two backends: the real OH services, and
  minimal Linux ones. **Recommended.** It also gives the OH code a single platform seam (section 2 shows where it runs).
- **C. A container.** Ruled out by ADR-0001's premise.

### Design sketch for option B

**Package manager.** `westlake install x.apk`:
- verifies signatures and parses the manifest and splits;
- chooses APK-mapped or extracted libraries (existing logic);
- runs `dex2oat` with the `verify` filter;
- creates the per-app data directory, kept across updates as the board's Play services host now keeps its own
  (harness `PERSIST_DATA`);
- writes a `.desktop` entry with the app's icon, maps its intent filters to MIME types and URL schemes, and marks it
  D-Bus-activatable.

Later it could feed GNOME Software or Discover through AppStream or PackageKit.

**Activity manager:**
- The in-process stack stays as it is.
- A session broker, over D-Bus or binderfs' service manager, handles intents and results between apps (OH has no
  result path), tasks and recents, and starting and restarting processes.
- `org.freedesktop.Application.Open` maps to `ACTION_VIEW`; external links go through `xdg-open`; pickers and share
  go through desktop portals.
- Lifecycle follows the toplevel's activated and suspended states, the lock screen (logind) and memory pressure (PSI,
  to `onTrimMemory`).

**Window manager:**
- One `xdg_toplevel` per Android task. Westlake composes that task's activity, dialog, popup and toast windows
  itself. Wayland gives clients no global positions, so Android's window-manager semantics stay under Westlake's
  control.
- SurfaceView and video go in subsurfaces placed below the parent. That fixes the SurfaceView-above-activity ordering
  behind OH's 38 black "view" apps.
- Insets come from the shell's panels and keyboard, density from the output scale, and rotation from the output
  transform, which triggers a configuration change.

## 6. Component gap map

Effort key, for one engineer who knows Westlake: S ≤ 2 weeks, M 2–6 weeks, L 1.5–4 months, XL > 4 months. These are
estimates.

| Component | Effort |
|---|---|
| Build and sysroot retarget onto Westlake's musl (ART, natives, shims, boot image) | M |
| Westlake's musl: pinned source, standalone build, sysroot, `oh` and `linux` profiles with their hooks | M |
| Westlake's musl patches: today's shims and staging workarounds moved into the libc, each with a test | S each |
| Westlake's musl at run time on OH: ABI check against the board's libc, measured switch, fallback | M |
| Private Mesa, Wayland, PipeWire and D-Bus client builds against Westlake's musl | M |
| (Alternative: retarget to glibc instead) | L–XL |
| Spawner and per-app sandbox (user and mount namespaces; Android's app seccomp filter exists, opt-in, westlake #148) | M |
| Graphics surfaces: Surface/BLAST/ANativeWindow to Wayland with EGL or dmabuf, vsync, AHardwareBuffer to GBM | L |
| Window management: task toplevels, composition, subsurfaces, insets, rotation, density | M–L |
| Input and IME (`wl_seat`, `text-input-v3` / input-method) | M |
| Activity broker, lifecycle, shell integration | M–L |
| Package manager and launcher/MIME integration | M |
| Permission model | S–M |
| Audio (OpenSL ES, AAudio, AudioTrack to PipeWire) and media codecs | M–L |
| Notifications, connectivity, location, sensors, battery, clipboard, storage | M in total, S each |
| Camera (libcamera), Bluetooth (BlueZ), telephony (ModemManager) | L each; defer, and keep feature claims honest |
| WebView: Chromium under the shim, its GPU process on Mesa | M |
| Real binder through binderfs: multi-process apps, the Play services and Play Store hosts | M |
| Harness device layer: ssh, journalctl/coredumpctl, wlr-screencopy screenshots, input injection, power | M; scoring and gap map reused |

## 7. What gets easier on Linux

- **Real binder between processes.** binderfs gives Westlake its own context manager, which is the exact blocker
  ADR-0001 records on OH. That unblocks multi-process apps (#107) and makes the Play services and Play Store hosts
  natural.
- **GLES 3.x and Vulkan are standard** with Mesa, so the GLES3 mapping workaround goes away.
- **Subsurface ordering** solves the SurfaceView problem.
- **OH's hap SELinux restrictions are gone.**
- **A system on-screen keyboard exists** through `text-input`, which Westlake on OH lacks today.
- **Tooling:** gdb, perf, strace, bpftrace and normal core dumps, with no hdc or Wi-Fi link stalls.

## 8. Risks

- **GPU and drivers.** Mainline coverage varies by device, and apps and hwui expect Android EGL extensions (native
  fences, frame timestamps).
- **Suspend and wake-ups.** Linux phones suspend the whole system. AlarmManager, jobs and push need RTC wake alarms and
  network wake, and push delivery during suspend is weak on Linux phones.
- **Security model:**
  - every app runs as one user, so per-app isolation has to come from namespaces;
  - protector and banking apps may treat the non-Android environment as tampering, and those stay observe-only.
- **Fragmentation.** Shells differ (Phosh, Plasma Mobile, Lomiri), and fractional scaling, hardware decode and camera
  support are immature.
- **Split focus.** Two platforms to maintain. Mitigate with the single platform seam and the same corpus harness on
  both.
- **Small Debian-mobile user base.** The same port would also cover ARM Linux laptops and embedded Linux.

## 9. Plan with go/no-go points

| Phase | Time | Goal | Go if |
|---|---|---|---|
| 0a, Westlake's musl | 2–4 weeks | OH's musl, pinned at the board's release, built standalone with Westlake's toolchain in both profiles; ABI-checked against the board's `libc.so` | libc-test and the loader's namespace tests pass, and the ABI check shows no difference that matters |
| 0b, headless | 1–2 weeks | The existing ART and framework on Debian arm64 on Westlake's musl (`linux` profile): dex code, a Service, the non-UI calibration flows | They boot with OH services stubbed |
| 1, first draw | 3–6 weeks | One Activity draws in a Wayland window on a Raspberry Pi 5 (Surface to `wl_egl_window`, hwui on Mesa), with touch and pointer input | It draws and takes input |
| 2, phone | 2–3 months | A OnePlus 6/6T on Mobian or postmarketOS with Phosh: keyboard, rotation, lifecycle, audio, `westlake install` and `.desktop` launch | Simple apps work end to end |
| 3, measure | — | Port the harness device layer and run 100 HK-corpus apps, for a first Linux gap map | Draw rate at least about half of OH's on the same apps, within an agreed window |
| 4, expand | — | binderfs multi-process, the Play services host, system bridges in gap-map frequency order | — |

## 10. Open questions

- **OH's musl outside OH.** Does it build standalone and run elsewhere? That includes its parameter, hilog and
  signal-chain hooks and any reliance on OH kernel features. Phase 0a answers it.
- **Westlake's musl at run time on OH.** Is Westlake's build of the board's musl close enough to the board's own for
  OH's system libraries (EGL/GLES, RenderService client, audio) to run on it? The ABI check in phase 0a says whether
  to try; a corpus round decides.
- **ART's build dependencies** on the OH SDK and sysroot, beyond the libc that Westlake's musl replaces.
- **Licences** of OH's porting layer in its musl fork, before Westlake's musl ships beyond the test board.
- **Per-directory seam.** Section 2 counts references. Turning it into a seam design means reading each OH call
  site in window, surface, activity, package-manager and the media and audio shims.
- **Effort figures** are judgments, to be replaced by phase 0 and 1 actuals.

## 11. Readiness for porters outside the project

The project holds most of the code that carries over, but a team outside it could not yet take it and port it alone.

**What exists:**
- **westlake (private):** the framework adapters and shims, `native/` (source lists for the AOSP components rather than
  the sources), tests, tools and the design documents.
- **manifest (public):** the fetch and build tooling, the launcher (`probe_source_app.py`) and the app-input pin format.
- **westlake-harness (public):** scoring, gap maps and functional checks, which are reusable. Its device layer is
  OH-specific.

**What is missing:**
1. **Access.** The westlake repo is private, and it holds nearly all the code to port.
2. **A from-scratch build.** Nobody has shown the stack rebuilding from the repos alone.
   - Builds depend on local state that no repo holds: the source-closure workspace, toolchains, compiler inputs and
     older build outputs.
   - Some build inputs were recovered from a board stage after the originals were deleted.
   - Some steps need host tools that no longer exist.
3. **Device-captured and prebuilt pieces.** The WebView payload is captured from the device by design, and one OH
   window bridge can't be rebuilt. Both are OH-board artifacts, useless on Debian.
4. **No platform seam or Linux backend.** All platform code talks to OH directly (section 2), and nothing exists yet
   for Wayland, PipeWire, D-Bus, a spawner, or package and activity management on Linux.
5. **The libc.** Section 4 now says how: OH's musl becomes Westlake's own. Until phase 0a, though, the native sysroot
   and headers still come from OH's SDK, not from the project.
6. **main doesn't match the board.** The deployed build comes from branches with open PRs (#148, #149), plus test-only
   builds kept locally.
7. **No apps to test with.** By policy, the corpus APKs and Google's APKs stay out of the repos, and the Google
   entries in the app pins are local only. Porters need their own app set to run the harness.

**What would make it a usable starting point:**
- access to westlake (or opening it), with main brought in line with what runs on the board;
- a reproducible build guide, ideally a CI build from a clean machine, with the recovered and captured artifacts
  either rebuilt from source or documented;
- Westlake's musl (phase 0a), so the native build no longer needs OH's SDK sysroot;
- the platform seam: OH calls behind interfaces, so a Linux backend can sit beside the OH one;
- the phase 0b result: ART and the framework headless on Debian arm64;
- a harness device layer for Linux: ssh, journald, Wayland screenshots, input injection.

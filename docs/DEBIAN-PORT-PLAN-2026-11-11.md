# Westlake on Debian-family mobile Linux: port guide and plan

- **Status:** proposal. Nothing is built; the effort figures are order-of-magnitude judgments, not measurements.
- **Date:** 2026-11-11
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
- **The libc the Westlake process runs on.** Recommended: keep OH's musl as Westlake's private libc (section 4).
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
| libc and loader | OH musl with namespaces (`dlopen_ns`) and many musl-specific fixes | Depends on the libc decision (section 4) | Depends |

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

## 4. Decision 1: the process's libc

**Option 1 (recommended): keep OH's musl as Westlake's private libc.**
- Westlake ships OH's musl loader with its namespace support.
- Mesa, Wayland, PipeWire and the D-Bus client libraries are built against it inside Westlake's runtime directory.
  Mesa on musl is routine: Alpine ships it.
- The host provides only kernel and socket interfaces: the DRM render node, binderfs, and the Wayland, PipeWire and
  D-Bus sockets.
- Every musl/Bionic fix carries over unchanged: relocation renumbering, TLS slots, the recursive-mutex initializer,
  `DT_INIT`, `IRELATIVE`, fopencookie and failed-dlopen clean-up.
- The result does not depend on the distribution, so it also covers desktop and embedded Linux.
- **Costs:**
  - building OH's musl outside OH, stubbing its hilog and parameter hooks;
  - carrying a private Mesa per GPU family, much as Flatpak's GL extensions do.

**Option 2: retarget to glibc.** It fits Debian natively, but the compatibility layer has to be redone:
- Android's linker namespaces have no good glibc equivalent: `dlmopen` duplicates libc in each namespace.
- Struct layouts differ. `pthread_mutex_t` is 40 bytes in Bionic arm64 and in musl, but 48 in glibc aarch64.
- Bionic's TLS slots collide with glibc's static TLS.
- The stdio globals differ.

Effort: large to very large.

**Option 3: Bionic as the process libc.** Mesa, Wayland and the rest would need unusual Bionic builds, for little gain
over option 1.

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
| Build and sysroot retarget (ART, natives, shims, boot image) | M |
| Process libc and loader | M–L with option 1; L–XL with option 2 |
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
| 0, headless | 1–2 weeks | The existing OH-built ART and framework on Debian arm64, with OH musl as a private libc: dex code, a Service, the non-UI calibration flows | They boot with OH services stubbed |
| 1, first draw | 3–6 weeks | One Activity draws in a Wayland window on a Raspberry Pi 5 (Surface to `wl_egl_window`, hwui on Mesa), with touch and pointer input | It draws and takes input |
| 2, phone | 2–3 months | A OnePlus 6/6T on Mobian or postmarketOS with Phosh: keyboard, rotation, lifecycle, audio, `westlake install` and `.desktop` launch | Simple apps work end to end |
| 3, measure | — | Port the harness device layer and run 100 HK-corpus apps, for a first Linux gap map | Draw rate at least about half of OH's on the same apps, within an agreed window |
| 4, expand | — | binderfs multi-process, the Play services host, system bridges in gap-map frequency order | — |

## 10. Open questions

- **OH's musl outside OH.** Does it build and run elsewhere? That includes its parameter and hilog hooks and any
  reliance on OH kernel features. Phase 0 answers it.
- **ART's build dependencies** on the OH SDK and sysroot.
- **Per-directory seam.** Section 2 counts references. Turning it into a seam design means reading each OH call
  site in window, surface, activity, package-manager and the media and audio shims.
- **Effort figures** are judgments, to be replaced by phase 0 and 1 actuals.

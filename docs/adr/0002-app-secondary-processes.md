# ADR-0002: Run an app's secondary processes as Westlake processes from its own zygote

- **Status:** proposed (needs a decision on priority against NativeActivity input and Play Store Phase A)
- **Date:** 2026-11-08
- **Builds on:** [ADR-0001](0001-shared-play-services-process.md), whose process start and
  `SocketBinder` this reuses

## Context

On Android, a component's `android:process` attribute decides which process it runs in. The
activity manager starts that process when the component is first needed, and the app's code can
rely on the processes being separate. Westlake runs each app as one process. Every component,
whatever process it declares, runs in that process:

- the app's own services are created and bound in process (`InProcessServiceBinder`);
- Chromium's child services (`SandboxedProcessService`, `PrivilegedProcessService`) are hosted in
  process;
- since westlake #106 the one process takes the launch activity's declared process name, so code
  that branches on the process name takes the branch for that process.

Most apps don't notice. 329 of the 982 scanned Hong Kong apps declare components in secondary
processes (the gap map's `pm:multiprocess` row). Most of those are push or helper processes (`:push`,
`:remote`) that have run in one process without a visible failure. The apps below depend on the
separation itself, and in the board rounds of 2026-11-08 they stop on it:

| App | Declared processes it needs | What happens in one process |
|---|---|---|
| Microsoft Edge | Chromium's child processes | `libchrome.so` loads, then traps on its `ChildProcessMain` thread: the child's process main runs inside the browser |
| Yandex Browser | Chromium's renderer | The renderer service, hosted in the browser's process, finds the library already set up for the browser: "Trying to change the LibraryProcessType from 1 to 2". The app's exception handler then ends the process |
| Samsung Internet | Chromium's child processes | Traps on its `ChildProcessMain` thread (`libterrace.so`) |
| Google app | `GoogleAppActivity` in `:googleapp`, `AccountSyncService` in `:search` | It binds the service, which by its name syncs account data between the app's processes, over and over: 3,838 binds in the run. The main thread skips frames and the activity never draws |
| WeChat | `CoreService` in `:push` | Ends at startup |
| Xiaomi Wear | Its device services in `:device` | Outside `:device`, `BinderDelegate.getProxy` wraps the service in a `java.lang.reflect.Proxy` built from the interfaces of the object's own class. Across processes that object is the AIDL `Stub.Proxy`, which declares the interface. In one process it is the service's implementation, whose class declares none, so the cast to `IInterface` fails at startup |

Chrome and Brave share Edge's process model. 24 scanned apps declare Chromium's
`:sandboxed_process*` services and 36 declare a `:phoenix` restart process.

No per-app workaround makes these work without changing the apps. Chromium's single-process mode is
a testing mode that a release browser does not turn on; WebView is the only Android embedder that
renders in process. Google's account sync and WeChat's push service assume separate processes in the
apps' own code. Making the environment transparent to these apps means giving them the processes.

**Parts that already exist:**

- **appspawn-x is a zygote.** It starts ART and preloads the framework once, then waits on a Unix
  socket and forks a child for each spawn request. Each request names the process. The launcher
  sends the app's request (`host_spawn`). The zygote keeps running for the life of the app, beside
  the child.
- **A process with no activity.** `WESTLAKE_NO_ACTIVITY` binds the application and starts no
  activity. The shared Play services process runs this way (ADR-0001).
- **`SocketBinder`** carries Parcels between Westlake processes as bytes, with a table for binder
  objects and `SCM_RIGHTS` for file descriptors. ADR-0001's milestone 2 passed under enforcing
  SELinux: a call between two processes spawned by one appspawn-x, passing a binder, a callback and
  a file descriptor.
- **`PlayServicesChannel`** routes binds, starts, stops and provider lookups for another process's
  components through a `SocketBinder` channel and returns real binders.

## Decision (proposed)

1. **Start a secondary process on first use.** A component whose declared process differs from the
   caller's runs in a Westlake process of that name. The first time one of that process's components
   is needed, the app process asks its own appspawn-x for it, as Android's activity manager starts
   a process on demand.
   - The request names the process and asks for no activity.
   - The child binds the same `Application` under that name: `AppBindData.processName`,
     `Application.getProcessName()` and the provider filter, as #106 does for the launch process.
2. **Connect the processes with `SocketBinder`.** The app process holds a process table: process
   name, pid and channel. A secondary process's calls to the app process's components come back the
   same way.
3. **Route by declared process.** These calls go to the component's own process and return what it
   answers: `bindService`, `bindServiceInstance`, `startService`, `stopService`,
   `getContentProvider`, and broadcasts to receivers declared there. The remote side runs the
   component as the app process runs its own today, and a binder it returns is a real
   `SocketBinder` binder.
4. **Deliver process death as Android does.** When a channel closes, the binders from that process
   die. Clients get `binderDied`, then `onServiceDisconnected` or `onBindingDied`, and a binding made
   with `BIND_AUTO_CREATE` starts the process again.
5. **Run isolated processes as ordinary processes.** An `android:isolatedProcess` service, such as
   Chromium's sandboxed renderer, gets a process of its own but not a separate uid, since every
   Westlake process shares one OH uid. This is recorded as a known difference from Android.
6. **Leave WebView in process.** It renders in process today (the framework build turns its
   multiprocess flags off), and that stays.

## Consequences

- **Memory.** Each process is a fork of the zygote, so it shares the boot image and the preloaded
  classes and adds its own heap. A Chromium browser adds a GPU process and renderers. How many the
  board sustains has not been measured.
- **Harness.** Each process writes its own log. Rounds must collect all of an app's processes, and
  the summaries need a record per process. A renderer that crashes is not the app's death, as on
  Android.
- **OH process policy.** A secondary process has no window and no OH ability. Whether OH freezes or
  ends such processes in the background on this board has to be checked.
- **Security.** All of an app's processes share one uid, so an isolated process is not isolated.
  The zygote's socket accepts a request from any process in the app's mount namespace. Only the
  app's own processes live there.

## Alternatives considered

- **One process, with a workaround per app.** A release Chromium browser does not run single-process,
  and the Google app and WeChat would each need their own code paths changed. That contradicts
  running APKs unmodified and transparently.
- **One process, with the process boundary visible to binder clients.** For a service declared in
  another process, the in-process bind would deliver a binder whose `queryLocalInterface` answers null.
  `asInterface` then builds the AIDL `Stub.Proxy`, and calls go through `transact` and Parcels, as
  across processes. That is enough for Xiaomi Wear. It does nothing for code that branches on the
  process name or needs a second process: Chromium, the Google app and WeChat. It may still be
  worth doing first, as a step toward milestone 2, since it exercises the Parcel path those calls
  will take.
- **Each process started from the host, as the Play services process is today.** That fits a
  process known before launch. A secondary process is started by the app, at a time only the app
  knows, so a request path from the app is needed either way. The app's own zygote already has the
  framework preloaded.
- **Kernel binder between processes.** There is still no first handle between Westlake processes:
  OH owns the driver's context manager (ADR-0001).
- **OH abilities as processes.** An OH service ability is not an Android process and cannot host
  one.

## Open questions

1. **Activities declared in another process.** These are rare, and the launch activity's own process
   is already handled (#106). They need a window in the secondary process.
2. **The environment a secondary process gets.** The zygote forks with the launcher's environment.
   The request has to carry what differs, at least the process name and the no-activity mode.
3. **Limits.** How many processes the board allows at once, and what OH does to processes without a
   window.

## Milestones and how we will know

| # | Milestone | Passes when |
|---|---|---|
| 1 | Spawn on request | A test app's service declared in `:remote` starts in a second process forked by the app's appspawn-x, whose `getProcessName()` is `<package>:remote` |
| 2 | Bind across | The app binds that service, gets a binder over `SocketBinder`, and calls it with a file descriptor and a callback. Unbinding and killing the process deliver what Android delivers |
| 3 | Google app | `AccountSyncService` runs in `:search`, the bind loop ends and `GoogleAppActivity` draws |
| 4 | WeChat | `CoreService` runs in `:push` and the app gets past startup |
| 5 | A Chromium browser | Edge or Chrome starts its GPU and renderer processes as processes and draws a page |
| 6 | Harness | A round collects every process of an app and attributes a secondary process's crash to that process |

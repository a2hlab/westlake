# Native watchdog signal validation

These tools diagnose the mixed ART/OpenHarmony process boundary. They do not
establish Google authentication or full application acceptance. The candidate-54
[live report](google-two-path-full-validation.md#candidate-54-retain-arts-signal-blocks-in-the-openharmony-watchdog)
records those outcomes separately.

## Observe signal delivery

Build `tools/sigpipe_watch.c` with the OH SDK clang for `aarch64-linux-ohos` and
the SDK sysroot, using `-std=c11 -O2 -Wall -Wextra -Werror`. Host x86-64 builds
also support the controlled closed-pipe self-tests:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror tools/sigpipe_watch.c -o sigpipe-watch
./sigpipe-watch --self-test
./sigpipe-watch --self-test-all
```

On the board, use a unique staging directory, verify the transferred binary's
hash, and independently verify the target's spawner, UID and executable before
invocation:

```text
sigpipe-watch PID UID EXECUTABLE TIMEOUT_SECONDS [--all-threads]
```

The default watches `OS_DfxWatchdog`. `--all-threads` also catches delivery before
that thread is visible to a slower external check. The tool verifies executable
device/inode and real/effective UID, uses `PTRACE_SEIZE`, and polls for new threads.
It reports the SIGPIPE metadata and bounded executable stack locations, then
detaches with the same signal. It does not read I/O buffers, suppress the signal,
change signal masks, or use `PTRACE_O_EXITKILL`.

Addresses labeled `offset` are **file offsets**, not ELF virtual addresses. Resolve
them through the actual mapped library's executable `PT_LOAD` segment before
symbolization. The board libraries in this investigation have a `0x1000`
difference between executable file offsets and virtual addresses. For example,
musl file offset `0x132a44` resolves to ELF address `0x133a44`, inside
`pthread_sigmask`. The board library hashes are pinned in
[candidate54.json](../tests/google-auth/full48/candidate54.json).

Tracing affects timing. Threads that start and fail between scans can be missed;
the watchdog-only mode cannot exclude a signal on another thread. A timeout or
unavailable watchdog is not a crash-free pass. Verify target and tracer state
after any interrupted run. Keep captures private until reviewed.

## Reproduce the watchdog interaction

`tools/hicollie_signal_probe.c` forks a controlled child and invokes the installed
board library through `SetTimerRust`. It blocks ART's three signals, an unrelated
signal, and the native sampler signal; it then queues its own SIGPIPE before
starting the watchdog. Its callback checks the resulting mask. No application
process, account or network is used.

Build the probe with OH SDK clang, `-std=c11 -O2 -Wall -Wextra -Werror -ldl`.
Build `tools/hicollie_signal_probe_shim.c` twice as a shared library with
`-fPIC -shared -ldl`: once with `-DWESTLAKE_PROBE_PROTECT=0` and once with
`-DWESTLAKE_PROBE_PROTECT=1`. The protected variant includes the exact production
`hicollie_signal_mask.h`; the control forwards directly to native musl.

After transferring and verifying all three binaries, run:

```text
hicollie-signal-probe /absolute/path/control.so android sigpipe
hicollie-signal-probe /absolute/path/protected.so android preserved
hicollie-signal-probe /absolute/path/protected.so native sigpipe
```

Each command must end with `expected_result=PASS`. The protected Android case
must also report that its real watchdog callback ran, SIGPIPE/SIGQUIT/SIGUSR1
remain blocked, SIGUSR2 is unblocked and sampler signal 35 stays blocked. The
native control intentionally retains the board library's original behavior.
The signal is sent only to the probe's own child.

This minimal preload isolates the production mask adapter. It is not a substitute
for rebuilding the full shim and running an APK in its normal launch environment.
Loading the complete Android compatibility preload into this standalone probe
produced SIGSEGV without reaching the pending-signal or callback markers for
both baseline and modified variants. Those attempts are not application tests
or successful integration checks.

#!/bin/bash
# ttwalk.sh — Toutiao walkthrough harness, modelled on walkcat1.sh + wl_ui.sh/wl_click_all.sh.
#
# Gates on both a laid-out Android view and a visible RenderService surface
# (NEVER a timer), writes walkpid so every liveness check names the RIGHT pid,
# then drives the app through the bridge side-channel and records BOTH
# oracles at each step:
#   pixels  -> snapshot_display + shotlit.py  (lum / lit% / colours / verdict)
#   widgets -> `echo v > noice_tap.<child-pid>` view-tree dump (rect/id/text/clickable), which is
#              INDEPENDENT of pixels -- if VT lists laid-out widgets the UI exists even
#              when the framebuffer is black.
#
# usage: ttwalk.sh [launch|respawn|vt|tap <x> <y>|shot <name>|walk]
set -uo pipefail
# PID-scoped WSL interop sockets can outlive their Windows relay. The stable
# PID-1 alias follows the live relay and keeps hdc.exe usable after reconnects.
if [ -S /run/WSL/1_interop ]; then
  export WSL_INTEROP=/run/WSL/1_interop
fi
H=/mnt/c/Users/dspfa/Dev/ohos-tools/hdc.exe
HDC_TARGET=${WESTLAKE_HDC_TARGET:-5cdbf6af00000000000000000923012c}
RUN_SCRIPT=${WL_RUN_SCRIPT:-/data/local/tmp/asx/run_tt.sh}
SP=/tmp/claude-1000/-home-dspfac-openharmony/9b468523-43c8-4767-a2c0-caaf78c59c9d/scratchpad
SHOTS=$SP/shots; mkdir -p "$SHOTS"
SHOTLIT=$SP/shotlit.py
# Scratchpad paths are session-scoped. Keep old capture locations compatible,
# but use the durable harness copy when the originating session has expired.
[ -f "$SHOTLIT" ] || SHOTLIT=/home/dspfac/bridge-build-arm64/shotlit.py
ASX=/data/service/el1/public/appspawnx
# The async UI-thread input route now preserves Android's normal RecyclerView
# gesture/click contract. Forced performClick ignores RecyclerView transforms
# and can select an offscreen child after an otherwise valid tap.
FORCE_CLICK=${WL_TOUCH_FORCE_CLICK:-0}
TOUCH_DIRECT=${WL_TOUCH_DIRECT:-0}
TOUCH_ENQUEUE=${WL_TOUCH_ENQUEUE:-1}
# The physical gsl680 panel is event2 on this board.  event5 is the VSoC
# virtual device and disappears after reconnects, leaving touchfwd alive but
# unable to deliver any events.  Keep an override for boards with a different
# input enumeration.
TOUCH_DEVICE=${WL_TOUCH_DEVICE:-/dev/input/event2}
# evdev advertises and emits display-pixel coordinates on this board. Keep an
# explicit override for calibration probes, but never infer a multiplier from
# a handful of taps in one small screen region.
TOUCH_COORD_GAIN=${WL_TOUCH_COORD_GAIN:-1}
TLS_HTTP_HEADS=${WL_TLS_DUMP_HTTP_HEADS:-0}
TLS_SEARCH_BODY=${WL_TLS_DUMP_SEARCH_BODY:-0}
FORCE_JIT=${APPSPAWNX_FORCE_JIT:-1}
PROFILED_JIT=${APPSPAWNX_PROFILED_JIT:-0}
JIT_THRESHOLD=${WL_JIT_THRESHOLD:-}
JIT_WARMUP_THRESHOLD=${WL_JIT_WARMUP_THRESHOLD:-}
JIT_BASELINE=${WL_JIT_BASELINE:-0}
BOOT_IMAGE=${WL_BOOT_IMAGE:-}
TRUSTED_OAT_DIR=${WL_TRUSTED_OAT_DIR:-}
# Optional Android-zygote-style class metadata preload. The parent loads the
# curated list without running class initializers, then children inherit the
# class tables and dex caches through fork COW.
PRELOAD_CLASS_LIST=${WL_PRELOAD_CLASS_LIST:-}
# Keep the compatibility boundary for boot classes, with the validated narrow
# source-defined org.json allowlist enabled. This is distinct from disabling
# WESTLAKE_JIT_APP_ONLY and can still be overridden to 0 for an A/B control.
JIT_BOOT_PARSER=${WESTLAKE_JIT_BOOT_PARSER:-1}
JIT_DELAY_MS=${WL_JIT_DELAY_MS:-}
# A production-shaped zygote class preload can legitimately take longer than
# the normal 90-second development parent startup on an interpreter-heavy
# build.  Keep the existing default, but make the parent-only wait explicit so
# preload experiments do not send the child spawn request before the socket is
# ready.  This time is reported separately and never counted as app launch.
PARENT_READY_TIMEOUT_SECONDS=${WL_PARENT_READY_TIMEOUT_SECONDS:-90}
TTVIDEO_CALLS=${WL_TRACE_TOUTIAO_VIDEO:-0}
NATIVE_LOAD_TRACE=${WL_TRACE_NATIVE_LOAD:-0}
NATIVE_NET_TRACE=${WL_TRACE_ANDROID_NATIVE_NET:-0}
NATIVE_WINDOW_TRACE=${WL_TRACE_ANDROID_NATIVE_WINDOW:-0}
TZ_TRACE=${WESTLAKE_TRACE_TZ:-0}
TOUTIAO_FORCE_JAVA_NET=${WL_TOUTIAO_FORCE_JAVA_NET:-0}
POST_CLICK=${WL_TOUCH_POST_CLICK:-0}
TOUCH_FD_MAIN=${WL_TOUCH_FD_MAIN:-0}
KEY_ENQUEUE=${WL_KEY_ENQUEUE:-0}
TOUCH_FORWARDER=${WL_TOUCH_FORWARDER:-1}
# Restricting Android children to CPUs 0-5 excluded this board's sole
# capacity-1024 core and made the unchanged Toutiao cold start CPU-bound.
# Two structural-gate trials with 0-7 completed in 46 s and 58 s versus
# 98-114 s for the restricted baseline, while RS composition and the physical
# touch forwarder remained live. Keep an immediate A/B rollback with value 2.
APP_CPU_RESERVE=${WESTLAKE_APP_CPU_RESERVE:-0}
# Toutiao's unchanged cold start is highly variable on this port.  Validated
# JIT runs have reached the side-channel gate after the old fixed 360-second
# ceiling, so keep the gate structural and make only the safety ceiling
# configurable.  This is a harness timeout, not an app-readiness assertion.
LAUNCH_TIMEOUT_SECONDS=${WL_LAUNCH_TIMEOUT_SECONDS:-1200}
TOUCH_ENQUEUE_ENV=
if [ "$TOUCH_ENQUEUE" != "0" ]; then
  TOUCH_ENQUEUE_ENV=WL_TOUCH_ENQUEUE=1
fi
POST_CLICK_ENV=
if [ "$POST_CLICK" != "0" ]; then
  POST_CLICK_ENV=WL_TOUCH_POST_CLICK=1
fi
TOUCH_FD_MAIN_ENV=
if [ "$TOUCH_FD_MAIN" != "0" ]; then
  TOUCH_FD_MAIN_ENV=WL_TOUCH_FD_MAIN=1
fi
TOUCH_DIRECT_ENV=
if [ "$TOUCH_DIRECT" != "0" ]; then
  TOUCH_DIRECT_ENV=WL_TOUCH_DIRECT=1
fi
KEY_ENQUEUE_ENV=
if [ "$KEY_ENQUEUE" != "0" ]; then
  KEY_ENQUEUE_ENV=WL_KEY_ENQUEUE=1
fi
BOOT_IMAGE_ENV=
if [ -n "$BOOT_IMAGE" ]; then
  BOOT_IMAGE_ENV=ANDROID_BOOT_IMAGE=$BOOT_IMAGE
fi
TRUSTED_OAT_DIR_ENV=
if [ -n "$TRUSTED_OAT_DIR" ]; then
  TRUSTED_OAT_DIR_ENV=WESTLAKE_TRUSTED_OAT_DIR=$TRUSTED_OAT_DIR
fi
PRELOAD_CLASS_LIST_ENV=
if [ -n "$PRELOAD_CLASS_LIST" ]; then
  PRELOAD_CLASS_LIST_ENV=WESTLAKE_PRELOAD_CLASS_LIST=$PRELOAD_CLASS_LIST
fi

dev() { $H -t "$HDC_TARGET" shell "$1" 2>/dev/null | tr -d '\r'; }
childlog() { dev 'ls -t '"$ASX"'/adapter_child_*.stderr 2>/dev/null | head -1'; }
walkpid()  { dev 'cat /data/local/tmp/asx/walkpid 2>/dev/null'; }
tapch()    { echo "/data/local/tmp/noice_tap.$(walkpid)"; }
proc_cpu_ticks() {
  # /proc/<pid>/stat fields 14 and 15 are process user and system CPU ticks.
  # Keep the raw kernel unit: it is stable within an A/B run and avoids
  # pretending that network/surface wait time is runtime execution time.
  dev "set -- \$(cat /proc/$1/stat 2>/dev/null); echo \$((\${14} + \${15}))"
}

awake() {
  # The configured screen-off override is not sufficient on this image: the
  # power service still reports its 30-second policy and SCBScreenLock can take
  # focus while touchfwd remains healthy.  The service's debug keep-on command
  # owns an idempotent SCREEN running lock (PowerMgrKeepOnLock).  Acquire it,
  # unlock the panel, then foreground the OH host window that owns the Android
  # bridge session.  This keeps physical CAPTCHA interaction alive after the
  # harness returns and after hdc reconnects.
  # Only send the unlock gesture when SCBScreenLock actually owns focus.  An
  # unconditional swipe after a reconnect used to scroll the foreground
  # Android page (or move a CAPTCHA puzzle) while merely trying to keep the
  # panel awake.
  dev "power-shell timeout -o 3600000 >/dev/null 2>&1; power-shell wakeup >/dev/null 2>&1; sleep 1; D=\$(hidumper -s WindowManagerService -a '-a' 2>/dev/null); F=\$(printf '%s\\n' \"\$D\" | awk '/^Focus window:/ {print \$3; exit}'); L=\$(printf '%s\\n' \"\$D\" | awk '\$1 ~ /^SCBScreenLock/ {print \$4; exit}'); if [ -n \"\$F\" ] && [ \"\$F\" = \"\$L\" ]; then uinput -T -m 600 1500 600 400 300 >/dev/null 2>&1; fi; power-shell dump -t >/dev/null 2>&1; aa start -b com.github.ashutoshgngwr.noice -a EntryAbility >/dev/null 2>&1" >/dev/null
}

shot() {  # shot <name> -> capture + quantify
  local n=$1
  dev "power-shell wakeup >/dev/null 2>&1; snapshot_display -f /data/local/tmp/s.jpeg >/dev/null 2>&1" >/dev/null
  ( cd "$SHOTS" && $H -t "$HDC_TARGET" file recv /data/local/tmp/s.jpeg ./"$n".jpeg >/dev/null 2>&1 )
  python3 "$SHOTLIT" "$SHOTS/$n.jpeg" 2>/dev/null | tail -1
}

vt() {  # dump the view tree; prints the widget lines emitted by this dump only
  # NOTE: the tag is "[N/OH_InputBridge] VT" where N varies per run, so match with a
  # wildcard, not a literal 'OH_InputBridge:'. Getting this wrong makes a WORKING
  # side-channel look dead (it reported 0 widgets while the dialog was right there).
  local C N; C=$(childlog); N=$(dev "wc -l < $C" | tr -d ' ')
  dev "echo 'v' > $(tapch)"; sleep 8
  dev "tail -n +$N $C | grep -a 'OH_InputBridge. VT'" | sed -E 's/.*VT +//'
}

taps() {  # taps <id-or-text-substring> — tap the centre of the first matching widget
  local pat=$1 line rect x y w h
  line=$(vt | grep -a "$pat" | head -1)
  [ -z "$line" ] && { echo "no widget matching '$pat'"; return 1; }
  rect=$(echo "$line" | grep -oE 'rect=\[[0-9]+,[0-9]+ [0-9]+x[0-9]+\]')
  set -- $(echo "$rect" | sed -E 's/rect=\[([0-9]+),([0-9]+) ([0-9]+)x([0-9]+)\]/\1 \2 \3 \4/')
  x=$(( $1 + $3 / 2 )); y=$(( $2 + $4 / 2 ))
  echo "tap '$pat' -> ($x,$y)  [$line]"
  tap "$x" "$y"
}

tap() { dev "echo '$1 $2' > $(tapch)" >/dev/null; }

start_touch_forwarder() {
  local P=$1
  [ "$TOUCH_FORWARDER" = "0" ] && return 0
  # AppSpawnX can replace a crashed child without re-running this harness.  A
  # fixed PID-qualified symlink then leaves the physical digitizer apparently
  # dead even though touchfwd is still alive.  Keep the stable output pathname
  # and retarget it to the newest ready Android child.  The in-process bridge
  # continues to consume only its own PID-qualified mailbox.
  # Do not truncate the active mailbox on every router poll.  Apart from
  # racing touchfwd's append, that changes mtime once per second and makes the
  # in-process watcher wake continuously.  Parameter expansion also avoids a
  # nested sed command whose quoting was corrupted by hdc's Windows boundary.
  dev "TP=\$(pidof touchfwd 2>/dev/null); [ -n \"\$TP\" ] && kill \$TP 2>/dev/null; RP=\$(cat /data/local/tmp/touchfwd-router.pid 2>/dev/null); [ -n \"\$RP\" ] && kill \$RP 2>/dev/null; rm -f /data/local/tmp/noice_tap; : > /data/local/tmp/noice_tap.$P; chmod 0666 /data/local/tmp/noice_tap.$P; ln -s /data/local/tmp/noice_tap.$P /data/local/tmp/noice_tap; nohup sh -c 'while :; do C=\$(ls -t $ASX/adapter_child_*.stderr 2>/dev/null | head -1); P=\${C##*adapter_child_}; P=\${P%.stderr}; if [ -n \"\$P\" ] && [ -d /proc/\$P ] && grep -aq \"side-channels started\" \"\$C\" 2>/dev/null; then T=/data/local/tmp/noice_tap.\$P; if [ ! -e \"\$T\" ]; then : > \"\$T\"; chmod 0666 \"\$T\"; fi; if [ \"\$(readlink /data/local/tmp/noice_tap 2>/dev/null)\" != \"\$T\" ]; then ln -sf \"\$T\" /data/local/tmp/noice_tap; fi; echo \$P > /data/local/tmp/asx/walkpid; fi; sleep 1; done' </dev/null >/data/local/tmp/touchfwd-router.log 2>&1 & echo \$! > /data/local/tmp/touchfwd-router.pid; nohup env WL_TOUCH_STREAM=1 WL_TOUCH_STREAM_CLICK=1 WL_TOUCH_COORD_GAIN=$TOUCH_COORD_GAIN /data/local/tmp/touchfwd $TOUCH_DEVICE /data/local/tmp/noice_tap </dev/null >/data/local/tmp/touchfwd.stream.log 2>&1 & sleep 1" >/dev/null
  awake
  echo "TOUCH_STREAM child=$P device=$TOUCH_DEVICE forwarder=$(dev 'pidof touchfwd 2>/dev/null') router=$(dev 'cat /data/local/tmp/touchfwd-router.pid 2>/dev/null')"
}

source "$(dirname "${BASH_SOURCE[0]}")/ttwalk_ready.sh"

launch() {
  echo "=== launching Toutiao (new appspawn; gate: Android view + visible RS surface) ==="
  GATE_START_SECONDS=$SECONDS
  GATE_START_MONOTONIC_MS=$(ready_monotonic_ms)
  dev "kill -9 \$(pidof appspawn-x) 2>/dev/null; sleep 3; cd /data/local/tmp/asx; rm -f asx.err walkpid /data/local/tmp/asx/THROWTRACE /data/local/tmp/noice_tap.* /data/local/tmp/noice_text; : > /data/local/tmp/noice_text; chmod 0666 /data/local/tmp/noice_text; mkdir -p $ASX/prev; for f in $ASX/adapter_child_*.stderr; do [ -e \"\$f\" ] && mv \"\$f\" $ASX/prev/; done; setenforce 0 2>/dev/null; env APPSPAWNX_FORCE_JIT=$FORCE_JIT APPSPAWNX_PROFILED_JIT=$PROFILED_JIT APPSPAWNX_JIT_THRESHOLD=$JIT_THRESHOLD APPSPAWNX_JIT_WARMUP_THRESHOLD=$JIT_WARMUP_THRESHOLD APPSPAWNX_JIT_BASELINE=$JIT_BASELINE WESTLAKE_JIT_BOOT_PARSER=$JIT_BOOT_PARSER APPSPAWNX_JIT_DELAY_MS=$JIT_DELAY_MS $BOOT_IMAGE_ENV $TRUSTED_OAT_DIR_ENV $PRELOAD_CLASS_LIST_ENV WL_TOUTIAO_FORCE_JAVA_NET=$TOUTIAO_FORCE_JAVA_NET WESTLAKE_APP_CPU_RESERVE=$APP_CPU_RESERVE $TOUCH_DIRECT_ENV WL_TOUCH_FORCE_CLICK=$FORCE_CLICK $TOUCH_ENQUEUE_ENV $POST_CLICK_ENV $TOUCH_FD_MAIN_ENV $KEY_ENQUEUE_ENV WL_TLS_DUMP_HTTP_HEADS=$TLS_HTTP_HEADS WL_TLS_DUMP_SEARCH_BODY=$TLS_SEARCH_BODY WESTLAKE_TRACE_TOUTIAO_VIDEO=$TTVIDEO_CALLS WESTLAKE_TRACE_NATIVE_LOAD=$NATIVE_LOAD_TRACE WESTLAKE_TRACE_ANDROID_NATIVE_NET=$NATIVE_NET_TRACE WL_TRACE_ANDROID_NATIVE_WINDOW=$NATIVE_WINDOW_TRACE WESTLAKE_TRACE_ANDROID_NATIVE_WINDOW=$NATIVE_WINDOW_TRACE WESTLAKE_TRACE_TZ=$TZ_TRACE ASX_KEEP_THEME=1 ASX_DIRECT_LAUNCH=1 ASX_LAUNCH_PKG=com.ss.android.article.news ASX_APK_PATH=/data/local/tmp/asx/toutiao.apk ASX_LAUNCH_ACTIVITY=com.ss.android.article.news.activity.MainActivity ASX_WEBVIEW_APK=/data/local/tmp/asx/webview-t.apk ASX_WEBVIEW_LIB_DIR=/data/local/tmp/asx/webview-t-lib ASX_WEBVIEW_DATA_DIR=/data/local/tmp/asx/webview-t-data nohup sh $RUN_SCRIPT >/dev/null 2>&1 & i=0; max=\$((10 * $PARENT_READY_TIMEOUT_SECONDS)); while [ \$i -lt \$max ]; do grep -q 'Ready to accept' asx.err 2>/dev/null && break; sleep 0.1; i=\$((i+1)); done; ./spawn_client /dev/unix/socket/AppSpawnX com.ss.android.article.news >/dev/null 2>&1; echo spawned" >/dev/null
  # This timestamp separates parent/runtime creation from child UI readiness.
  # Android normally pays the former at boot, not on every application launch.
  echo "PARENT_READY_AND_SPAWN_REQUEST after $((SECONDS - GATE_START_SECONDS))s wall"
  wait_for_ready
}

respawn() {
  # Android cold-start measurements reuse the persistent zygote.  Keep
  # appspawn-x alive, terminate only its existing Toutiao child, archive that
  # child's log, and apply exactly the same structural gate as launch().
  echo "=== respawning Toutiao from existing appspawn (child-only gate) ==="
  GATE_START_SECONDS=$SECONDS
  GATE_START_MONOTONIC_MS=$(ready_monotonic_ms)
  if ! dev "pidof appspawn-x >/dev/null 2>&1 && echo yes" | grep -q yes; then
    echo "RESPAWN_FAIL: appspawn-x is not running; run launch first"
    return 1
  fi
  dev "P=\$(cat /data/local/tmp/asx/walkpid 2>/dev/null); [ -n \"\$P\" ] && kill -9 \"\$P\" 2>/dev/null; sleep 1; rm -f /data/local/tmp/asx/walkpid /data/local/tmp/noice_tap.*; mkdir -p $ASX/prev; for f in $ASX/adapter_child_*.stderr; do [ -e \"\$f\" ] && mv \"\$f\" $ASX/prev/; done; cd /data/local/tmp/asx; ./spawn_client /dev/unix/socket/AppSpawnX com.ss.android.article.news >/dev/null 2>&1" >/dev/null
  wait_for_ready
}

status() {
  local P C; P=$(walkpid); C=$(childlog)
  echo "walkpid=$P alive=$(dev "[ -d /proc/$P ] && echo YES || echo NO")  log=$(basename "$C")"
  dev "echo \"  sidechan=\$(grep -ac 'side-channels started' $C) VRI=\$(grep -ac ViewRootImpl $C) segv=\$(grep -ac CHILDSEGV $C) ifoob=\$(grep -ac WESTLAKE-IFOOB $C)\""
  echo "  RS nodes visible: $(dev "hidumper -s RenderService -a RSTree 2>/dev/null | grep -ac 'ss.android.article.news.*Visible: 1'")"
}

case "${1:-walk}" in
  launch) launch ;;
  respawn) respawn ;;
  touchfwd)
    P=$(walkpid)
    if [ -z "$P" ] || ! dev "[ -d /proc/$P ] && echo yes" | grep -q yes; then
      C=$(childlog)
      P=$(basename "$C" .stderr)
      P=${P#adapter_child_}
      dev "echo $P > /data/local/tmp/asx/walkpid" >/dev/null
    fi
    start_touch_forwarder "$P"
    ;;
  vt)     vt ;;
  tap)    tap "$2" "$3"; echo "tapped $2 $3" ;;
  shot)   awake; shot "${2:-shot}" ;;
  walk)
    launch || exit 1
    awake
    status
    echo; echo "=== PIXEL ORACLE ==="; shot "tt_00_launch"
    echo; echo "=== WIDGET ORACLE (view tree, pixel-independent) ==="
    V=$(vt); n=$(echo "$V" | grep -c 'rect=' || true)
    echo "view-tree widget lines: $n"
    echo "$V" | head -30
    ;;
esac

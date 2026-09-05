#!/bin/bash
# Sourced by ttwalk.sh; dev(), childlog(), proc_cpu_ticks() and the touch
# forwarder are supplied by the caller. No device work occurs on source.

ready_monotonic_ms() {
  awk '{printf "%.0f\n", $1 * 1000}' /proc/uptime
}

ready_child_state() {
  local pid=$1 log=$2
  dev "if [ ! -r '$log' ]; then echo ERROR; exit; fi
    grep -aEq 'FATAL EXCEPTION|WESTLAKE-CHILDSEGV.*fatal|Fatal signal' '$log'
    rc=\$?; if [ \$rc -eq 0 ]; then echo FATAL; exit; fi
    if [ \$rc -ne 1 ]; then echo ERROR; exit; fi
    if ! read -r stat < /proc/$pid/stat; then echo DEAD; exit; fi
    stat=\${stat##*) }; set -- \$stat
    case \$1 in Z|X|x) echo DEAD; exit;; esac
    shift 19; echo LIVE:\$1"
}

ready_request_view() {
  local pid=$1 log=$2
  # Keep the original byte boundary across retries so a slow asynchronous
  # response is not discarded at the next poll. Historical VT lines cannot pass.
  dev "N=\$(wc -c < '$log') || exit; echo v > /data/local/tmp/noice_tap.$pid || exit; echo \$((N + 1))"
}

ready_view_and_surface() {
  local pid=$1 log=$2 first_byte=$3 dump S
  # RSNode::GenerateId = (creator pid << 32) | sequence. Decode on the host:
  # this device's sh arithmetic is 32-bit and its default PATH has no awk.
  dump=$(dev "tail -c +$first_byte '$log' | grep -aEq 'OH_InputBridge. VT .*rect=\[[^]]* [1-9][0-9]*x[1-9][0-9]*\]' || exit
    echo VIEW
    hidumper -s RenderService -a RSTree 2>/dev/null |
      sed -n 's/.*SURFACE_NODE\[\([0-9][0-9]*\)\].*Name \[com.ss.android.article.news\/com.ss.android.article.news.activity.MainActivity\].*Visible: 1.*/\1/p'")
  [[ "${dump%%$'\n'*}" == VIEW ]] || return 1
  while IFS= read -r S; do
    [[ "$S" =~ ^[0-9]+$ ]] || continue
    [ "$((S / 4294967296))" -eq "$pid" ] || continue
    if [[ $(dev "hidumper -s RenderService -a 'surfacenode $S' 2>/dev/null | grep -aq 'totalBuffersMemSize = [1-9]' && echo READY") == READY ]]; then
      echo READY
      return 0
    fi
  done <<< "${dump#*$'\n'}"
  return 1
}

wait_for_ready() {
  local C= P= identity= state first_byte= start deadline now elapsed cpu_ticks
  start=$(ready_monotonic_ms)
  deadline=$((start + LAUNCH_TIMEOUT_SECONDS * 1000))
  while :; do
    now=$(ready_monotonic_ms)
    [ "$now" -lt "$deadline" ] || break
    if [ -z "$C" ]; then
      C=$(childlog)
      if [ -z "$C" ]; then sleep 0.5; continue; fi
      # Only accept the expected log path and a numeric PID before interpolation.
      P=${C##*/adapter_child_}; P=${P%.stderr}
      if [[ ! "$P" =~ ^[0-9]+$ || "$C" != "$ASX/adapter_child_$P.stderr" ]]; then
        echo "LAUNCH_FAIL: invalid child log identity"; return 1
      fi
    fi
    state=$(ready_child_state "$P" "$C")
    if [[ ! "$state" =~ ^LIVE:[0-9]+$ ]]; then
      echo "LAUNCH_FAIL: child=$P state=${state:-unavailable}"; return 1
    fi
    [ -n "$identity" ] || identity=$state
    if [ "$identity" != "$state" ]; then
      echo "LAUNCH_FAIL: child=$P process identity changed"; return 1
    fi
    if [ -z "$first_byte" ]; then
      if [[ $(dev "grep -aq 'side-channels started' '$C' && echo YES") == YES ]]; then
        first_byte=$(ready_request_view "$P" "$C")
        if [[ ! "$first_byte" =~ ^[1-9][0-9]*$ ]]; then
          echo "LAUNCH_FAIL: child=$P view request failed"; return 1
        fi
      fi
    elif [[ $(ready_view_and_surface "$P" "$C" "$first_byte") == READY ]]; then
      # A fatal/process exit wins even when both UI oracles just passed.
      sleep 1
      state=$(ready_child_state "$P" "$C")
      if [ "$state" != "$identity" ]; then
        echo "LAUNCH_FAIL: child=$P failed post-gate liveness ($state)"; return 1
      fi
      dev "echo $P > /data/local/tmp/asx/walkpid" >/dev/null
      now=$(ready_monotonic_ms)
      elapsed=$((now - ${GATE_START_MONOTONIC_MS:-$start}))
      cpu_ticks=$(proc_cpu_ticks "$P")
      echo "LAUNCHED child=$P identity=$identity gateMs=$elapsed cpuTicks=${cpu_ticks:-unknown} (fresh Android view + PID-owned visible buffered RS surface)"
      start_touch_forwarder "$P"
      return 0
    else
      # Reissue without moving the fresh-response boundary.
      dev "echo v > /data/local/tmp/noice_tap.$P" >/dev/null
    fi
    sleep 0.5
  done
  echo "LAUNCH_FAIL: no fresh Android view + PID-owned visible RS surface within ${LAUNCH_TIMEOUT_SECONDS}s"
  return 1
}

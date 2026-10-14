#!/usr/bin/env bash
# Boot the port for a fixed time and check it stayed alive and healthy. Run from anywhere; needs a display.
set -uo pipefail
cd "$(dirname "$0")/.."
SECONDS_TO_RUN="${1:-90}"
LOG=build/smoke.log
# The port ignores SIGTERM while shutting down threads, so force-kill after a grace period; either way it was
# still alive at the deadline (124 = stopped by TERM, 137 = needed KILL).
JFG_AUTOSTART=1 JFG_DEBUG_MODULES=1 timeout -k 10 "$SECONDS_TO_RUN" build/JFGRecompiled > "$LOG" 2>&1
status=$?
fail=0
[[ $status -eq 124 || $status -eq 137 ]] || { echo "FAIL: exited early with status $status"; fail=1; }
grep -q '\[jfg\] module .* loaded' "$LOG" || { echo "FAIL: no modules loaded"; fail=1; }
if grep -E 'Failed to find function|Unable to find a matching GBI|Cannot partially unload|Unhandled|FATAL|assert' "$LOG"; then
  echo "FAIL: fatal runtime message above"; fail=1
fi
echo "modules loaded: $(grep -c 'module .* loaded' "$LOG"), unloaded: $(grep -c 'unloaded' "$LOG"), lazy links: $(grep -c 'lazy link' "$LOG")"
exit $fail

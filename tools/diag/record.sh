#!/usr/bin/env bash
# Autostart the port (optionally driven by JFG_INPUT_SCRIPT), record its window at 60 fps from START for DURATION
# seconds, and report a flicker metric (tools/diag/flicker.py). Development aid; needs the desktop session.
# Usage: tools/diag/record.sh OUT.mkv START DURATION
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$1"; start_at="$2"; duration="$3"
total=$(( ${start_at%.*} + ${duration%.*} + 8 ))
timeout -k 3 "$total" env JFG_AUTOSTART=1 JFG_NO_VSYNC="${JFG_NO_VSYNC:-1}" build/JFGRecompiled > "${out%.*}.log" 2>&1 &
timer=$!
# The port ignores SIGTERM while shutting down, so kill it outright when done.
trap 'pkill -KILL -P "$timer" 2>/dev/null || true' EXIT
pid=""
for _ in $(seq 50); do
  sleep 0.2
  pid=$(pgrep -P "$timer" JFGRecompiled || true) && [ -n "$pid" ] && break
done
# Pick this process's window (another instance may still be closing).
wid=""
for _ in $(seq 50); do
  sleep 0.2
  for w in $(xwininfo -root -tree 2>/dev/null | grep -i '"Jet Force Gemini' | awk '{print $1}'); do
    if xprop -id "$w" _NET_WM_PID 2>/dev/null | grep -q "= $pid\$"; then wid=$w; break; fi
  done
  [ -n "$wid" ] && break
done
[ -n "$wid" ] || { echo "record: game window never appeared" >&2; exit 1; }
sleep "$start_at"
xwininfo -id "$wid" > /dev/null 2>&1 || { echo "record: game window closed" >&2; exit 1; }
ffmpeg -hide_banner -loglevel error -y -f x11grab -window_id "$wid" -framerate 60 -i "$DISPLAY" -t "$duration" -c:v ffv1 "$out"
python3 tools/diag/flicker.py "$out"

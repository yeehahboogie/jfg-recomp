#!/usr/bin/env bash
# Autostart the port and screenshot its window at the given seconds (default 8 14 20 26 32 40), plus a contact
# sheet of them all. Development aid for visual checks. Runs on a private virtual display unless JFG_ON_DESKTOP=1.
# Usage: scripts/screenshots.sh OUT_DIR [SECONDS...]
set -euo pipefail
cd "$(dirname "$0")/.."
out="$1"; shift
# Run off the desktop on a private virtual X display (Xvfb; the NVIDIA driver still renders on the GPU), so test runs
# never open windows on the user's screen or take keyboard focus. JFG_ON_DESKTOP=1 runs on the real display instead.
# JFG_XDISPLAY picks the virtual display number (default 99), so several runs can go in parallel.
if [ "${JFG_ON_DESKTOP:-0}" != 1 ]; then
  xd=":${JFG_XDISPLAY:-99}"
  export DISPLAY="$xd"
  unset WAYLAND_DISPLAY
  if ! xwininfo -root -display "$xd" > /dev/null 2>&1; then
    Xvfb "$xd" -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
    for _ in $(seq 20); do sleep 0.2; xwininfo -root -display "$xd" > /dev/null 2>&1 && break; done
  fi
fi
times=("${@:-8 14 20 26 32 40}")
read -r -a times <<< "${times[*]}"
mkdir -p "$out"; rm -f "$out"/t*.png "$out"/sheet.png
last=${times[-1]%.*}
timeout -k 3 $((last + 6)) env JFG_AUTOSTART=1 JFG_NO_VSYNC="${JFG_NO_VSYNC:-1}" build/JFGRecompiled > "$out/run.log" 2>&1 &
pid=$!
wid=""
for _ in $(seq 50); do
  sleep 0.2
  wid=$(xwininfo -root -tree 2>/dev/null | grep -i '"Jet Force Gemini' | head -1 | awk '{print $1}') && [ -n "$wid" ] && break
done
[ -n "$wid" ] || { echo "screenshots: game window never appeared" >&2; exit 1; }
start=$(date +%s.%N)
for t in "${times[@]}"; do   # seconds after the window appears; fractions allowed
  sleep "$(awk -v s="$start" -v t="$t" -v now="$(date +%s.%N)" 'BEGIN { d = s + t - now; print (d > 0 ? d : 0) }')"
  # Never call import on a window that no longer exists: it would fall back to grabbing the mouse for a selection.
  xwininfo -id "$wid" > /dev/null 2>&1 || { echo "screenshots: game window closed at ${t}s" >&2; break; }
  import -window "$wid" "$out/t$t.png"
done
wait "$pid" || true
python3 - "$out" "${times[@]}" <<'PY'
import sys
from PIL import Image
out, times = sys.argv[1], sys.argv[2:]
import os
ims = [Image.open(f"{out}/t{t}.png").convert("RGB") for t in times if os.path.exists(f"{out}/t{t}.png")]
w, h = ims[0].size[0] // 2, ims[0].size[1] // 2
cols = 3
sheet = Image.new("RGB", (w * cols, h * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(f"{out}/sheet.png")
PY
echo "$out/sheet.png"

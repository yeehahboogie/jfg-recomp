#!/usr/bin/env bash
# Autostart the port and screenshot its window at the given seconds (default 8 14 20 26 32 40), plus a contact
# sheet of them all. Development aid for visual checks; needs an X11-capable desktop session.
# Usage: scripts/screenshots.sh OUT_DIR [SECONDS...]
set -euo pipefail
cd "$(dirname "$0")/.."
out="$1"; shift
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
  import -window "$wid" "$out/t$t.png"
done
wait "$pid" || true
python3 - "$out" "${times[@]}" <<'PY'
import sys
from PIL import Image
out, times = sys.argv[1], sys.argv[2:]
ims = [Image.open(f"{out}/t{t}.png").convert("RGB") for t in times]
w, h = ims[0].size[0] // 2, ims[0].size[1] // 2
cols = 3
sheet = Image.new("RGB", (w * cols, h * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(f"{out}/sheet.png")
PY
echo "$out/sheet.png"

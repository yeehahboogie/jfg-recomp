#!/usr/bin/env bash
# Run ares on jfg.z64 driven by a virtual gamepad script (tools/diag/vpad.py) and screenshot its window at the given
# seconds, plus a contact sheet. ares's own save (jfg.flash next to the ROM) is moved to jfg.flash.bak first so each
# run starts from a blank save. Needs ares (flatpak dev.ares.ares) with VirtualPad1 bound to the virtual pad.
# Usage: tools/diag/ares-shots.sh OUT_DIR "SCRIPT" SECONDS...
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$1"; script="$2"; shift 2
times=("$@")
mkdir -p "$out"; rm -f "${out:?}"/t*.png "${out:?}"/sheet.png
[ -f jfg.flash ] && mv -f jfg.flash jfg.flash.bak
last=${times[-1]%.*}
python3 tools/diag/vpad.py run "$script" > "$out/vpad.log" 2>&1 &
pad=$!
sleep 0.5
timeout -k 3 $((last + 10)) flatpak run --user dev.ares.ares "$PWD/jfg.z64" > "$out/run.log" 2>&1 &
emu=$!
cleanup() { kill "$pad" 2>/dev/null || true; kill "$emu" 2>/dev/null || true; pkill -f "ares $PWD/jfg.z64" 2>/dev/null || true; }
trap cleanup EXIT
wid=""
for _ in $(seq 100); do
  sleep 0.2
  wid=$(xwininfo -root -tree 2>/dev/null | grep '"jfg"' | head -1 | awk '{print $1}') && [ -n "$wid" ] && break
done
[ -n "$wid" ] || { echo "ares-shots: ares window never appeared" >&2; exit 1; }
start=$(date +%s.%N)
for t in "${times[@]}"; do   # seconds after the pad started (~ when ares launched)
  sleep "$(awk -v s="$start" -v t="$t" -v now="$(date +%s.%N)" 'BEGIN { d = s + t - 1 - now; print (d > 0 ? d : 0) }')"
  xwininfo -id "$wid" > /dev/null 2>&1 || { echo "ares-shots: ares window closed at ${t}s" >&2; break; }
  import -window "$wid" "$out/t$t.png"
done
python3 - "$out" "${times[@]}" <<'PY'
import sys
from PIL import Image
out, times = sys.argv[1], sys.argv[2:]
import os
ims = [Image.open(f"{out}/t{t}.png").convert("RGB") for t in times if os.path.exists(f"{out}/t{t}.png")]
w, h = ims[0].size[0] // 2, ims[0].size[1] // 2
cols = 4
sheet = Image.new("RGB", (w * cols, h * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(f"{out}/sheet.png")
PY
echo "$out/sheet.png"

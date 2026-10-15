#!/usr/bin/env bash
# Run ares on jfg.z64 driven by a virtual gamepad script (tools/diag/vpad.py) and screenshot its window at the given
# seconds, plus a contact sheet. ares's own save (jfg.flash next to the ROM) is moved to jfg.flash.bak first so each
# run starts from a blank save. Needs ares (flatpak dev.ares.ares) with VirtualPad1 bound to the virtual pad.
# Usage: tools/diag/ares-shots.sh OUT_DIR "SCRIPT" SECONDS...
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$1"; script="$2"; shift 2
# Run on a private virtual X display (Xvfb) so ares never opens on the desktop (and because ares only loads the ROM
# there on this machine). ARES_ON_DESKTOP=1 uses the real display.
if [ "${ARES_ON_DESKTOP:-0}" != 1 ]; then
  export DISPLAY=:99
  unset WAYLAND_DISPLAY
  if ! xwininfo -root -display :99 > /dev/null 2>&1; then
    Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
    for _ in $(seq 20); do sleep 0.2; xwininfo -root -display :99 > /dev/null 2>&1 && break; done
  fi
fi
# ARES_WARP=<level>[:<entry>]: send the new game straight to that level (tools/diag/ares-warp.py, via ares's GDB
# server, which is enabled only for this run: it listens on all interfaces).
settings="$HOME/.var/app/dev.ares.ares/data/ares/settings.bml"
times=("$@")
mkdir -p "$out"; rm -f "${out:?}"/t*.png "${out:?}"/sheet.png
[ -f jfg.flash ] && mv -f jfg.flash jfg.flash.bak
last=${times[-1]%.*}
if [ -n "${ARES_WARP:-}" ]; then
  cp "$settings" "$out/settings.bml.orig"
  sed -i '/^DebugServer/,/^[^ ]/ s/^  Enabled: false/  Enabled: true/; /^DebugServer/,/^[^ ]/ s/^  UseIPv4: false/  UseIPv4: true/' "$settings"
fi
python3 tools/diag/vpad.py run "$script" > "$out/vpad.log" 2>&1 &
pad=$!
sleep 0.5
# No access to the desktop's sound server unless on the desktop: test runs must stay silent.
mute=(--nosocket=pulseaudio --env=SDL_AUDIODRIVER=dummy --env=PULSE_SERVER=unix:/nonexistent)
[ "${ARES_ON_DESKTOP:-0}" = 1 ] && mute=()
# ARES_AUDIO_SINK=<sink>: let ares reach the sound server, aimed at that sink (used by tools/diag/ares-audio.sh, which
# also moves the stream there, as stream-restore can override the request).
[ -n "${ARES_AUDIO_SINK:-}" ] && mute=(--env=PULSE_SINK="$ARES_AUDIO_SINK")
timeout -k 3 $((last + 10)) flatpak run --user "${mute[@]}" dev.ares.ares "$PWD/jfg.z64" > "$out/run.log" 2>&1 &
emu=$!
warper=""
if [ -n "${ARES_WARP:-}" ]; then
  python3 tools/diag/ares-warp.py 127.0.0.1:9123 "${ARES_WARP%%:*}" "$( [ "${ARES_WARP#*:}" != "$ARES_WARP" ] && echo "${ARES_WARP#*:}" || echo 0)" > "$out/warp.log" 2>&1 &
  warper=$!
fi
cleanup() {
  kill "$pad" 2>/dev/null || true; kill "$emu" 2>/dev/null || true; [ -z "$warper" ] || kill "$warper" 2>/dev/null || true
  pkill -f "ares $PWD/jfg.z64" 2>/dev/null || true
  # ares writes its settings when it exits: wait for that, then put them back (debug server off again).
  for _ in $(seq 50); do pgrep -f "ares $PWD/jfg.z64" > /dev/null || break; sleep 0.2; done
  if [ -f "$out/settings.bml.orig" ]; then
    cp "$out/settings.bml.orig" "$settings" && rm -f "$out/settings.bml.orig"
    grep -A2 "^DebugServer" "$settings" | grep -q "Enabled: false" || echo "ares-shots: WARNING ares debug server still enabled in $settings" >&2
  fi
  true
}
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

#!/bin/bash
# Record ares's sound for one tools/diag/ares-shots.sh run into a WAV without it reaching the speakers.
# Usage: tools/diag/ares-audio.sh OUT.wav OUT_DIR "SCRIPT" SECONDS...   (the arguments after OUT.wav go to ares-shots.sh)
# A temporary null sink takes ares's stream: PipeWire's stream-restore ignores PULSE_SINK and would put ares on the
# default output, so the stream is moved to the null sink the moment it appears (ares is silent for its first seconds).
# The sink is removed afterwards.
set -u
wav=$1; shift
cd "$(dirname "$0")/../.."
mod=$(pactl load-module module-null-sink sink_name=jfg_ares_capture sink_properties=device.description=jfg-ares-capture)
sink=$(pactl list short sinks | awk '$2=="jfg_ares_capture"{print $1}')
parecord --device=jfg_ares_capture.monitor --format=s16le --rate=22050 --channels=2 --file-format=wav "$wav" &
rec=$!
ARES_AUDIO_SINK=jfg_ares_capture tools/diag/ares-shots.sh "$@" &
run=$!
while kill -0 $run 2>/dev/null; do
  for i in $(pactl -f json list sink-inputs 2>/dev/null | python3 -c "
import json, sys
print(' '.join(str(s['index']) for s in json.load(sys.stdin)
               if s.get('properties', {}).get('application.process.binary') == 'ares' and s['sink'] != $sink))"); do
    pactl move-sink-input "$i" jfg_ares_capture && echo "ares-audio: moved ares stream $i to the null sink"
  done
  sleep 0.1
done
kill $rec; wait $rec 2>/dev/null
pactl unload-module "$mod" && echo "ares-audio: null sink removed"

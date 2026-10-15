#!/bin/bash
# Record a test run's sound to a WAV without it reaching the speakers.
# Usage: tools/diag/capture-audio.sh PROCESS OUT.wav COMMAND...
#   PROCESS: the binary whose sound streams to capture (JFGRecompiled, ares)
# Creates a temporary null sink, exports JFG_AUDIO_SINK and ARES_AUDIO_SINK naming it (scripts/screenshots.sh and
# tools/diag/ares-shots.sh then aim their sound there), records its monitor while COMMAND runs, and removes it after.
# PipeWire's stream-restore can override the requested sink and put a stream on the default output, so PROCESS's
# streams are also moved to the null sink the moment they appear (both programs are silent for their first seconds).
set -u
proc=$1; wav=$2; shift 2
mod=$(pactl load-module module-null-sink sink_name=jfg_capture_$$ sink_properties=device.description=jfg-capture-$$)
sink=$(pactl list short sinks | awk '$2=="jfg_capture_'$$'"{print $1}')
parecord --device=jfg_capture_$$.monitor --format=s16le --rate=48000 --channels=2 --file-format=wav "$wav" &
rec=$!
JFG_AUDIO_SINK=jfg_capture_$$ ARES_AUDIO_SINK=jfg_capture_$$ "$@" &
run=$!
while kill -0 $run 2>/dev/null; do
  for i in $(pactl -f json list sink-inputs 2>/dev/null | python3 -c "
import json, sys
print(' '.join(str(s['index']) for s in json.load(sys.stdin)
               if s.get('properties', {}).get('application.process.binary') == '$proc' and s['sink'] != $sink))"); do
    pactl move-sink-input "$i" jfg_capture_$$ && echo "capture-audio: moved $proc stream $i to the null sink"
  done
  sleep 0.1
done
wait $run; status=$?
kill $rec; wait $rec 2>/dev/null
pactl unload-module "$mod" && echo "capture-audio: null sink removed"
exit $status

#!/bin/bash
# Record ares's sound for one tools/diag/ares-shots.sh run into a WAV without it reaching the speakers.
# Usage: tools/diag/ares-audio.sh OUT.wav OUT_DIR "SCRIPT" SECONDS...   (the arguments after OUT.wav go to ares-shots.sh)
cd "$(dirname "$0")/../.."
wav=$1; shift
exec tools/diag/capture-audio.sh ares "$wav" tools/diag/ares-shots.sh "$@"

#!/usr/bin/env bash
# Survey many levels (see scripts/survey-level.sh). Usage: scripts/survey-batch.sh OUT_ROOT LEVEL...
# JFG_SURVEY_WORKERS (default 1) runs that many games in parallel, each on its own virtual display (:99, :98, ...)
# and with its own copy of build/appdata (saves are wiped per run). One result line per level in OUT_ROOT/results.txt.
set -uo pipefail
cd "$(dirname "$0")/.."
root="$1"; shift
mkdir -p "$root"
workers="${JFG_SURVEY_WORKERS:-1}"
levels=("$@")
worker() {
  local k="$1"; local app="$PWD/build/appdata-survey$k"
  rm -rf -- "${app:?}"; cp -r build/appdata "$app"
  local i
  for ((i = k; i < ${#levels[@]}; i += workers)); do
    APP_FOLDER_PATH="$app" JFG_XDISPLAY=$((99 - k)) scripts/survey-level.sh "${levels[i]}" "$root/${levels[i]}" >> "$root/results.txt" 2>&1
  done
}
for ((k = 0; k < workers; k++)); do worker "$k" & done
wait
echo "batch done: ${#levels[@]}" >> "$root/results.txt"

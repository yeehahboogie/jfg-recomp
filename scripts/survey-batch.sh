#!/usr/bin/env bash
# Survey many levels in a row (see scripts/survey-level.sh). Usage: scripts/survey-batch.sh OUT_ROOT LEVEL...
# Appends one result line per level to OUT_ROOT/results.txt.
set -uo pipefail
cd "$(dirname "$0")/.."
root="$1"; shift
mkdir -p "$root"
for level in "$@"; do
  scripts/survey-level.sh "$level" "$root/$level" >> "$root/results.txt" 2>&1
done
echo "batch done: $#" >> "$root/results.txt"

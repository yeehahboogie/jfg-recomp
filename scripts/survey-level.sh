#!/usr/bin/env bash
# Whole-game survey: start a new game straight into LEVEL (JFG_WARP), screenshot it, and report its health.
# Usage: scripts/survey-level.sh LEVEL[:ENTRY] OUT_DIR [extra JFG_INPUT_SCRIPT entries]
# Prints one line: LEVEL status (ok | crashed | errors) and the number of error lines; screenshots in OUT_DIR.
set -euo pipefail
cd "$(dirname "$0")/.."
target="$1"; out="$2"; extra="${3:-}"
mkdir -p "$out"
rm -f "${APP_FOLDER_PATH:?set APP_FOLDER_PATH}"/saves/jfg.n64.us.bin*
# Title -> New Game -> name entry -> start; the warp replaces the story intro (requested about 54 s in).
menus="30+0.3 START; 36+0.3 START; 42+0.3 A; 49+0.2 A; 50.5+0.15 - 0 -1; 51.3+0.15 - 0 -1; 52.1+0.15 - 0 -1; 52.9+0.15 - 0 -1; 54+0.2 A"
JFG_WARP="$target" JFG_DEBUG_LEVELS=1 JFG_INPUT_SCRIPT="$menus${extra:+; $extra}" \
  scripts/screenshots.sh "$out" 62 70 78 86 > /dev/null 2>&1 || true
log="$out/run.log"
errors=$(grep -cE "Failed to find function|unregistered|SP CRASHED|DP CRASHED|Assertion|Segmentation|Unhandled|fatal" "$log" || true)
status=ok
grep -q "warp: new game goes to level" "$log" || status=no-warp
[ -f "$out/t86.png" ] || status=crashed
[ "$errors" -gt 0 ] && status="errors"
playing=$(grep -o "playing level [0-9]*" "$log" | tail -1 || true)
echo "$target $status errors=$errors last='$playing'"

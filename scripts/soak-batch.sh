#!/usr/bin/env bash
# Soak test: start a new game in each LEVEL (as scripts/survey-batch.sh) and play it for about two minutes with
# invulnerability and every weapon: run in all directions while firing, jump, roll, and cycle weapons, so combat,
# weapon, effect and enemy code runs. Results in OUT_ROOT/results.txt (crashed = the game died before the last shot).
# Usage: JFG_SURVEY_WORKERS=N scripts/soak-batch.sh OUT_ROOT LEVEL...
set -euo pipefail
cd "$(dirname "$0")/.."
script=""
for t in $(seq 62 12 170); do
  script+="${script:+; }$t+3 Z 0 1; $((t + 3))+2 Z 1 0.3; $((t + 5))+0.3 A 0 1; $((t + 6))+3 Z -1 -1"
  script+="; $((t + 9))+0.3 B 0.8 0; $((t + 10))+0.2 CU; $((t + 11))+0.8 Z 0.3 -1"
done
JFG_CHEATS=invuln,weapons JFG_SURVEY_EXTRA="$script" JFG_SURVEY_SHOTS="70 110 150 185" exec scripts/survey-batch.sh "$@"

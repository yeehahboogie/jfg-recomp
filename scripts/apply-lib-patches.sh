#!/usr/bin/env bash
# Apply our patches to submodules after `git submodule update`. Safe to re-run.
set -euo pipefail
cd "$(dirname "$0")/.."
for p in lib-patches/rt64/*.patch; do
  if git -C lib/rt64 apply --reverse --check "../../$p" 2>/dev/null; then
    echo "already applied: $p"
  else
    git -C lib/rt64 apply "../../$p" && echo "applied: $p"
  fi
done

#!/usr/bin/env bash
# Apply our patches to submodules after `git submodule update`. Safe to re-run.
# lib-patches/<name>/*.patch applies to lib/<name>.
set -euo pipefail
cd "$(dirname "$0")/.."
for dir in lib-patches/*/; do
  lib="lib/$(basename "$dir")"
  for p in "$dir"*.patch; do
    if git -C "$lib" apply --reverse --check "$PWD/$p" 2>/dev/null; then
      echo "already applied: $p"
    else
      git -C "$lib" apply "$PWD/$p" && echo "applied: $p"
    fi
  done
done

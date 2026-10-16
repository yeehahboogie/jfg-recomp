#!/usr/bin/env bash
# Regenerate everything from the ROM and recompile. Any N64Recomp warning or error that isn't
# allow-listed fails the run.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/paths.env
ROM="${JFG_ROM:-jfg.z64}"
# A checked copy in big-endian byte order, whatever order the dump is in; RSPRecomp reads raw bytes from it.
mkdir -p generated
tools/romtool/.venv/bin/romtool export "$ROM" generated/jfg.z64
tools/romtool/.venv/bin/romtool build-elf generated/jfg.z64 --out generated --n64sym "$N64SYM"
rm -rf generated/funcs && mkdir -p generated/funcs
"$N64RECOMP" generated/jfg.toml > generated/recomp.log 2>&1 || { tail -50 generated/recomp.log; exit 1; }
problems=$(grep -E '\[WARN\]|[Ee]rror|Unhandled|Could not|No function found' generated/recomp.log \
           | grep -vxF -f <(grep -v '^#' recomp/recompile_allowlist.txt | sed '/^$/d'; echo '__never_matches__') || true)
if [[ -n "$problems" ]]; then
  echo "N64Recomp reported problems:"; echo "$problems"; exit 1
fi
mkdir -p generated/rsp
"$RSPRECOMP" recomp/aspMain.jfg.toml > generated/rsp.log 2>&1 || { cat generated/rsp.log; exit 1; }
echo "recompiled: $(ls generated/funcs/*.c | wc -l) C files, $(ls generated/rsp/*.cpp | wc -l) RSP microcode"

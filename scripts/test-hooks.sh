#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CXX="${CXX:-$(command -v clang++ || command -v g++)}"
"$CXX" -std=c++20 -Wall -Iinclude -Itests/fake src/jfg/jfg_hooks.cpp tests/jfg_hooks_test.cpp -o build/jfg_hooks_test
build/jfg_hooks_test
"$CXX" -std=c++20 -Wall -Iinclude -Ilib/N64ModernRuntime/N64Recomp/include src/jfg/libultra_stubs.cpp tests/libultra_stubs_test.cpp -o build/libultra_stubs_test
build/libultra_stubs_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/cic6105.cpp tests/cic6105_test.cpp -o build/cic6105_test
build/cic6105_test
"$CXX" -std=c++20 -Wall -Iinclude -Ilib/N64ModernRuntime/N64Recomp/include src/jfg/fpu_mode.cpp tests/fpu_mode_test.cpp -o build/fpu_mode_test
build/fpu_mode_test

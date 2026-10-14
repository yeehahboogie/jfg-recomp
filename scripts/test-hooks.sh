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
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/input_script.cpp tests/input_script_test.cpp -o build/input_script_test
build/input_script_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/gate.cpp tests/controls/gate_test.cpp -o build/gate_test
build/gate_test
"$CXX" -std=c++20 -Wall -Iinclude -Ilib/rt64/src/contrib src/jfg/controls/settings.cpp tests/controls/settings_test.cpp -o build/settings_test
build/settings_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/translate.cpp tests/controls/translate_test.cpp -o build/translate_test
build/translate_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/input_state.cpp tests/controls/input_state_test.cpp -o build/input_state_test
build/input_state_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/free_camera.cpp tests/controls/free_camera_test.cpp -o build/free_camera_test
build/free_camera_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/aim.cpp tests/controls/aim_test.cpp -o build/aim_test
build/aim_test
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/aim_move.cpp src/jfg/controls/translate.cpp tests/controls/aim_move_test.cpp -o build/aim_move_test
build/aim_move_test

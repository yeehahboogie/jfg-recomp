# JFG Modern Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Modern" control scheme to the Jet Force Gemini port:
- a free camera on the right stick or mouse;
- JFG's own hip-fire auto-aim;
- JFG's R aim mode driven by the mouse or right stick, with analog walking while aiming;
- the original controls kept unchanged as "Original".

**Architecture:**
- Native C++ units in `src/jfg/controls/`. The pure logic is unit-tested.
- They're wired into the recompiled game with N64Recomp `[[patches.hook]]` entries in `recomp/jfg.toml.in`.
- The frontend turns modern actions into the N64 buttons the game already understands.
- The free camera steers JFG's own follow camera: it overrides the orbit heading at `0x8002DFE8`, and applies pitch as a vertical orbit after the follow camera returns.

**Tech Stack:** C++20, N64Recomp hooks, the Zelda64Recomp-derived frontend (SDL2, RmlUi, nlohmann::json), clang 18 from `tools/toolchain.env`.

**Spec:** `docs/superpowers/specs/2026-10-14-jfg-modern-controls-design.md`. Research with addresses: `docs/superpowers/specs/2026-10-14-jfg-controls-research.md`.

## Global Constraints

- Branch: `modern-controls`. Push to `origin` after every commit.
- Commits end with: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
- Never commit the ROM or anything derived from it. `generated/`, `build/` and `*.z64` stay ignored.
- With the scheme set to Original, every hook returns immediately and the N64 pad is built exactly as before.
- Modern applies to player index 0, on foot (Juno, Vela, Lupus), input not disabled by the game, scheme = Modern. The camera hooks also require that the follow camera ran.
- Fail loudly. Hooks are named N64Recomp entries, so a missing function fails the build. An unexpected state or character value is logged once to stderr and treated as Original.
- Build: `source tools/toolchain.env && scripts/recompile.sh && cmake --build build --target JFGRecompiled -j 20`. Run `scripts/recompile.sh` after every `recomp/jfg.toml.in` change.
- Unit tests: `source tools/toolchain.env && scripts/test-hooks.sh` (every test binary prints `<name>: OK`).
- Visual checks: `scripts/screenshots.sh OUT SECONDS…` with `JFG_INPUT_SCRIPT` and `APP_FOLDER_PATH=$PWD/build/appdata`. The display is the user's (permission given). Use `JFG_NO_VSYNC=1` (the default in the scripts).
- Reaching gameplay: `rm -f build/appdata/saves/jfg.n64.us.bin*`, then use the Goldwood script from `docs/superpowers/handoff-2026-10-14.md` (Juno controllable from about 300 s).

## Review Focus

1. **Mouse and stick at the same time** (a pad user nudging the mouse). Expected: the mouse wins for that frame and nothing doubles. Pinned in Task 5 (`look_mixed_input_mouse_wins`) and Task 7 (`aim_mouse_wins`).
2. **Switching the scheme to Original mid-game.** Expected: the camera, aim and buttons return to original behaviour on the next frame, the right stick drives the C-buttons again, and the mouse is released. Pinned in Task 3 (`translate_original_passthrough`) and checked in Task 10 Step 3.
3. **Leaving a cutscene, static camera or aim mode.** Expected: no camera snap, and the free camera continues from the view on screen. Pinned in Task 5 (`resync_after_other_camera`), checked in Task 6 Step 6.
4. **Large mouse flicks** (hundreds of counts in one frame). Expected: pitch stays clamped, the crosshair stays in its box, and the overflow turns the body without wrapping the angle wrongly. Pinned in Task 5 (`pitch_clamped_on_big_flick`) and Task 7 (`aim_overflow_turns_body_big_flick`).
5. **Holding Aim while airborne or crouched, and pressing Jump while aiming.** Expected: the game's own rules apply (aim needs the ground or crouch states; jump is ignored while aiming, as in the original), with no stuck states. Checked in Task 9 Step 7 with a scripted sequence.

---

### Task 1: Controls scaffolding, gate and debug log (confirms the inferred fields)

**Files:**
- Create: `src/jfg/controls/gate.h`, `src/jfg/controls/gate.cpp`, `src/jfg/controls/hooks.cpp`, `include/jfg_controls_hooks.h`, `tests/controls/gate_test.cpp`
- Modify: `recomp/jfg.toml.in`, `CMakeLists.txt` (add sources), `scripts/test-hooks.sh`

**Interfaces:**
- Produces:
  - `namespace jfg::controls { struct PlayerView { int index; int character; int state; bool input_disabled; }; bool modern_applies(const PlayerView&, bool scheme_modern); }`
  - C hook entry points `void jfg_controls_latch(uint8_t*, recomp_context*)`, `void jfg_controls_begin_player(uint8_t*, recomp_context*)`, `void jfg_controls_after_camera(uint8_t*, recomp_context*)`
  - Global `jfg::controls::g_frame` (`struct FrameState { bool active; uint32_t pd; uint32_t obj; int camera_kind; }`): `camera_kind` 0 = unknown, 1 = follow, 2 = aim, 3 = other.

- [x] **Step 1: Write the failing gate test**

`tests/controls/gate_test.cpp`:
```cpp
#include <cassert>
#include <cstdio>
#include "../../src/jfg/controls/gate.h"
using jfg::controls::PlayerView;
using jfg::controls::modern_applies;
int main() {
    PlayerView juno{0, 1, 0, false};
    assert(modern_applies(juno, true));
    assert(!modern_applies(juno, false));                      // scheme Original
    assert(!modern_applies(PlayerView{1, 1, 0, false}, true)); // Floyd / player 2
    assert(!modern_applies(PlayerView{0, 1, 0, true}, true));  // game disabled input
    assert(!modern_applies(PlayerView{0, 3, 0, false}, true)); // hovership (character bits 3)
    std::puts("gate_test: OK");
}
```
Append to `scripts/test-hooks.sh`:
```bash
"$CXX" -std=c++20 -Wall -Iinclude src/jfg/controls/gate.cpp tests/controls/gate_test.cpp -o build/gate_test
build/gate_test
```

- [x] **Step 2: Run it and see it fail** (`gate.cpp` does not exist).

Run: `source tools/toolchain.env && scripts/test-hooks.sh 2>&1 | tail -2`. Expected: a compile error naming `src/jfg/controls/gate.cpp`.

- [x] **Step 3: Implement the gate**

`src/jfg/controls/gate.h`:
```cpp
#pragma once
#include <cstdint>
namespace jfg::controls {
// What the gate needs to know about the player being updated.
struct PlayerView {
    int index;          // pd+0x000
    int character;      // pd+0x001 & 3 (3 = hovership camera case)
    int state;          // pd+0x568
    bool input_disabled;
};
// Modern controls apply only to player 1 on foot, while the game accepts input.
bool modern_applies(const PlayerView& p, bool scheme_modern);

// Per-frame state shared by the hooks (player 0 only).
struct FrameState {
    bool active = false;   // modern_applies() for player 0 this frame
    uint32_t pd = 0;       // player data address (KSEG0)
    uint32_t obj = 0;      // object address
    int camera_kind = 0;   // 0 unknown, 1 follow, 2 aim, 3 other
};
extern FrameState g_frame;
}
```
`src/jfg/controls/gate.cpp`:
```cpp
#include "gate.h"
namespace jfg::controls {
FrameState g_frame;
bool modern_applies(const PlayerView& p, bool scheme_modern) {
    return scheme_modern && p.index == 0 && (p.character & 3) != 3 && !p.input_disabled;
}
}
```

- [x] **Step 4: Run the test.** Run `scripts/test-hooks.sh`. Expected: `gate_test: OK`.

- [x] **Step 5: Hooks and debug log.**

`include/jfg_controls_hooks.h` (declares the C entry points, included by the generated code through `recomp_include`):
```c
#ifndef JFG_CONTROLS_HOOKS_H
#define JFG_CONTROLS_HOOKS_H
#ifdef __cplusplus
extern "C" {
#endif
void jfg_controls_latch(uint8_t* rdram, recomp_context* ctx);
void jfg_controls_begin_player(uint8_t* rdram, recomp_context* ctx);
void jfg_controls_after_camera(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif
```
`src/jfg/controls/hooks.cpp` (this task: latch is a no-op; `begin_player` fills `g_frame`; `after_camera` logs when `JFG_DEBUG_CONTROLS` is set):
```cpp
#include <cstdio>
#include <cstdlib>
#include "recomp.h"
#include "jfg_controls_hooks.h"
#include "gate.h"

namespace {
const bool debug = std::getenv("JFG_DEBUG_CONTROLS") != nullptr;
constexpr uint32_t disablejoy_addr = 0x800F6DBC; // research appendix: input globals 0x800F6DA0–0x800F6DC0 (confirm Step 7)
bool scheme_modern() { return false; }            // replaced by settings in Task 2
uint32_t cam0(uint8_t* rdram) {                   // camGetListPtr() result is a static list; entry 0 is player 1's camera
    return 0;                                     // address filled in Step 7 from func_80041E0C's generated code
}
}

extern "C" void jfg_controls_latch(uint8_t*, recomp_context*) {}

extern "C" void jfg_controls_begin_player(uint8_t* rdram, recomp_context* ctx) {
    // controlPlayer(obj) at entry: a0 = obj; pd = *(obj + 0x68)
    const gpr obj = ctx->r4;
    const uint32_t pd = (uint32_t)MEM_W(0x68, obj);
    jfg::controls::PlayerView v{
        MEM_B(0x000, (gpr)(int32_t)pd), MEM_B(0x001, (gpr)(int32_t)pd) & 3,
        MEM_BU(0x568, (gpr)(int32_t)pd), MEM_W(0, (gpr)(int32_t)disablejoy_addr) != 0};
    if (v.index == 0) {
        jfg::controls::g_frame = {jfg::controls::modern_applies(v, scheme_modern()), pd, (uint32_t)obj, 0};
    }
}

extern "C" void jfg_controls_after_camera(uint8_t* rdram, recomp_context*) {
    auto& f = jfg::controls::g_frame;
    if (!debug || f.pd == 0) return;
    const gpr pd = (gpr)(int32_t)f.pd;
    std::fprintf(stderr, "[controls] active=%d cam=%d state=%u facing=%04X orbit=%04X pan=%04X follow=%u\n",
                 f.active, f.camera_kind, MEM_BU(0x568, pd), (uint16_t)MEM_H(0x11C, pd),
                 (uint16_t)MEM_H(0x104, pd), (uint16_t)MEM_H(0x10A, pd), MEM_BU(0x10C, pd));
}
```
Note that `controlPlayer`'s exact argument (obj or pd) is confirmed in Step 7. If it's `pd`, adjust the two reads and the `PlayerView` construction.

Add the header to `recomp_include` in `recomp/jfg.toml.in`, then the hooks:
```toml
# Modern controls (docs/superpowers/specs/2026-10-14-jfg-modern-controls-design.md)
[[patches.hook]]
func = "func_80043130"          # joyRead: latch this frame's modern input
text = "jfg_controls_latch(rdram, ctx);"

[[patches.hook]]
func = "func_80032A48"          # controlPlayer: per-player gate
text = "jfg_controls_begin_player(rdram, ctx);"
```
Find the dispatcher's return. Run `tools/romtool/.venv/bin/python -m romtool.cli disasm jfg.z64 --address 0x8002B418 --count 400 | grep -n "jr  *\$ra"`, take the first `jr $ra` that ends `0x8002B418`'s function body (the next function starts at the following `addiu $sp`), and add:
```toml
[[patches.hook]]
func = "func_8002B418"          # camera dispatcher, before its return
before_vram = 0x<address of that jr $ra>
text = "jfg_controls_after_camera(rdram, ctx);"
```
Add `src/jfg/controls/gate.cpp` and `src/jfg/controls/hooks.cpp` to the JFG sources list in `CMakeLists.txt`, next to `src/jfg/fpu_mode.cpp`.

- [x] **Step 6: Build.** Run `source tools/toolchain.env && scripts/recompile.sh && cmake --build build --target JFGRecompiled -j 20 > build/diag/build.log 2>&1; echo $?`. Expected: `0`.

Check `grep -n "jfg_controls_after_camera" generated/funcs/*.c`: one hit, inside `func_8002B418`.

- [x] **Step 7: Confirm the inferred fields at run time.**

1. Read `func_80041E0C` (`camGetListPtr`) in the generated code to get the camera list address, and fill `cam0()`.
2. Read the first lines of `func_80032A48` to see whether `a0` is `obj` or `pd`, and fix Step 5 if needed.
3. Read `func_80037250` (`controlReadJoypad`) to confirm the `disablejoy` address it tests.
4. Extend the debug line with `cam yaw=%04X` (from `cam0()`, s16 at +0x00).
5. Run, with the debug switch on, to Goldwood, plus an R hold:
   ```bash
   JFG_DEBUG_CONTROLS=1 JFG_INPUT_SCRIPT="<Goldwood script>; 296+4 - 0 1; 302+2 - 1 0; 304+1.5 R" timeout -k 3 312 build/JFGRecompiled > build/diag/controls1.log 2>&1
   ```
   (Add `R` to `JFG_INPUT_SCRIPT`'s button names if it's missing: it's in the table, 0x0010.)
6. Confirm from `build/diag/controls1.log`:
   - (a) the state is 0 while running and 11 while R is held;
   - (b) the relation between `orbit` and `cam yaw` while following, e.g. `cam yaw ≈ 0x8000 − orbit` or `≈ orbit + 0x8000`;
   - (c) `index` 0 and `character` ∈ {0, 1, 2} for Juno.

Record (a)–(c) under "Findings" at the end of this plan.

- [x] **Step 8: Commit.**
```bash
git add src/jfg/controls include/jfg_controls_hooks.h tests/controls/gate_test.cpp recomp/jfg.toml.in CMakeLists.txt scripts/test-hooks.sh docs/superpowers/plans/2026-10-14-jfg-modern-controls.md
git commit -m "Controls: gate, hooks scaffolding and JFG_DEBUG_CONTROLS log" && git push -q origin modern-controls
```

---

### Task 2: Settings (scheme, camera mode, drift, sensitivities, inversion, aim button)

**Files:**
- Create: `src/jfg/controls/settings.h`, `src/jfg/controls/settings.cpp`, `tests/controls/settings_test.cpp`
- Modify: `src/game/config.cpp` (save/load in `general.json`), `src/ui/ui_config.cpp` (data model), `assets/config_menu/general.rml` (rows), `src/jfg/controls/hooks.cpp` (`scheme_modern()` uses settings), `scripts/test-hooks.sh`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `jfg::controls::Settings { bool modern=true; int camera_mode=0 /*0 free*/; float drift_delay=1.5f /*<0 = off*/; float stick_yaw_dps=180, stick_pitch_dps=120; float aim_mult=0.6f; bool invert_mouse_y=false, invert_stick_y=false; bool aim_toggle=false; }`
  - `Settings& settings()`
  - `void to_json(nlohmann::json&, const Settings&)`, `void from_json(const nlohmann::json&, Settings&)` (missing keys keep the defaults)

- [x] **Step 1: Failing test**

`tests/controls/settings_test.cpp`: round-trip a non-default `Settings` through JSON, check that every field survives, and check that `from_json` of `{}` leaves the defaults. Assertions:
```cpp
jfg::controls::Settings s; s.modern = false; s.drift_delay = -1; s.stick_yaw_dps = 90; s.aim_toggle = true; s.invert_mouse_y = true;
nlohmann::json j = s; jfg::controls::Settings t = j.get<jfg::controls::Settings>();
assert(!t.modern && t.drift_delay == -1 && t.stick_yaw_dps == 90 && t.aim_toggle && t.invert_mouse_y);
jfg::controls::Settings d = nlohmann::json::object().get<jfg::controls::Settings>();
assert(d.modern && d.drift_delay == 1.5f && d.aim_mult == 0.6f);
std::puts("settings_test: OK");
```
Compile line for `scripts/test-hooks.sh`:
```bash
"$CXX" -std=c++20 -Wall -Iinclude -I<json include dir found below> src/jfg/controls/settings.cpp tests/controls/settings_test.cpp -o build/settings_test
build/settings_test
```
Before writing the line, find the json include directory with `find lib include -name json.hpp -path "*json/json.hpp" | head -1`, then use that directory's parent as `-I` (the frontend includes `json/json.hpp`). Use only that one path in the compile line.

- [x] **Step 2: Run, expect a compile failure.**

- [x] **Step 3: Implement `settings.{h,cpp}`** with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, modern, camera_mode, drift_delay, stick_yaw_dps, stick_pitch_dps, aim_mult, invert_mouse_y, invert_stick_y, aim_toggle)` and a `static Settings g;` returned by `settings()`.

- [x] **Step 4: Run, expect `settings_test: OK`.**

- [x] **Step 5: Save and load.**

In `src/game/config.cpp`:
- `save_general_config`: add `config_json["controls"] = jfg::controls::settings();`
- the load function: add `jfg::controls::settings() = from_or_default(config_json, "controls", jfg::controls::Settings{});`

Follow the existing `from_or_default` pattern (around lines 231–263).

- [x] **Step 6: Menu rows.**

In `src/ui/ui_config.cpp`, bind with the same `constructor.BindFunc` getter/setter pattern used for `mouse_sensitivity` (search `general_model_handle`):
- `controls_modern` (radio Modern/Original);
- `controls_drift` (range 0–50 meaning 0–5.0 s; 0 = Off, stored as −1);
- `controls_stick_speed` (range 30–360 °/s);
- `controls_aim_mult` (range 10–100 %);
- `controls_invert_mouse`, `controls_invert_stick` (radio On/Off);
- `controls_aim_toggle` (radio Hold/Toggle).

Setters write `jfg::controls::settings()` and call `general_model_handle.DirtyVariable(...)`.

In `assets/config_menu/general.rml`, add one `config-option` block per setting after the Joystick Deadzone row. Copy the existing radio and range markup, with new `set_cur_config_index(N)` numbers continuing the sequence, and fix the `nav-up`/`nav-down` ids of the rows before and after.

- [x] **Step 7: Wire the scheme.** In `hooks.cpp`, `scheme_modern()` returns `jfg::controls::settings().modern`.

- [x] **Step 8: Build and check that the settings persist.**

Build. Run once with `JFG_AUTOSTART=1` for 10 s:
- `python3 -c "import json; print(json.load(open('build/appdata/general.json'))['controls'])"` prints the defaults (`modern: True`, `drift_delay: 1.5`, ...).
- `strings build/JFGRecompiled | grep -c controls_modern` is ≥ 1, so the menu bindings are compiled in.

The menu rows themselves are checked by the user in Task 11 (they need clicks).

- [x] **Step 9: Commit and push.**

---

### Task 3: Modern actions, mouse buttons, and translation to N64 buttons

**Files:**
- Create: `src/jfg/controls/translate.h`, `src/jfg/controls/translate.cpp`, `tests/controls/translate_test.cpp`
- Modify: `include/recomp_input.h` (new inputs), `src/game/input.cpp` (mouse buttons and wheel as digital inputs, scanning, an unsuppressed axis getter), `src/game/controls.cpp` (Modern path in `get_n64_input`), `src/jfg/controls/hooks.cpp` (force the Normal key map), `recomp/jfg.toml.in`

**Interfaces:**
- Produces:
  - `struct Actions { bool fire, aim, jump, crouch, weapon_prev, weapon_next, quick[4], start; }`
  - `uint16_t to_n64_buttons(const Actions&, bool aim_held_effective)`:
    - Normal key map: Fire→Z `0x2000`, Aim→R `0x0010`, Jump→A `0x8000`, Crouch→B `0x4000`, Next→C-up `0x0008`, Previous→C-down `0x0004`;
    - quick weapons: D-pad up `0x0800`, right `0x0100`, down `0x0400`, left `0x0200`;
    - Start `0x1000`.
  - `bool update_aim_toggle(bool pressed_edge, bool current)`
  - Frontend: `GameInput` entries `JFG_FIRE, JFG_AIM, JFG_JUMP, JFG_CROUCH, JFG_WEAPON_PREV, JFG_WEAPON_NEXT, JFG_QUICK_1..4, JFG_LOOK_LEFT, JFG_LOOK_RIGHT, JFG_LOOK_UP, JFG_LOOK_DOWN`
  - `float recomp::get_input_analog_unsuppressed(const std::span<const InputField>)`

- [ ] **Step 1: Failing translation test.**

`tests/controls/translate_test.cpp` asserts:
- each single action maps to its bit;
- several actions combine with OR;
- `translate_original_passthrough`: `to_n64_buttons` isn't used in Original. That half is a frontend check, Step 7. Here, test that `Actions{}` maps to 0.
- `update_aim_toggle(true, false) == true`, `update_aim_toggle(true, true) == false`, `update_aim_toggle(false, x) == x`.

- [ ] **Step 2: Run, expect failure.**

- [ ] **Step 3: Implement `translate.{h,cpp}`** with exactly the bit table in Interfaces.

- [ ] **Step 4: Run, expect `translate_test: OK`.**

- [ ] **Step 5: New bindable inputs.**

In `include/recomp_input.h`, add `#define DEFINE_JFG_MODERN_INPUTS()` with:
- `DEFINE_INPUT(JFG_FIRE, 0, "Fire (Modern)")`, plus Aim, Jump, Crouch/Roll, Previous weapon, Next weapon, Quick weapon 1–4;
- `DEFINE_INPUT(JFG_LOOK_LEFT, 0, "Look left (Modern)")`, and right, up, down.

Include it in `DEFINE_ALL_INPUTS()` after the N64 buttons and axes. Extend `DefaultN64Mappings` with matching `std::vector<InputField>` fields, and extend `get_default_mapping_for_input` with their cases.

Defaults, in `src/game/input.cpp` where `default_n64_keyboard_mappings` and `default_n64_controller_mappings` are defined:
- **Keyboard:** Fire = Mouse left, Aim = Mouse right, Jump = Space, Crouch = Left Ctrl, Previous = wheel down / Q, Next = wheel up / E, Quick 1–4 = keys 1–4. Look stays empty: the mouse is read as a delta, not as a binding.
- **Controller:** Fire = right-trigger axis, Aim = left-trigger axis, Jump = A, Crouch = B, Previous = left shoulder, Next = right shoulder, Quick 1–4 = D-pad up, right, down, left. Look = right stick axes (`SDL_CONTROLLER_AXIS_RIGHTX+1` with sign).

Use the same `{(uint32_t)InputType::..., id}` forms as the existing defaults.

- [ ] **Step 6: Mouse buttons and wheel as digital inputs.**

In `src/game/input.cpp`:
- `get_input_digital`, `case InputType::Mouse`: ids 1–5 are `SDL_BUTTON(id) & SDL_GetMouseState(nullptr, nullptr)`. Ids 100 (wheel up) and 101 (wheel down) are true for one poll when `mouse_wheel_pos` changed in that direction since the previous poll; track the previous value in `InputState`.
- `get_input_analog`, Mouse: `get_input_digital ? 1.0f : 0.0f`.
- Scanning: in the `SDL_MOUSEBUTTONDOWN` handler, when scanning, call `set_scanned_input({(uint32_t)InputType::Mouse, button})`. In the `SDL_MOUSEWHEEL` handler, when scanning, use 100 or 101.
- `InputField::to_string` for Mouse: "Mouse Left/Right/Middle/4/5", "Wheel Up/Down".
- Add `get_input_analog_unsuppressed`, which is `get_input_analog` but with `controller_axis_state(id, false)` for `ControllerAnalog`.

- [ ] **Step 7: The Modern path in `get_n64_input`** (`src/game/controls.cpp`).

If `jfg::controls::settings().modern`:
1. Read each `JFG_*` action from both mapping arrays with `get_input_digital`. Trigger axes count when ≥ the existing axis threshold.
2. Update the aim toggle state if `aim_toggle`.
3. `cur_buttons = to_n64_buttons(actions, aim_effective) | (START binding as before)`.
4. Build the movement stick from the existing N64 axis bindings (left stick / WASD), then multiply x and y by `80.0f / 127.0f`. The runtime reports ±1.0 as ±127 and the game expects ±80 for full tilt.
5. Call `recomp::set_right_analog_suppressed(true)`. In Original, call `set_right_analog_suppressed(false)` and run the existing code unchanged.
6. The scripted-input path (`JFG_INPUT_SCRIPT`) still ORs its buttons on top in both modes.

- [ ] **Step 8: Force the Normal key map in Modern.**

Add a hook in `recomp/jfg.toml.in`:
```toml
[[patches.hook]]
func = "func_80032A48"          # controlPlayer, after controlModeKeys is chosen
before_vram = 0x80032B40
text = "jfg_controls_force_normal_keymap(rdram, ctx);"
```
Confirm 0x80032B40 is the instruction right after `controlModeKeys` (`0x800A18B0`) is stored: disasm `0x80032B00` for 32 instructions and look for `sw ... 0x18B0(...)`. Use the address after that store.

`jfg_controls_force_normal_keymap`: if `g_frame.active`, `MEM_W(0, 0x800A18B0) = 0x800A18B4`. Declare it in `jfg_controls_hooks.h`.

- [ ] **Step 9: Build and check in game.**

Use a scripted run to Goldwood. The Goldwood script already uses N64 button names, which still work in Modern: the scripted buttons are ORed in. Then `scripts/screenshots.sh` at 300–310 s, to confirm Juno still moves and the game didn't crash.

- [ ] **Step 10: Commit and push.**

---

### Task 4: Modern input snapshot and the input script's look axes

**Files:**
- Create: `src/jfg/controls/input_state.h`, `src/jfg/controls/input_state.cpp`, `tests/controls/input_state_test.cpp`
- Modify: `include/jfg_input_script.h`, `src/jfg/input_script.cpp`, `tests/input_script_test.cpp` (look axes), `src/jfg/controls/hooks.cpp` (`jfg_controls_latch`), `src/game/controls.cpp` (publish look and move to the latch source)

**Interfaces:**
- Produces:
  - `struct ModernInput { float move_x, move_y; float stick_look_x, stick_look_y; float mouse_dx, mouse_dy; bool aim_held; }`
  - `const ModernInput& frame()`: the snapshot for this game frame.
  - `void publish(const ModernInput&)`: called by the frontend each poll. Mouse deltas accumulate across polls until `latch()`.
  - `void latch()`: moves the published values into the frame snapshot and zeroes the accumulated mouse delta.
  - Input script fields: `T+D BUTTONS [X Y [RX RY [MDX MDY]]]`, where `MDX MDY` are mouse counts per game frame for the duration.

- [ ] **Step 1: Failing tests.**
  - `input_state_test`: two `publish` calls with mouse deltas 3 and 4, then `latch()` gives `frame().mouse_dx == 7`. A second `latch()` with no publish gives 0.
  - `input_script_test` additions: parsing `"5+1 - 0 0 0.5 -0.5 10 0"` gives `rx 0.5`, `ry -0.5`, `mdx 10`; `apply` at t=5.5 adds them.

- [ ] **Step 2: Run, expect failures.**

- [ ] **Step 3: Implement.**
  - `input_state.cpp` keeps a published copy and a mouse accumulator under a `std::mutex`. The frontend thread publishes; the game thread latches.
  - Extend `InputEvent` with `rx, ry, mdx, mdy` (default 0). `apply_input_script` gains `float* rx, float* ry, float* mdx, float* mdy` out-parameters. Keep the old overload for existing callers.

- [ ] **Step 4: Run, expect OK.**

- [ ] **Step 5: Frontend publish.**

In `get_n64_input` (Modern path), publish each call:
- move: the float movement vector after deadzone, before the ±80 scaling;
- stick look: `JFG_LOOK_*` via `get_input_analog_unsuppressed`, right minus left and up minus down, through `apply_joystick_deadzone`;
- mouse: `recomp::get_mouse_deltas`, plus the script's `mdx/mdy`;
- `aim_held`: the effective aim.

`jfg_controls_latch` calls `jfg::controls::latch()`.

- [ ] **Step 6: Debug check.**

Add `look=(%.2f,%.2f) mouse=(%.1f,%.1f)` to the `JFG_DEBUG_CONTROLS` line. Run a script with `300+2 - 0 0 1 0` (right stick right) and `303+1 - 0 0 0 0 20 0` (mouse right). Expected: the log shows `look=(1.00,0.00)` for about 60 frames, and `mouse=(20.0,0.0)` per frame for about 30 frames.

- [ ] **Step 7: Commit and push.**

---

### Task 5: Free camera logic (pure)

**Files:**
- Create: `src/jfg/controls/free_camera.h`, `src/jfg/controls/free_camera.cpp`, `tests/controls/free_camera_test.cpp`

**Interfaces:**
- Consumes: `ModernInput` (Task 4), `Settings` (Task 2).
- Produces:
  ```cpp
  struct FreeCameraState { int16_t yaw = 0; float pitch = 0.f; float idle = 0.f; bool synced = false; };
  struct LookParams { float mouse_counts_to_units; float stick_yaw_units_per_s; float stick_pitch_per_s; bool invert_mouse_y, invert_stick_y; };
  // One follow-camera frame: integrate look input. Returns true when the free camera should own the orbit heading
  // this frame (idle < drift_delay or drift off); false lets the game's follow logic run (drift-back).
  bool free_camera_step(FreeCameraState&, const ModernInput&, const LookParams&, float drift_delay, float dt);
  // After another camera ran: adopt the given heading, centre pitch, mark idle past the delay.
  void free_camera_resync(FreeCameraState&, int16_t heading, float drift_delay);
  // Game heading read back while drifting.
  void free_camera_follow(FreeCameraState&, int16_t heading);
  ```
  - Yaw units: s16 binary angle. Pitch: −1…1. Mouse wins when both mouse and stick give input in the same frame.
  - Sign conventions: mouse/stick right decreases `yaw`, and mouse down/stick down decreases `pitch` (look down) unless inverted. Task 6 confirms the visible direction and flips constants there if needed.

- [ ] **Step 1: Failing tests** (`free_camera_test.cpp`). Each case asserts with explicit numbers:
  - `look_mouse_turns`: mouse_dx = 10 with 100 units per count gives yaw −1000 and `idle` 0, and returns true.
  - `look_stick_rate`: stick_x = 1 at 180°/s for dt = 1/30 gives yaw −(65536·0.5/30) ±1.
  - `look_mixed_input_mouse_wins`: mouse_dx = 10 and stick_x = 1 gives only the mouse's contribution.
  - `pitch_clamped_on_big_flick`: mouse_dy = 100000 gives pitch −1.0 exactly.
  - `invert_mouse_y`: the sign of the pitch change flips.
  - `drift_after_delay`: no input for 1.6 s at delay 1.5 makes it return false; with drift off (delay < 0), it still returns true.
  - `resync_after_other_camera`: resync(0x4000, 1.5) gives yaw 0x4000, pitch 0, and idle ≥ 1.5, so the next idle step returns false.
  - `deadzone_tiny_mouse_is_input`: mouse_dx = 1 resets `idle` (any mouse count is deliberate).

- [ ] **Step 2: Run, expect failure.**

- [ ] **Step 3: Implement `free_camera.cpp`:**
```cpp
bool free_camera_step(FreeCameraState& s, const ModernInput& in, const LookParams& p, float drift_delay, float dt) {
    const bool mouse = in.mouse_dx != 0.f || in.mouse_dy != 0.f;
    const bool stick = std::fabs(in.stick_look_x) > 0.f || std::fabs(in.stick_look_y) > 0.f;
    float dyaw = 0.f, dpitch = 0.f;
    if (mouse) {
        dyaw = -in.mouse_dx * p.mouse_counts_to_units;
        dpitch = -in.mouse_dy * (p.mouse_counts_to_units / 16384.f) * (p.invert_mouse_y ? -1.f : 1.f);
    } else if (stick) {
        dyaw = -in.stick_look_x * p.stick_yaw_units_per_s * dt;
        dpitch = in.stick_look_y * p.stick_pitch_per_s * dt * (p.invert_stick_y ? -1.f : 1.f);
    }
    s.yaw = int16_t(int32_t(s.yaw) + int32_t(std::lround(dyaw)));
    s.pitch = std::clamp(s.pitch + dpitch, -1.f, 1.f);
    s.idle = (mouse || stick) ? 0.f : s.idle + dt;
    return drift_delay < 0.f || s.idle < drift_delay;
}
void free_camera_resync(FreeCameraState& s, int16_t heading, float drift_delay) {
    s.yaw = heading; s.pitch = 0.f; s.idle = std::max(drift_delay, 0.f) + 1e-3f; s.synced = true;
}
void free_camera_follow(FreeCameraState& s, int16_t heading) { s.yaw = heading; }
```
(`mouse_counts_to_units / 16384` maps one count to the same angular rate in pitch, where pitch 1.0 = a quarter turn.)

- [ ] **Step 4: Run, expect `free_camera_test: OK`.**

- [ ] **Step 5: Commit and push.**

---

### Task 6: Free camera in game (yaw override, drift-back, pitch orbit)

**Files:**
- Modify: `src/jfg/controls/hooks.cpp`, `include/jfg_controls_hooks.h`, `recomp/jfg.toml.in`

**Interfaces:**
- Consumes: `free_camera_step/resync/follow` (Task 5), `frame()` (Task 4), `settings()` (Task 2), `g_frame` (Task 1).
- Produces hooks:
  - `jfg_free_camera_entry` (follow-camera entry: undo last frame's pitch orbit)
  - `jfg_free_camera_heading` (before `0x8002DFE8`: own or read back `pd+0x104`)
  - `jfg_free_camera_pitch` (right after the follow camera returns in the dispatcher: apply the pitch orbit)

- [ ] **Step 1: Locate the call sites.**

Run `grep -n "func_8002CF6C(rdram, ctx);" generated/funcs/*.c`. All hits should be inside `func_8002B418`. For each, take the vram of the instruction after the call's delay slot (from the `// 0x...` comments). Those are the `before_vram`s for `jfg_free_camera_pitch`.

Confirm the branch target at `0x8002DFE8` gets the hook text after its label: after Step 3's recompile, `grep -n -A2 "L_8002DFE8:" generated/funcs/*.c` must show the hook call on the next lines.

- [ ] **Step 2: Hooks in `recomp/jfg.toml.in`:**
```toml
[[patches.hook]]
func = "func_8002CF6C"          # follow camera entry: restore the unpitched camera from last frame
text = "jfg_free_camera_entry(rdram, ctx);"

[[patches.hook]]
func = "func_8002CF6C"          # both orbit-heading paths converge here (research §4.7)
before_vram = 0x8002DFE8
text = "jfg_free_camera_heading(rdram, ctx);"
```
Add one `jfg_free_camera_pitch` hook per call site from Step 1, each with `func = "func_8002B418"`.

- [ ] **Step 3: Implement the hooks** in `hooks.cpp`:
  - `jfg_free_camera_entry`:
    - If `g_frame.active`, set `g_frame.camera_kind = 1`.
    - If last frame applied a pitch orbit and `cam0()`'s position still equals the position we wrote, restore the saved unpitched position and pitch.
  - `jfg_free_camera_heading`:
    - If `!g_frame.active`, return.
    - Build `LookParams`:
      - `mouse_counts_to_units` = `recomp::get_mouse_sensitivity()` (0–100) × 3.0. This is a starting point; tune in Task 10.
      - `stick_yaw_units_per_s` = `stick_yaw_dps · 65536 / 360`.
      - `stick_pitch_per_s` = `stick_pitch_dps / 90`.
    - Call `free_camera_step(s, frame(), params, drift_delay, ticks/60.0f)`. `ticks` is not available in this function, so use `dt = 1/30` and check the frame cadence from the debug log.
    - If it returns true: `MEM_H(0x104, pd) = s.yaw` and `MEM_H(0x10A, pd) = 0`.
    - Otherwise: `free_camera_follow(s, MEM_H(0x104, pd))`.
    - If `!s.synced`, call `free_camera_resync` from `MEM_H(0x104, pd)` first.
  - `jfg_free_camera_pitch`:
    - If `!g_frame.active || g_frame.camera_kind != 1` or `|s.pitch| < 0.01`, return.
    - Otherwise, the camera position `c` (cam0 +0x0C/+0x10/+0x14) and the player head `h` (obj +0x0C/+0x10/+0x14, plus 20.0 on y; confirm the height in the debug log) define `v = c − h`, horizontal length `r = |v.xz|`, and current elevation `e = atan2(v.y, r)`.
    - New elevation `e' = clamp(e + s.pitch·(π/4), −π/12, 5π/12)`. Keep `|v|`.
    - `c' = h + (cos e'·v.xz/r·|v|, sin e'·|v|)`.
    - Clamp `c'.y ≥ obj.y + 4.0` (don't go under Juno's feet).
    - Write `c'`. Set cam pitch to `cam.pitch + int16((e' − e)·65536/(2π))`. Confirm the sign in Step 6: looking down means the camera moves up and pitch points down.
    - Save the originals and what was written, for `jfg_free_camera_entry`.
  - `jfg_controls_after_camera`: if `g_frame.active` and `camera_kind != 1`, call `free_camera_resync(s, heading_from_cam(cam0 yaw), drift_delay)`, where `heading_from_cam` is the relation recorded in Findings (b). Reset `camera_kind = 0` for the next frame.

- [ ] **Step 4: Build.** Run `scripts/recompile.sh && cmake --build ...`. Then check the `L_8002DFE8:` placement (Step 1) and that `jfg_free_camera_pitch` appears right after each `func_8002CF6C` call.

- [ ] **Step 5: Debug log checks** (Goldwood script plus inputs, `JFG_DEBUG_CONTROLS=1`):
  - (a) `300+2 - 0 0 1 0` (stick right): `orbit` changes steadily while `facing` doesn't (Juno is standing).
  - (b) `303+3 - 0 1` (run forward, no look input, 1.5 s past the last look): `orbit` approaches `0x8000 − facing` (drift-back) from about 1.5 s after the look input ended.
  - (c) `307+1 - 0 0 0 -1` (stick up, look up): the debug line `pitch` goes positive, and the camera y in the log goes down.

Record pass or fail for each with the log line numbers in Findings.

- [ ] **Step 6: Visual check.**
  - `scripts/screenshots.sh build/diag/freecam 299 301 302 304 306 308` with the same script. The camera has orbited around Juno at 301–302 s and swung back behind her by 306 s.
  - Then run a cutscene transition: start a new game; the intro ends in the teleporter, and gameplay follows. Check that the first gameplay frames show no snap, against frames 1 s apart.

If the visible yaw or pitch direction is opposite to the stick, flip the constant in `free_camera_step` and update its test expectations in the same commit.

- [ ] **Step 7: Commit and push.**

---

### Task 7: Aim controller logic (pure)

**Files:**
- Create: `src/jfg/controls/aim.h`, `src/jfg/controls/aim.cpp`, `tests/controls/aim_test.cpp`

**Interfaces:**
- Consumes: `ModernInput`.
- Produces:
  ```cpp
  struct AimLimits { int16_t yaw_lim, pitch_lim, pitch_min, pitch_max; };       // from the call (0xE38, 0xE38, -0x2AAA, 0x2AAA)
  struct AimState  { int16_t yaw_off, pitch_off; int16_t facing; int16_t pitch_base; float zoom; };
  struct AimOutput { AimState next; bool zoomed; };
  AimOutput aim_step(const AimState& cur, const ModernInput& in, const AimLimits& lim,
                     float mouse_units_per_count, float aim_mult, bool invert_mouse_y, bool invert_stick_y,
                     bool zoom_weapon, int zoom_dir /* -1 out, 0, +1 in */, float ticks);
  // Right stick, original semantics: position mapping, the stick rescaled from ±127 to the N64 ±80 range.
  int16_t stick_to_offset(float stick /* -1..1 */, int16_t lim);
  ```

- [ ] **Step 1: Failing tests** (`aim_test.cpp`, explicit numbers). Use `lim = {0xE38, 0xE38, -0x2AAA, 0x2AAA}` and 1 unit per count, aim_mult 1, zoom 1:
  - `aim_mouse_moves_crosshair`: dx = 100 gives `yaw_off == -100`, and facing unchanged.
  - `aim_overflow_turns_body`: `yaw_off = -0xE00`, dx = 100: the offset clamps to `-0xE38`, and the remaining 44 units turn the facing by −44.
  - `aim_overflow_turns_body_big_flick`: dx = 100000 gives `yaw_off == -0xE38`, and the facing changes by −(100000 − 0xE38) mod 65536 without UB. The arithmetic is in int32, then cast.
  - `aim_pitch_overflow_moves_base_clamped`: the pitch overflow goes into `pitch_base`, clamped to ±0x2AAA.
  - `aim_mouse_wins`: with both mouse and stick, only the mouse applies.
  - `aim_zoom_scales_sensitivity`: zoom 5 scales the mouse effect by `1/((5−1)·5/2+1) = 1/11`.
  - `stick_rescale`: `stick_to_offset(80.0/127.0, 0xE38) == 0xE38` (full deflection at the N64 range), and `stick_to_offset(0.25, 0xE38) ≈ 0xE38·(0.25·127/80)/(40/40)` clamped to `lim`, within ±2.
  - `zoom_steps`: `zoom_weapon`, `zoom_dir = +1`, ticks 2 gives zoom +0.1 and `zoomed` true; clamped to [1, 5].

- [ ] **Step 2: Run, expect failure.**

- [ ] **Step 3: Implement `aim.cpp`.**
  - Mouse: `want = cur.off + d·units·mult·s`, where `s` is the zoom factor. The clamp to the box gives the new offset; the overflow is `want − clamped`, which is added to the facing (yaw) or the pitch base (pitch, clamped).
  - Stick (only when the mouse is idle): the original position mapping `off = lim·clamp(raw/40, ±1)` with `raw = stick·127·(80/127)`, i.e. `stick·80`. Rim turning: when `|raw| > 45`, apply the game's rate table `n(n+1)` for `n = min(|raw|−45, 19)`, scaled by `s`, times `ticks`, to the facing or pitch base.
  - Zoom: `zoom = clamp(zoom + 0.05·zoom_dir·ticks, 1, 5)`, and `zoomed = zoom_dir != 0`.

- [ ] **Step 4: Run, expect `aim_test: OK`.**

- [ ] **Step 5: Commit and push.**

---

### Task 8: Aim controller in game (`controlGetManualAim` hook)

**Files:**
- Modify: `src/jfg/controls/hooks.cpp`, `include/jfg_controls_hooks.h`, `recomp/jfg.toml.in`

**Interfaces:**
- Consumes: `aim_step` (Task 7).
- Produces: hook `jfg_manual_aim(rdram, ctx)`. It returns 1 when it handled the call (then the hook text does `return;`), or 0 to fall through to the original.

- [ ] **Step 1: Hook:**
```toml
[[patches.hook]]
func = "func_8003AABC"          # controlGetManualAim(obj, pd, yawLim, pitchLim, pitchMin, pitchMax, &yawOff, &pitchOff, ticks)
text = "if (jfg_manual_aim(rdram, ctx)) return;"
```

- [ ] **Step 2: Implement `jfg_manual_aim`.**
  - If `!g_frame.active`, or `pd` (a1) isn't `g_frame.pd`, or the hovership flag `(pd+1 & 3) == 3` is set, return 0.
  - Arguments come from the caller's frame at entry (Juno's call site confirms the layout):
    - `yawLim = (int16)ctx->r6`, `pitchLim = (int16)ctx->r7`;
    - `pitchMin = MEM_W(0x10, ctx->r29)`, `pitchMax = MEM_W(0x14, ctx->r29)`;
    - `yawOffPtr = MEM_W(0x18, ctx->r29)`, `pitchOffPtr = MEM_W(0x1C, ctx->r29)`;
    - `ticks = *(float*)&MEM_W(0x20, ctx->r29)` (a float stored with `swc1`; read the word and bit-cast).
  - State in: `yaw_off = MEM_H(0, yawOffPtr)`, `pitch_off = MEM_H(0, pitchOffPtr)`, `facing = MEM_H(0x11C, pd)`, `pitch_base = MEM_H(0x1E2, pd)`, `zoom = float at pd+0x1EC`.
  - Zoom weapon: weapon type 7. Read it the way the original does (find the weapon-type read in `func_8003AABC`'s generated code, the first `lbu`/`lb` after the prologue that's compared with 7).
  - `zoom_dir` is +1 when Next weapon (C-up bit) is held, −1 when Previous (C-down) is held. In Modern those come from the wheel/bumpers, so while zoomed, `to_n64_buttons` already put them in C-up/C-down.
  - Call `aim_step`, then write back the offsets, facing, pitch base and zoom.
  - Return value: `ctx->r2 = zoomed ? 1 : 0`, as the original returns. Then return 1.

- [ ] **Step 3: Build** (recompile and cmake).

- [ ] **Step 4: Debug log check.**

Script: Goldwood, then `304+3 R`, plus `304.5+1 R 0 0 0 0 30 0` (mouse right while aiming).

Expected per frame while R is held: `state=11`, the logged `yaw_off` reaches the box edge (−0xE38) and `facing` then decreases. Add `yaw_off` and `pitch_off` to the debug line from the pointers saved in the hook.

- [ ] **Step 5: Visual check.**

`scripts/screenshots.sh build/diag/aim 304 305 306` with that script. The over-the-shoulder camera is visible; the crosshair moves right, then the view turns.

- [ ] **Step 6: Commit and push.**

---

### Task 9: Walking while aiming (analog)

**Files:**
- Create: `src/jfg/controls/aim_move.h`, `src/jfg/controls/aim_move.cpp`, `tests/controls/aim_move_test.cpp`
- Modify: `src/jfg/controls/hooks.cpp`, `include/jfg_controls_hooks.h`, `recomp/jfg.toml.in`

**Interfaces:**
- Consumes: `ModernInput.move_x/move_y` (Task 4).
- Produces:
  ```cpp
  struct MoveOutput { float forward; float strafe; };   // game convention: forward < 0 means moving forward
  MoveOutput aim_move(float cur_forward, float cur_strafe, float move_x, float move_y, int16_t facing, int16_t cam_yaw, float ticks);
  ```
  Caps: forward speed −4.0 (forward) … +3.0 (back), strafe ±2.5. Approach rate 0.5 per tick, clamped so it never overshoots.

- [ ] **Step 1: Failing tests** (`aim_move_test.cpp`):
  - `forward_when_camera_and_facing_agree`: move (0, 1), facing == cam_yaw + 0x8000 (Findings b relation), ticks 10: forward reaches −4.0 and strafe stays 0.
  - `strafe_right`: move (1, 0) gives strafe +2.5 and forward 0.
  - `back_capped`: move (0, −1) gives forward +3.0.
  - `diagonal_caps_each_axis`: move (0.707, 0.707) gives each component within its cap.
  - `approach_rate`: from 0 with move (0, 1) and ticks 1, forward == −0.5.

  The camera/facing relation in the first test must use the convention confirmed in Findings (b). If Findings (b) shows a different convention, write the test with the confirmed one.

- [ ] **Step 2: Run, expect failure.**

- [ ] **Step 3: Implement `aim_move.cpp`.**
  - World move direction = the stick rotated by the camera yaw. Use the same formula as the game's `heading = Arctanf(X, −Y) − cam->yaw`: `move_heading = atan2(x, -y)` in s16 minus `cam_yaw`, magnitude `min(|move|, 1)`.
  - Relative to the facing: `d = move_heading − facing`; `fwd_target = −cos(d)·mag·4.0`, clamped to [−4.0, +3.0]; `strafe_target = sin(d)·mag·2.5`.
  - Approach both at 0.5·ticks.

- [ ] **Step 4: Run, expect `aim_move_test: OK`.**

- [ ] **Step 5: Hooks.**

One `[[patches.hook]]` per character at the animation-select entry, `func = "m015_05328"` / `"m016_05120"` / `"m017_04C50"`, with `text = "jfg_aim_move(rdram, ctx);"`.

`jfg_aim_move`: a0/a1 are the obj and pd. Check this by reading Juno's call to `m016_05120` in `m016_00000`: the args set before the `jal`. Then:
- if `g_frame.active`, `pd == g_frame.pd` and the state is 11 or 5:
  - call `aim_move(MEM_F(0x04, pd), MEM_F(0x10, pd), move_x, move_y, MEM_H(0x11C, pd), cam yaw, ticks)` and write `pd+0x04` and `pd+0x10`;
  - `ticks`: if no argument carries it, use 2.0, the game's 30 fps tick (confirm from the `rate` argument in the debug log).
- In state 5 (crouch-aim), set nothing; side-roll comes from C-left/C-right, which `to_n64_buttons` sets from the move x when |x| > 0.5 while crouch-aiming. Add `crouch_aim_roll` to the translation: `move_x > 0.5` gives C-right and `< −0.5` gives C-left, only when the frontend knows aim and crouch are held.

- [ ] **Step 6: Build and check the debug log.**

Script: Goldwood, then `304+3 R` and `304.5+2 R 1 0` (strafe right while aiming). Expected: state 11, strafe → +2.5, Juno's position moves sideways in the log (add `pos` x and z to the debug line). The animation is checked visually in Step 7.

- [ ] **Step 7: Visual and sequence check (Review Focus 5).**

`scripts/screenshots.sh build/diag/aimwalk 305 306 307` with the strafe script: Juno strafes while aiming with a strafe animation, not sliding.

Then run `304+1 R; 304.3+0.2 A` (jump pressed while aiming). Expected: no jump, as in the original, and the state returns to 0 after R is released at 305 s, not stuck.

If the animation slides, add an animation fix in this task: set the walk/strafe animation the way the ground state does. Find where `m016_04934` (strafe helper) chooses the strafe animation and call the same path when |strafe| > |forward|.

- [ ] **Step 8: Commit and push.**

---

### Task 10: Mouse capture, Original regression and tuning defaults

**Files:**
- Modify: `src/game/input.cpp` (capture rule), `docs/superpowers/handoff-2026-10-14.md` (Modern controls section), this plan's Findings

- [ ] **Step 1: Capture rule.**

In `src/game/input.cpp` (around line 308), the `cursor_locked` condition becomes:

`(recomp::get_mouse_sensitivity() != 0 || jfg::controls::settings().modern) && !recomp::game_input_disabled() && ultramodern::is_game_started() && jfg::controls::g_frame.active`

`g_frame.active` is false in cutscenes and menus, because the game disables input. Add a short grace period: keep the lock for 0.5 s after `active` drops, so cutscene cuts don't flicker the cursor.

- [ ] **Step 2: Original regression.**

Add a `JFG_DEBUG_CONTROLS` field-only mode: with the scheme Original, the debug line still prints `state`, `facing`, `orbit` and `pos`.

1. Run the Goldwood script twice with `settings.modern = false` in `build/appdata/general.json`: once on this branch, once on `base-port` (build it in a temporary worktree, or `git stash` and rebuild).
2. Compare `state` and `orbit` sequences with `diff <(grep -o "state=.*orbit=[0-9A-F]*" a.log) <(grep -o ... b.log)`.

Expected: identical. If the base-port build lacks the debug line, compare screenshots at 300, 305 and 310 s instead, which should be pixel-equal within compression noise. Use `python3 -c` with PIL `ImageChops.difference(...).getbbox()` and expect None, or a tiny bbox from the HUD clock.

- [ ] **Step 3: Scheme switch mid-game (Review Focus 2).**

Add the debug switch `JFG_CONTROLS_SCHEME_FLIP_AT=<seconds>`. It flips `settings().modern` once, that many seconds after start, without saving.

Run the Goldwood script with the right stick held (`300+6 - 0 0 1 0`) and the flip at 303. Expected in the `JFG_DEBUG_CONTROLS` log:
- `active` goes 1 → 0 within a frame of 303 s;
- `orbit` stops following the stick;
- the right stick (now C-buttons again) produces C-pan values in `pan`.

- [ ] **Step 4: Handoff doc.**

Add a "Modern controls" section to the handoff doc: settings, default bindings, debug switches (`JFG_DEBUG_CONTROLS`, `JFG_CONTROLS_SCHEME_FLIP_AT`), the input-script axes, and Findings. Move the stick-range note for the Original scheme to its own item.

- [ ] **Step 5: Commit and push.** The loop stops here. Task 11 is the user's.

---

### Task 11: User playtest and acceptance (user)

- [ ] **Step 1:** The user plays Goldwood with Modern controls on a gamepad and on mouse plus keyboard, and reports the feel:
  - camera speed;
  - drift-back delay;
  - aim speed;
  - walking while aiming;
  - anything that fights the game.
- [ ] **Step 2:** Tune the defaults in `Settings` from the feedback. Re-run the Task 5, 7 and 9 tests, which pin behaviour rather than defaults.
- [ ] **Step 3:** Decide the next camera mode to add (1a, 1b or 3), or fallback camera B if pitch was too limited.

---

## Findings

Recorded during execution: runtime-confirmed facts with log references.

- (a) States while running and aiming: `state=0` standing and running, `state=11` for every frame R is held (34 frames for a 1.5 s hold at ~289 s; `build/diag/controls1.log`).
- (b) Orbit heading and camera yaw relation:
  - `pd+0x104` (orbit) is the direction from the camera to the player in the game's angle convention, `orbit = atan2(−dx, dz)` with `d = player − camera`. Measured 0x5667 against a logged 0x5663 (f=445).
  - "Behind the player" is `0x8000 − facing`. Running forward with `facing=8000` gives `orbit=0000` and `camyaw=0000`, and the camera sits at −z of the player.
  - `cam yaw` is not equal to the orbit: it runs ~0x7BB ahead at rest (`orbit=5663`, `camyaw=5E22`). Resyncs therefore read `pd+0x104`, never derive it from `cam yaw`.
  - The camera stays about 181–198 units away horizontally and ~170 above the player.
- (c) Player index and character values for Juno: `idx=0`, `chr=1` (`pd+1 & 3`).
- (d) `controlPlayer` argument, camera list, `disablejoy`:
  - `controlPlayer`'s `a0` is `obj`, and `pd = *(obj+0x68)` (`0x80032A68`).
  - The camera list (`camGetListPtr`) is the array at `0x800FA4D0`: s16 yaw at +0, s16 pitch at +2, f32 position at +0x0C/+0x10/+0x14.
  - `disablejoy` is the word at `0x800F6DBC`, tested first by `controlReadJoypad`.
  - The camera dispatcher `0x8002B418(obj, pd, ticks)` reuses `s0` at `0x8002B884`, so `pd` is captured at its entry. It also runs for a non-player `pd` (`801DC790`) during cutscenes.
  - Juno is controllable and standing from about 287 s to 296 s in the Goldwood script, a safe window for scripted test inputs. The scripted run then reaches Magnus's dialogue trigger at about 300.3 s.
- (e) Free camera checks (Task 6 Step 5 a–c):

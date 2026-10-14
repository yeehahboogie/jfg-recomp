#pragma once
#include <cstdint>
namespace jfg::controls {
// Modern actions for one frame, from the player's bindings.
struct Actions {
    bool fire = false, aim = false, jump = false, crouch = false;
    bool weapon_prev = false, weapon_next = false;
    bool quick[4] = {false, false, false, false}; // D-pad up, right, down, left
    bool start = false;
};
// The N64 buttons that perform these actions under the game's Normal key map. `aim_held_effective` is the aim
// state after hold/toggle handling; it becomes R.
uint16_t to_n64_buttons(const Actions& a, bool aim_held_effective);
// Aim toggle mode: a press flips the aim state.
bool update_aim_toggle(bool pressed_edge, bool current);
}

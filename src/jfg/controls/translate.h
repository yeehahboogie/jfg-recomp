#pragma once
#include <cstdint>
namespace jfg::controls {
// Modern actions for one frame, from the player's bindings.
struct Actions {
    bool fire = false, aim = false, jump = false, crouch = false;
    bool weapon_prev = false, weapon_next = false;
    bool quick[4] = {false, false, false, false}; // D-pad up, right, down, left
    bool c_left = false, c_right = false, l = false; // strafe left / right (and menu functions), L (menus)
    bool start = false;
};
// The N64 buttons that perform these actions under the game's Normal key map. `aim_held_effective` is the aim
// state after hold/toggle handling; it becomes R.
uint16_t to_n64_buttons(const Actions& a, bool aim_held_effective);
// While aiming, a strong sideways movement stick also presses C-left / C-right: the crouch-aim state side-rolls on
// them, and the standing aim state picks its strafe animation from them.
uint16_t aim_strafe_buttons(bool aiming, float move_x);
// Aim toggle mode: a press flips the aim state.
bool update_aim_toggle(bool pressed_edge, bool current);
}

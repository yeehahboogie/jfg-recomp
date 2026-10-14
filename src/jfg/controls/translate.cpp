#include "translate.h"
namespace jfg::controls {
namespace {
// N64 button bits (recomp_input.h DEFINE_N64_BUTTON_INPUTS).
constexpr uint16_t A = 0x8000, B = 0x4000, Z = 0x2000, START = 0x1000, R = 0x0010, C_UP = 0x0008, C_DOWN = 0x0004;
constexpr uint16_t quick_bits[4] = {0x0800, 0x0100, 0x0400, 0x0200}; // D-pad up, right, down, left
}

uint16_t to_n64_buttons(const Actions& a, bool aim_held_effective) {
    uint16_t b = 0;
    if (a.fire) b |= Z;
    if (aim_held_effective) b |= R;
    if (a.jump) b |= A;
    if (a.crouch) b |= B;
    if (a.weapon_next) b |= C_UP;
    if (a.weapon_prev) b |= C_DOWN;
    for (int i = 0; i < 4; i++) {
        if (a.quick[i]) b |= quick_bits[i];
    }
    if (a.start) b |= START;
    return b;
}

bool update_aim_toggle(bool pressed_edge, bool current) { return pressed_edge ? !current : current; }
}

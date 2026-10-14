#include <cassert>
#include <cstdio>
#include "../../src/jfg/controls/translate.h"
using jfg::controls::Actions;
using jfg::controls::to_n64_buttons;
using jfg::controls::update_aim_toggle;

static Actions with(bool Actions::*field) { Actions a{}; a.*field = true; return a; }

int main() {
    // Each action maps to the Normal key map's button (research §3).
    assert(to_n64_buttons(with(&Actions::fire), false) == 0x2000);        // Z
    assert(to_n64_buttons(with(&Actions::jump), false) == 0x8000);        // A
    assert(to_n64_buttons(with(&Actions::crouch), false) == 0x4000);      // B
    assert(to_n64_buttons(with(&Actions::weapon_next), false) == 0x0008); // C-up
    assert(to_n64_buttons(with(&Actions::weapon_prev), false) == 0x0004); // C-down
    assert(to_n64_buttons(with(&Actions::start), false) == 0x1000);       // Start
    // Aim is R, from the effective aim state (hold or toggle), not the raw button.
    assert(to_n64_buttons(with(&Actions::aim), false) == 0);
    assert(to_n64_buttons(Actions{}, true) == 0x0010);
    // Quick weapons: D-pad up, right, down, left (quickWeaponKey order).
    const uint16_t quick[4] = {0x0800, 0x0100, 0x0400, 0x0200};
    for (int i = 0; i < 4; i++) {
        Actions a{};
        a.quick[i] = true;
        assert(to_n64_buttons(a, false) == quick[i]);
    }
    // Several actions combine.
    Actions a{};
    a.fire = a.jump = a.weapon_next = true;
    assert(to_n64_buttons(a, true) == (0x2000 | 0x8000 | 0x0008 | 0x0010));
    // Nothing pressed: no buttons.
    assert(to_n64_buttons(Actions{}, false) == 0);
    // Aim toggle: a press flips it, no press keeps it.
    assert(update_aim_toggle(true, false) == true);
    assert(update_aim_toggle(true, true) == false);
    assert(update_aim_toggle(false, true) == true);
    assert(update_aim_toggle(false, false) == false);
    std::puts("translate_test: OK");
}

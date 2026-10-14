#include <cassert>
#include <cstdio>
#include "../../src/jfg/controls/input_state.h"
using namespace jfg::controls;
int main() {
    // Mouse deltas from every poll since the last latch add up; other fields are the latest state.
    ModernInput a{}; a.mouse_dx = 3; a.mouse_dy = 1; a.move_x = 0.5f; a.stick_look_x = 0.2f;
    ModernInput b{}; b.mouse_dx = 4; b.mouse_dy = -1; b.move_x = 0.25f; b.stick_look_x = 0.4f; b.aim_held = true;
    publish(a);
    publish(b);
    latch();
    assert(frame().mouse_dx == 7 && frame().mouse_dy == 0);
    assert(frame().move_x == 0.25f && frame().stick_look_x == 0.4f && frame().aim_held);
    // A frame with no new poll: no mouse movement, the held state stays.
    latch();
    assert(frame().mouse_dx == 0 && frame().mouse_dy == 0);
    assert(frame().move_x == 0.25f && frame().aim_held);
    std::puts("input_state_test: OK");
}

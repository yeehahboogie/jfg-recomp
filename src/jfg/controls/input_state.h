#pragma once
namespace jfg::controls {
// The modern controls' analog input for one game frame.
struct ModernInput {
    float move_x = 0, move_y = 0;             // movement stick after deadzone, -1..1 (+y forward)
    float stick_look_x = 0, stick_look_y = 0; // right stick after deadzone, -1..1 (+x right, +y up)
    float mouse_dx = 0, mouse_dy = 0;         // raw mouse counts since the previous frame (+x right, +y down)
    bool aim_held = false;                    // aim after hold/toggle handling
};
// Called by the frontend on every controller read. Mouse counts add up until the next latch().
void publish(const ModernInput& in);
// Called once per game frame (joyRead): takes the published input as this frame's snapshot.
void latch();
// This frame's snapshot.
const ModernInput& frame();
}

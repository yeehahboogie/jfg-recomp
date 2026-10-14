#pragma once
#include <cstdint>
#include "input_state.h"
namespace jfg::controls {
// The free camera's look state. `yaw` is an orbit heading in the game's pd+0x104 convention (the direction from the
// camera to the player); larger values turn the view to the right. `pitch` is -1 (look down) .. 1 (look up).
struct FreeCameraState {
    int16_t yaw = 0;
    float pitch = 0.f;
    float idle = 0.f;     // seconds since the last look input
    bool synced = false;  // yaw has been taken from the game at least once
};
struct LookParams {
    float mouse_counts_to_units;  // orbit units per mouse count (pitch: the same angle, 1.0 = a quarter turn)
    float stick_yaw_units_per_s;  // orbit units per second at full right-stick tilt
    float stick_pitch_per_s;      // pitch per second at full tilt
    bool invert_mouse_y, invert_stick_y;
};
// One follow-camera frame: integrate look input. Returns true when the free camera owns the orbit heading this frame
// (idle < drift_delay, or drift off when drift_delay < 0); false lets the game's follow logic swing it (drift-back),
// during which the pitch also eases back to level.
bool free_camera_step(FreeCameraState& s, const ModernInput& in, const LookParams& p, float drift_delay, float dt);
// After another camera ran: adopt the given heading, level the pitch, and mark idle past the delay.
void free_camera_resync(FreeCameraState& s, int16_t heading, float drift_delay);
// While drifting: adopt the game's heading so the next look input continues from the camera's actual direction.
void free_camera_follow(FreeCameraState& s, int16_t heading);
}

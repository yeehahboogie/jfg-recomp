#include "free_camera.h"
#include <algorithm>
#include <cmath>
namespace jfg::controls {
namespace {
constexpr float pitch_units_per_quarter_turn = 16384.f;
constexpr float pitch_recenter_rate = 2.f; // per second, exponential, while drifting back
}

bool free_camera_step(FreeCameraState& s, const ModernInput& in, const LookParams& p, float drift_delay, float dt) {
    const bool mouse = in.mouse_dx != 0.f || in.mouse_dy != 0.f;
    const bool stick = in.stick_look_x != 0.f || in.stick_look_y != 0.f;
    float dyaw = 0.f, dpitch = 0.f;
    if (mouse) { // the mouse wins when both move in the same frame
        dyaw = in.mouse_dx * p.mouse_counts_to_units;
        dpitch = -in.mouse_dy * (p.mouse_counts_to_units / pitch_units_per_quarter_turn) * (p.invert_mouse_y ? -1.f : 1.f);
    } else if (stick) {
        dyaw = in.stick_look_x * p.stick_yaw_units_per_s * dt;
        dpitch = in.stick_look_y * p.stick_pitch_per_s * dt * (p.invert_stick_y ? -1.f : 1.f);
    }
    s.yaw = (int16_t)(uint16_t)(int32_t)((int32_t)s.yaw + (int32_t)std::lround(dyaw));
    s.pitch = std::clamp(s.pitch + dpitch, -1.f, 1.f);
    s.idle = (mouse || stick) ? 0.f : s.idle + dt;

    const bool owned = drift_delay < 0.f || s.idle < drift_delay;
    if (!owned) {
        s.pitch *= std::exp(-pitch_recenter_rate * dt);
    }
    return owned;
}

void free_camera_resync(FreeCameraState& s, int16_t heading, float drift_delay) {
    s.yaw = heading;
    s.pitch = 0.f;
    s.idle = std::max(drift_delay, 0.f) + 1e-3f;
    s.synced = true;
}

void free_camera_follow(FreeCameraState& s, int16_t heading) { s.yaw = heading; }
}

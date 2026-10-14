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

Vec3 pull_in(Vec3 head, Vec3 hit, float margin) {
    const float dx = hit.x - head.x, dy = hit.y - head.y, dz = hit.z - head.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len <= margin) return head;
    const float k = (len - margin) / len;
    return {head.x + dx * k, head.y + dy * k, head.z + dz * k};
}

int16_t orbit_heading(Vec3 cam, Vec3 player) {
    const double a = std::atan2(-(double)(player.x - cam.x), (double)(player.z - cam.z));
    return (int16_t)(uint16_t)(int32_t)std::lround(a * 32768.0 / 3.14159265358979323846);
}

PitchOrbit pitch_orbit(Vec3 cam, Vec3 head, float floor_y, float pitch) {
    const float vx = cam.x - head.x, vy = cam.y - head.y, vz = cam.z - head.z;
    const float r = std::hypot(vx, vz);
    if (pitch == 0.f || r < 1e-4f) {
        return {cam, 0.f};
    }
    const float dist = std::hypot(r, vy);
    const float e = std::atan2(vy, r);
    const float limit = pitch > 0.f ? pitch_orbit_min_elevation : pitch_orbit_max_elevation;
    const float e2 = e + std::min(std::fabs(pitch), 1.f) * (limit - e);
    const float r2 = std::cos(e2) * dist;
    Vec3 pos{head.x + vx / r * r2, std::max(head.y + std::sin(e2) * dist, floor_y), head.z + vz / r * r2};
    return {pos, std::atan2(pos.y - head.y, r2) - e};
}
}

#include "aim.h"
#include <algorithm>
#include <cmath>
namespace jfg::controls {
namespace {
// The game's Powerf(x, n) with n = (int)ticks.
float powerf(float x, int n) {
    float r = 1.f;
    for (int i = 0; i < n; i++) r *= x;
    return r;
}

// One axis of the original stick logic: clamps `raw` to +-40 and returns the rim turn rate target.
float rim_target(int& raw, int16_t smoothed, int box, const AimTuning& t, float inv_zoom) {
    constexpr int rim_margin = 0x2D8;
    float target = 0.f;
    if (raw < -40) {
        if (raw < -45 && smoothed < rim_margin - box) {
            target = -t.rim_rate[std::min(-45 - raw, 19)] * inv_zoom;
        }
        raw = -40;
    } else if (raw > 40) {
        if (raw > 45 && smoothed > box - rim_margin) {
            target = t.rim_rate[std::min(raw - 45, 19)] * inv_zoom;
        }
        raw = 40;
    }
    return target;
}

float smooth_rate(float rate, float target, float k) {
    rate += (target - rate) * k;
    if (target == 0.f && rate > -1.f && rate < 1.f) rate = 0.f;
    return rate;
}

// Moves an offset by `delta` within +-box; returns the part that didn't fit.
int64_t move_in_box(int16_t& off, int64_t delta, int box) {
    const int64_t want = off + delta;
    const int64_t clamped = std::clamp<int64_t>(want, -box, box);
    off = (int16_t)clamped;
    return want - clamped;
}
}

AimOutput aim_step(const AimState& cur, const AimInput& in, bool mouse_mode, const AimLimits& lim, const AimTuning& t,
                   const AimOptions& opt, float ticks) {
    AimState s = cur;
    const int iticks = (int)ticks;

    // Zoom weapon: velocity eases towards the zoom rate; zoom stays within [1, 5].
    float z = 1.f;
    if (in.zoom_weapon) {
        const float target = in.zoom_dir > 0 ? t.zoom_in_vel : in.zoom_dir < 0 ? t.zoom_out_vel : 0.f;
        s.zoom_vel += (1.f - powerf(0.875f, iticks)) * (target - s.zoom_vel);
        s.zoom += s.zoom_vel * ticks;
        if (s.zoom < 1.f) { s.zoom = 1.f; if (s.zoom_vel < 0.f) s.zoom_vel = 0.f; }
        if (s.zoom > 5.f) { s.zoom = 5.f; if (s.zoom_vel > 0.f) s.zoom_vel = 0.f; }
        z = s.zoom;
    } else {
        s.zoom_vel = 0.f;
    }
    const float sens = 1.f / ((z - 1.f) * (z * 0.5f) + 1.f);
    const int yaw_box = (int)(lim.yaw_lim * sens), pitch_box = (int)(lim.pitch_lim * sens);

    const bool mouse = in.mouse_dx != 0.f || in.mouse_dy != 0.f;
    const bool stick = in.stick_x != 0.f || in.stick_y != 0.f;
    if (mouse) mouse_mode = true;
    else if (stick) mouse_mode = false;

    if (mouse_mode) {
        const float k = opt.mouse_units_per_count * sens;
        const float dy = opt.invert_mouse_y ? -in.mouse_dy : in.mouse_dy;
        const int64_t yaw_over = move_in_box(s.yaw_off, -std::llround(in.mouse_dx * k), yaw_box);
        const int64_t pitch_over = move_in_box(s.pitch_off, std::llround(dy * k), pitch_box);
        s.facing = (int16_t)(uint16_t)(uint64_t)(s.facing + yaw_over);
        s.pitch_base = (int16_t)std::clamp<int64_t>(s.pitch_base + pitch_over, lim.pitch_min, lim.pitch_max);
        s.yaw_rate = s.pitch_rate = 0.f;
        return {s, true, in.zoom_weapon ? 1 : 0};
    }

    // The original: the game negates the raw stick, maps +-40 to the box, and turns at the rim.
    int raw_x = -(int)std::lround(in.stick_x * 80.f);
    int raw_y = -(int)std::lround((opt.invert_stick_y ? -in.stick_y : in.stick_y) * 80.f);
    const float yaw_target = rim_target(raw_x, s.smoothed_yaw_off, yaw_box, t, 1.f / z);
    const float pitch_target = rim_target(raw_y, s.smoothed_pitch, pitch_box, t, 1.f / z);
    const float k = 1.f - powerf(t.rate_smooth, iticks);
    s.yaw_rate = smooth_rate(s.yaw_rate, yaw_target, k);
    s.pitch_rate = smooth_rate(s.pitch_rate, pitch_target, k);
    s.yaw_off = (int16_t)((yaw_box * raw_x) / 40);
    s.pitch_off = (int16_t)((pitch_box * raw_y) / 40);
    s.facing = (int16_t)(uint16_t)(s.facing + (int)(s.yaw_rate * ticks));
    s.pitch_base = (int16_t)std::clamp<int>(s.pitch_base + (int)(s.pitch_rate * ticks), lim.pitch_min, lim.pitch_max);
    return {s, false, in.zoom_weapon ? 1 : 0};
}
}

#pragma once
#include "json/json.hpp"
namespace jfg::controls {
// User settings for the modern control scheme, saved in general.json under "controls".
struct Settings {
    bool modern = true;            // control scheme: Modern (true) or Original
    int camera_mode = 0;           // 0 = free camera (the only mode so far)
    float drift_delay = 1.5f;      // seconds without look input before the camera drifts back; < 0 = off
    float stick_yaw_dps = 180.f;   // right-stick look speed, degrees per second
    float stick_pitch_dps = 120.f;
    float aim_mult = 0.6f;         // aim sensitivity relative to look sensitivity
    bool invert_mouse_y = false;
    bool invert_stick_y = false;
    bool aim_toggle = false;       // aim button: hold (false) or toggle
    bool extra_players = false;    // gamepads 2-4 become players 2-4 (false: every gamepad drives player 1)
};
// Missing keys keep their defaults, so older config files load.
void to_json(nlohmann::json& j, const Settings& s);
void from_json(const nlohmann::json& j, Settings& s);
Settings& settings();
}

#include <cassert>
#include <cstdio>
#include "../../src/jfg/controls/settings.h"
int main() {
    jfg::controls::Settings s;
    s.modern = false; s.drift_delay = -1; s.stick_yaw_dps = 90; s.aim_toggle = true; s.invert_mouse_y = true;
    s.extra_players = true;
    nlohmann::json j = s;
    jfg::controls::Settings t = j.get<jfg::controls::Settings>();
    assert(!t.modern && t.drift_delay == -1 && t.stick_yaw_dps == 90 && t.aim_toggle && t.invert_mouse_y);
    assert(t.extra_players);
    assert(t.camera_mode == 0 && t.stick_pitch_dps == 120 && t.aim_mult == 0.6f && !t.invert_stick_y);
    jfg::controls::Settings d = nlohmann::json::object().get<jfg::controls::Settings>();
    assert(d.modern && d.drift_delay == 1.5f && d.aim_mult == 0.6f && d.stick_yaw_dps == 180 && !d.aim_toggle);
    assert(!d.extra_players); // off by default: every gamepad drives player 1
    // A partial object (an older config) keeps the defaults for the missing keys.
    jfg::controls::Settings e = nlohmann::json{{"modern", false}}.get<jfg::controls::Settings>();
    assert(!e.modern && e.drift_delay == 1.5f);
    std::puts("settings_test: OK");
}

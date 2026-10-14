#include "settings.h"
namespace jfg::controls {
#define JFG_SETTINGS_FIELDS(X) X(modern) X(camera_mode) X(drift_delay) X(stick_yaw_dps) X(stick_pitch_dps) \
    X(aim_mult) X(invert_mouse_y) X(invert_stick_y) X(aim_toggle)

void to_json(nlohmann::json& j, const Settings& s) {
    j = nlohmann::json::object();
#define X(f) j[#f] = s.f;
    JFG_SETTINGS_FIELDS(X)
#undef X
}

void from_json(const nlohmann::json& j, Settings& s) {
    const Settings d;
#define X(f) s.f = j.value(#f, d.f);
    JFG_SETTINGS_FIELDS(X)
#undef X
}

Settings& settings() {
    static Settings g;
    return g;
}
}

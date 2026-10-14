#pragma once
#include <cstdint>
namespace jfg::controls {
// Modern replacement for controlGetManualAim (0x8003AABC). The stick path is the original's logic on the N64 stick
// range (+-80); the mouse path moves the crosshair within its box and turns the body or pitch base with the overflow.
// Angles are s16 binary angles; a positive pitch offset aims up, and aiming right lowers the yaw offset.

struct AimLimits { int16_t yaw_lim, pitch_lim, pitch_min, pitch_max; }; // call args: offset box, pitch-base clamp
struct AimTuning {             // the game's constants (the hook reads them from RDRAM)
    float rim_rate[20];        // 0x800A2E60: rim turn rate by min(|raw| - 45, 19)
    float rate_smooth;         // 0x800ABFCC: rim rate smoothing, Powerf(rate_smooth, ticks)
    float zoom_in_vel, zoom_out_vel; // 0x800ABFC4 / 0x800ABFC8
};
struct AimState {
    int16_t yaw_off = 0, pitch_off = 0;                   // crosshair offsets this call returns
    int16_t smoothed_yaw_off = 0, smoothed_pitch = 0;     // pd+0x1CE / pd+0x1E0 (read: the rim test)
    int16_t facing = 0, pitch_base = 0;                   // pd+0x11C / pd+0x1E2
    float yaw_rate = 0.f, pitch_rate = 0.f;               // pd+0x1E4 / pd+0x1E8
    float zoom = 1.f, zoom_vel = 0.f;                     // pd+0x1EC / pd+0x1F0
};
struct AimInput {
    float mouse_dx = 0.f, mouse_dy = 0.f; // counts (+x right, +y down)
    float stick_x = 0.f, stick_y = 0.f;   // right stick -1..1 (+y up)
    int zoom_dir = 0;                     // +1 zoom in, -1 out
    bool zoom_weapon = false;             // weapon type 7 and pd+0x18E clear
};
struct AimOptions { float mouse_units_per_count; bool invert_mouse_y, invert_stick_y; };
struct AimOutput {
    AimState next;
    bool mouse_mode;  // the mouse drove the crosshair last; it stays put without input
    int return_value; // what controlGetManualAim returns: 1 with the zoom weapon
};
// One call. `mouse_mode` is the previous call's output; mouse movement selects it, stick movement leaves it.
AimOutput aim_step(const AimState& cur, const AimInput& in, bool mouse_mode, const AimLimits& lim, const AimTuning& t,
                   const AimOptions& opt, float ticks);
}

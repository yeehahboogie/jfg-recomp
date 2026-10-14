#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/jfg/controls/aim.h"
using namespace jfg::controls;

// controlGetManualAim's limits as Juno passes them; test tuning (the hook reads the real values from the game).
static const AimLimits lim{0xE38, 0xE38, -0x2AAA, 0x2AAA};
static AimTuning tuning() {
    AimTuning t{};
    for (int n = 0; n < 20; n++) t.rim_rate[n] = (float)(n * (n + 1)); // the game's table is n(n+1)
    t.rate_smooth = 0.5f;
    t.zoom_in_vel = 0.05f;
    t.zoom_out_vel = -0.05f;
    return t;
}
static const AimOptions opts{1.f, false, false};
static AimInput mouse(float dx, float dy) { AimInput in{}; in.mouse_dx = dx; in.mouse_dy = dy; return in; }
static AimInput stick(float x, float y) { AimInput in{}; in.stick_x = x; in.stick_y = y; return in; }
static AimState base() { AimState s{}; s.facing = 0x8000; s.zoom = 1.f; return s; }

int main() {
    const AimTuning t = tuning();
    {   // aim_mouse_moves_crosshair: right = yaw offset down (the game negates the stick), body stays
        AimOutput o = aim_step(base(), mouse(100, 0), false, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -100 && o.next.facing == (int16_t)0x8000 && o.mouse_mode);
    }
    {   // aim_overflow_turns_body
        AimState s = base(); s.yaw_off = -0xE00;
        AimOutput o = aim_step(s, mouse(100, 0), true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -0xE38 && o.next.facing == (int16_t)(0x8000 - 44));
    }
    {   // aim_overflow_turns_body_big_flick: no wrap trouble
        AimOutput o = aim_step(base(), mouse(100000, 0), true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -0xE38);
        assert(o.next.facing == (int16_t)(uint16_t)(0x8000 - (100000 - 0xE38)));
    }
    {   // mouse down aims down (positive pitch is down, like the camera); invert flips it
        AimOutput o = aim_step(base(), mouse(0, 50), true, lim, t, opts, 2.f);
        assert(o.next.pitch_off == 50);
        AimOptions inv = opts; inv.invert_mouse_y = true;
        o = aim_step(base(), mouse(0, 50), true, lim, t, inv, 2.f);
        assert(o.next.pitch_off == -50);
    }
    {   // aim_pitch_overflow_moves_base_clamped
        AimState s = base(); s.pitch_off = 0xE00; s.pitch_base = 0x2000;
        AimOutput o = aim_step(s, mouse(0, 100000), true, lim, t, opts, 2.f);
        assert(o.next.pitch_off == 0xE38 && o.next.pitch_base == 0x2AAA);
        s.pitch_off = -0xE00; s.pitch_base = -0x2000;
        o = aim_step(s, mouse(0, -100000), true, lim, t, opts, 2.f);
        assert(o.next.pitch_off == -0xE38 && o.next.pitch_base == -0x2AAA);
    }
    {   // aim_mouse_wins
        AimInput in = mouse(10, 0); in.stick_x = 1.f;
        AimOutput o = aim_step(base(), in, false, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -10 && o.mouse_mode && o.next.yaw_rate == 0.f);
    }
    {   // the mouse crosshair stays where it was left
        AimState s = base(); s.yaw_off = 300; s.pitch_off = -200;
        AimOutput o = aim_step(s, AimInput{}, true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == 300 && o.next.pitch_off == -200 && o.mouse_mode);
    }
    {   // stick: the game's position mapping on the N64 range (+-80): quarter tilt = raw 20 = half the box
        AimOutput o = aim_step(base(), stick(0.25f, 0), true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == (0xE38 * -20) / 40 && !o.mouse_mode);
        o = aim_step(base(), stick(0, 0.25f), false, lim, t, opts, 2.f); // stick up aims up
        assert(o.next.pitch_off == (0xE38 * -20) / 40);
        AimOptions inv = opts; inv.invert_stick_y = true;
        o = aim_step(base(), stick(0, 0.25f), false, lim, t, inv, 2.f);
        assert(o.next.pitch_off == (0xE38 * 20) / 40);
    }
    {   // stick centred in stick mode: crosshair centred (original behaviour)
        AimState s = base(); s.yaw_off = 500;
        AimOutput o = aim_step(s, AimInput{}, false, lim, t, opts, 2.f);
        assert(o.next.yaw_off == 0 && !o.mouse_mode);
    }
    {   // stick_rim_turns: full tilt with the smoothed crosshair at its limit turns at table[19] / zoom, smoothed
        AimState s = base(); s.smoothed_yaw_off = -0xE38;
        AimOutput o = aim_step(s, stick(1.f, 0), false, lim, t, opts, 2.f);
        const float rate = -380.f * (1.f - 0.25f); // rate_smooth^2 = 0.25
        assert(o.next.yaw_off == -0xE38 && o.next.yaw_rate == rate);
        assert(o.next.facing == (int16_t)(0x8000 + (int)(rate * 2.f)));
    }
    {   // no rim turning until the smoothed crosshair reaches the edge, or below |raw| 46
        AimOutput o = aim_step(base(), stick(1.f, 0), false, lim, t, opts, 2.f);
        assert(o.next.yaw_rate == 0.f && o.next.facing == (int16_t)0x8000);
        AimState s = base(); s.smoothed_yaw_off = -0xE38;
        o = aim_step(s, stick(45.f / 80.f, 0), false, lim, t, opts, 2.f);
        assert(o.next.yaw_rate == 0.f);
    }
    {   // a small leftover rate stops once there is no target
        AimState s = base(); s.yaw_rate = 0.9f; s.pitch_rate = -0.5f;
        AimOutput o = aim_step(s, AimInput{}, false, lim, t, opts, 2.f);
        assert(o.next.yaw_rate == 0.f && o.next.pitch_rate == 0.f);
    }
    {   // zoom scales the mouse by s = 1 / ((z - 1) z / 2 + 1) and shrinks the box to lim * s
        AimState s = base(); s.zoom = 5.f;
        AimInput in = mouse(110, 0); in.zoom_weapon = true;
        AimOutput o = aim_step(s, in, true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -10);
        in = mouse(100000, 0); in.zoom_weapon = true;
        o = aim_step(s, in, true, lim, t, opts, 2.f);
        assert(o.next.yaw_off == -(int)(0xE38 / 11.f));
    }
    {   // zoom steps: velocity eases towards the zoom rate (0.875^ticks), zoom within [1, 5]; returns 1 for the zoom weapon
        AimState s = base();
        AimInput in{}; in.zoom_weapon = true; in.zoom_dir = 1;
        AimOutput o = aim_step(s, in, false, lim, t, opts, 2.f);
        const float vel = (1.f - 0.875f * 0.875f) * 0.05f;
        assert(std::fabs(o.next.zoom_vel - vel) < 1e-7f && std::fabs(o.next.zoom - (1.f + vel * 2.f)) < 1e-6f);
        assert(o.return_value == 1);
        s.zoom = 4.99f; s.zoom_vel = 0.05f;
        o = aim_step(s, in, false, lim, t, opts, 2.f);
        assert(o.next.zoom == 5.f && o.next.zoom_vel == 0.f);
        s.zoom = 1.01f; s.zoom_vel = -0.05f; in.zoom_dir = -1;
        o = aim_step(s, in, false, lim, t, opts, 2.f);
        assert(o.next.zoom == 1.f && o.next.zoom_vel == 0.f);
    }
    {   // other weapons: no zoom velocity, zoom left alone, returns 0
        AimState s = base(); s.zoom_vel = 0.3f; s.zoom = 2.f;
        AimInput in{}; in.zoom_dir = 1;
        AimOutput o = aim_step(s, in, false, lim, t, opts, 2.f);
        assert(o.next.zoom_vel == 0.f && o.next.zoom == 2.f && o.return_value == 0);
    }
    std::puts("aim_test: OK");
}

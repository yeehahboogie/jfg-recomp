#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/jfg/controls/free_camera.h"
using namespace jfg::controls;

// Look right increases the orbit heading (measured: stick right with camera yaw 0 moves Juno to -x and the
// camera's yaw and orbit increase as it follows; plan Findings b).
static const LookParams params{100.f, 65536.f * 0.5f, 120.f / 90.f, false, false};
static const float dt = 1.f / 30.f;

static ModernInput mouse(float dx, float dy) { ModernInput in{}; in.mouse_dx = dx; in.mouse_dy = dy; return in; }
static ModernInput stick(float x, float y) { ModernInput in{}; in.stick_look_x = x; in.stick_look_y = y; return in; }

int main() {
    {   // look_mouse_turns
        FreeCameraState s{}; s.idle = 0.7f;
        assert(free_camera_step(s, mouse(10, 0), params, 1.5f, dt));
        assert(s.yaw == 1000 && s.idle == 0.f);
    }
    {   // look_stick_rate: 180 deg/s for one 30 Hz frame
        FreeCameraState s{};
        free_camera_step(s, stick(1, 0), params, 1.5f, dt);
        assert(std::abs(s.yaw - 65536.0 * 0.5 / 30.0) <= 1.0);
    }
    {   // look_mixed_input_mouse_wins
        FreeCameraState s{};
        ModernInput in = mouse(10, 0); in.stick_look_x = 1;
        free_camera_step(s, in, params, 1.5f, dt);
        assert(s.yaw == 1000);
    }
    {   // yaw wraps as an s16 angle
        FreeCameraState s{}; s.yaw = 0x7FF0;
        free_camera_step(s, mouse(1, 0), params, 1.5f, dt);
        assert(s.yaw == (int16_t)(0x7FF0 + 100 - 0x10000));
    }
    {   // pitch_clamped_on_big_flick: mouse down looks down
        FreeCameraState s{};
        free_camera_step(s, mouse(0, 100000), params, 1.5f, dt);
        assert(s.pitch == -1.f);
    }
    {   // stick up looks up
        FreeCameraState s{};
        free_camera_step(s, stick(0, 1), params, 1.5f, dt);
        assert(std::abs(s.pitch - (120.f / 90.f) * dt) < 1e-6f);
    }
    {   // invert_mouse_y
        LookParams inv = params; inv.invert_mouse_y = true;
        FreeCameraState a{}, b{};
        free_camera_step(a, mouse(0, 10), params, 1.5f, dt);
        free_camera_step(b, mouse(0, 10), inv, 1.5f, dt);
        assert(a.pitch < 0.f && b.pitch == -a.pitch);
    }
    {   // invert_stick_y
        LookParams inv = params; inv.invert_stick_y = true;
        FreeCameraState a{}, b{};
        free_camera_step(a, stick(0, 1), params, 1.5f, dt);
        free_camera_step(b, stick(0, 1), inv, 1.5f, dt);
        assert(a.pitch > 0.f && b.pitch == -a.pitch);
    }
    {   // drift_after_delay
        FreeCameraState s{};
        bool owned = true;
        for (int i = 0; i < 48; i++) owned = free_camera_step(s, ModernInput{}, params, 1.5f, dt); // 1.6 s
        assert(!owned);
        FreeCameraState t{};
        for (int i = 0; i < 48; i++) owned = free_camera_step(t, ModernInput{}, params, -1.f, dt); // drift off
        assert(owned);
    }
    {   // pitch_recenters_while_drifting (drift on); stays put with drift off
        FreeCameraState s{}; s.pitch = 0.8f; s.idle = 2.f;
        for (int i = 0; i < 90; i++) free_camera_step(s, ModernInput{}, params, 1.5f, dt);         // 3 s
        assert(std::abs(s.pitch) < 0.01f);
        FreeCameraState t{}; t.pitch = 0.8f; t.idle = 2.f;
        for (int i = 0; i < 90; i++) free_camera_step(t, ModernInput{}, params, -1.f, dt);
        assert(t.pitch == 0.8f);
        FreeCameraState u{}; u.pitch = 0.8f;                                                      // inside the delay
        free_camera_step(u, ModernInput{}, params, 1.5f, dt);
        assert(u.pitch == 0.8f);
    }
    {   // resync_after_other_camera
        FreeCameraState s{}; s.pitch = 0.5f;
        free_camera_resync(s, 0x4000, 1.5f);
        assert(s.yaw == 0x4000 && s.pitch == 0.f && s.idle >= 1.5f && s.synced);
        assert(!free_camera_step(s, ModernInput{}, params, 1.5f, dt));
    }
    {   // follow: the game's heading is adopted while drifting
        FreeCameraState s{}; s.yaw = 10;
        free_camera_follow(s, 0x1234);
        assert(s.yaw == 0x1234);
    }
    {   // deadzone_tiny_mouse_is_input
        FreeCameraState s{}; s.idle = 5.f;
        assert(free_camera_step(s, mouse(1, 0), params, 1.5f, dt));
        assert(s.idle == 0.f);
    }
    std::puts("free_camera_test: OK");
}

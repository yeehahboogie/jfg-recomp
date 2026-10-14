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
    {   // pitch_orbit: pitch 0 leaves the camera where the game put it
        const Vec3 cam{0, 170, -190}, head{0, 20, 0};
        PitchOrbit o = pitch_orbit(cam, head, -1000.f, 0.f);
        assert(o.pos.x == cam.x && o.pos.y == cam.y && o.pos.z == cam.z && o.delta_elevation == 0.f);
    }
    {   // pitch_orbit: look up moves the camera down to the lowest elevation, keeping distance and direction
        const Vec3 cam{0, 170, -190}, head{0, 20, 0};
        const float dist = std::hypot(150.f, 190.f), e = std::atan2(150.f, 190.f);
        PitchOrbit o = pitch_orbit(cam, head, -1000.f, 1.f);
        const float dy = o.pos.y - head.y, dz = o.pos.z - head.z;
        assert(std::abs(std::hypot(dy, dz) - dist) < 1e-3f && std::abs(o.pos.x) < 1e-4f && dz < 0.f);
        assert(std::abs(std::atan2(dy, -dz) - pitch_orbit_min_elevation) < 1e-4f);
        assert(std::abs(o.delta_elevation - (pitch_orbit_min_elevation - e)) < 1e-4f);
    }
    {   // pitch_orbit: look down moves the camera up to the highest elevation
        const Vec3 cam{100, 120, 0}, head{0, 20, 0};
        PitchOrbit o = pitch_orbit(cam, head, -1000.f, -1.f);
        const float dy = o.pos.y - head.y, dx = o.pos.x - head.x;
        assert(std::abs(std::atan2(dy, dx) - pitch_orbit_max_elevation) < 1e-4f && std::abs(o.pos.z) < 1e-4f);
        assert(o.delta_elevation > 0.f);
    }
    {   // pitch_orbit: half-way look up is half-way between the game's elevation and the lowest
        const Vec3 cam{0, 120, -100}, head{0, 20, 0};
        PitchOrbit o = pitch_orbit(cam, head, -1000.f, 0.5f);
        const float e = std::atan2(100.f, 100.f);
        assert(std::abs(o.delta_elevation - 0.5f * (pitch_orbit_min_elevation - e)) < 1e-4f);
    }
    {   // pitch_orbit: never below the floor height (Juno's feet)
        const Vec3 cam{0, 170, -190}, head{0, 20, 0};
        PitchOrbit o = pitch_orbit(cam, head, 15.f, 1.f);
        assert(o.pos.y == 15.f);
    }
    {   // orbit_heading: the game's pd+0x104 convention, atan2(-dx, dz) of player - camera (plan Findings b)
        assert(orbit_heading(Vec3{0, 0, -190}, Vec3{0, 0, 0}) == 0);
        assert(orbit_heading(Vec3{100, 0, 0}, Vec3{0, 0, 0}) == 0x4000);
        assert(orbit_heading(Vec3{0, 0, 190}, Vec3{0, 0, 0}) == (int16_t)0x8000);
        // Logged in Goldwood (Task 1 run, f=632): camera (-65.7, 54.3, 1067.0), Juno (19.0, 3.7, 1151.8), orbit 0xE003.
        const int16_t h = orbit_heading(Vec3{-65.7f, 54.3f, 1067.0f}, Vec3{19.0f, 3.7f, 1151.8f});
        assert(std::abs((int)(uint16_t)h - 0xE003) < 0x10);
    }
    {   // pull_in: a ray from the head hit something; the camera goes just in front of it, on the same ray
        const Vec3 head{0, 20, 0}, hit{0, 20, -100};
        const Vec3 c = pull_in(head, hit, 8.f);
        assert(c.x == 0.f && c.y == 20.f && std::fabs(c.z - -92.f) < 1e-4f);
        // A hit closer than the margin leaves the camera at the head rather than behind it.
        const Vec3 d = pull_in(head, Vec3{0, 20, -5}, 8.f);
        assert(d.x == head.x && d.y == head.y && d.z == head.z);
    }
    std::puts("free_camera_test: OK");
}

#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/jfg/controls/aim_move.h"
#include "../../src/jfg/controls/translate.h"
using namespace jfg::controls;

// Conventions (plan Findings b): stick up runs along 0x8000 - cam_yaw; while aiming the camera is behind, so
// cam_yaw = 0x8000 - facing. pd+0x04 < 0 is forward; pd+0x10 > 0 is a strafe to the right.
static const int16_t facing = (int16_t)0x8000, cam_yaw = 0;
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    {   // forward_when_camera_and_facing_agree
        MoveOutput o = aim_move(0.f, 0.f, 0.f, 1.f, facing, cam_yaw, 10.f);
        assert(near(o.forward, -4.f) && near(o.strafe, 0.f));
    }
    {   // strafe_right
        MoveOutput o = aim_move(0.f, 0.f, 1.f, 0.f, facing, cam_yaw, 10.f);
        assert(near(o.strafe, 2.5f) && near(o.forward, 0.f));
        o = aim_move(0.f, 0.f, -1.f, 0.f, facing, cam_yaw, 10.f);
        assert(near(o.strafe, -2.5f));
    }
    {   // back_capped
        MoveOutput o = aim_move(0.f, 0.f, 0.f, -1.f, facing, cam_yaw, 10.f);
        assert(near(o.forward, 3.f) && near(o.strafe, 0.f));
    }
    {   // diagonal_caps_each_axis
        MoveOutput o = aim_move(0.f, 0.f, 0.7071f, 0.7071f, facing, cam_yaw, 10.f);
        assert(o.forward < 0.f && o.forward >= -4.f && o.strafe > 0.f && o.strafe <= 2.5f);
        assert(near(o.forward, -4.f * 0.7071f) && near(o.strafe, 2.5f * 0.7071f));
    }
    {   // the stick is camera-relative: turned body and camera together give the same result
        MoveOutput o = aim_move(0.f, 0.f, 0.f, 1.f, (int16_t)0x2000, (int16_t)0x6000, 10.f);
        assert(near(o.forward, -4.f) && near(o.strafe, 0.f));
    }
    {   // approach_rate: 0.5 per tick, no overshoot
        MoveOutput o = aim_move(0.f, 0.f, 0.f, 1.f, facing, cam_yaw, 1.f);
        assert(near(o.forward, -0.5f));
        o = aim_move(-3.9f, 0.f, 0.f, 1.f, facing, cam_yaw, 2.f);
        assert(near(o.forward, -4.f));
    }
    {   // no stick: both decay to rest
        MoveOutput o = aim_move(-2.f, 1.f, 0.f, 0.f, facing, cam_yaw, 2.f);
        assert(near(o.forward, -1.f) && near(o.strafe, 0.f));
    }
    {   // magnitude above 1 (keyboard diagonals) is capped
        MoveOutput o = aim_move(0.f, 0.f, 0.f, 1.5f, facing, cam_yaw, 10.f);
        assert(near(o.forward, -4.f));
    }
    {   // aim_strafe_buttons: a strong sideways stick while aiming also presses C-left/right (crouch-aim side-rolls)
        assert(aim_strafe_buttons(true, 0.8f) == 0x0001);
        assert(aim_strafe_buttons(true, -0.8f) == 0x0002);
        assert(aim_strafe_buttons(true, 0.3f) == 0);
        assert(aim_strafe_buttons(false, 1.f) == 0);
    }
    std::puts("aim_move_test: OK");
}

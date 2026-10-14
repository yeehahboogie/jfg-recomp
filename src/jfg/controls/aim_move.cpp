#include "aim_move.h"
#include <algorithm>
#include <cmath>
namespace jfg::controls {
namespace {
constexpr float rad_per_unit = 3.14159265358979f / 32768.f;

float approach(float cur, float target, float step) {
    return cur < target ? std::min(cur + step, target) : std::max(cur - step, target);
}
}

MoveOutput aim_move(float cur_forward, float cur_strafe, float move_x, float move_y, int16_t facing, int16_t cam_yaw,
                    float ticks) {
    float forward = 0.f, strafe = 0.f;
    const float mag = std::min(std::hypot(move_x, move_y), 1.f);
    if (mag > 0.f) {
        // The game's heading for the stick: Arctanf(X, -Y) - cam->yaw (research §4.2).
        const int32_t heading = (int32_t)std::lround(std::atan2(move_x, -move_y) / rad_per_unit) - cam_yaw;
        const float d = (int16_t)(uint16_t)(heading - facing) * rad_per_unit;
        forward = std::clamp(-std::cos(d) * mag * aim_move_forward_max, -aim_move_forward_max, aim_move_back_max);
        strafe = -std::sin(d) * mag * aim_move_strafe_max;
    }
    const float step = aim_move_accel * ticks;
    return {approach(cur_forward, forward, step), approach(cur_strafe, strafe, step)};
}
}

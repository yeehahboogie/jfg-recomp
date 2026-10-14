#pragma once
#include <cstdint>
namespace jfg::controls {
// Walking while aiming (states 11 and 5): the movement stick, camera-relative as on foot, split into forward and
// strafe speeds relative to the facing. Game conventions: forward < 0 moves forward (pd+0x04), strafe > 0 moves
// right (pd+0x10).
struct MoveOutput { float forward; float strafe; };
constexpr float aim_move_forward_max = 4.f; // about full run speed, as Expert mode's aim walk
constexpr float aim_move_back_max = 3.f;    // Expert mode's back-pedal cap
constexpr float aim_move_strafe_max = 2.5f; // the strafe helper's cap
constexpr float aim_move_accel = 0.5f;      // per tick
MoveOutput aim_move(float cur_forward, float cur_strafe, float move_x, float move_y, int16_t facing, int16_t cam_yaw,
                    float ticks);
}

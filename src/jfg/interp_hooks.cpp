// Frame interpolation, game side: hooks record matrix identities into jfg::interp::recorder() while a frame's
// display list is built; at the next graphics-task submit the frame goes to the renderer (src/main/rt64_render_context.cpp).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"
#include "jfg_interp.h"
#include "jfg_interp_hooks.h"

namespace {
constexpr gpr frame_pacing_mode = (gpr)(int32_t)0x800FECAC; // viFrameSync's retraces per frame: 2 = 30 fps, 3 = 20 fps
// The main display list's two buffers. In gameplay a module also submits a small display list between two main ones,
// after the next main frame's matrices have been made, so only a main submit takes the recorded frame.
constexpr gpr main_display_lists = (gpr)(int32_t)0x800FB400;
constexpr gpr camera_number = (gpr)(int32_t)0x800FA634; // camGetNo
constexpr gpr cameras = (gpr)(int32_t)0x800FA4D0;       // 4 camera structs, 0x4C bytes each
constexpr gpr anim_cameras = (gpr)(int32_t)0x801045B8;  // animcamera[4]: the object driving a cutscene camera
constexpr gpr static_camera = (gpr)(int32_t)0x800FB084;
uint64_t frames_submitted = 0;
const bool interp_debug = std::getenv("JFG_DEBUG_INTERP") != nullptr;

float read_f32(uint8_t* rdram, gpr address) {
    const uint32_t bits = (uint32_t)MEM_W(0, address);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
}

extern "C" void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dl = (uint32_t)ctx->r4 & 0x3FFFFFF;
    const int mode = MEM_BU(0, frame_pacing_mode);
    const uint16_t rate = (uint16_t)(60 / (mode > 0 ? mode : 2));
    const bool main = dl == ((uint32_t)MEM_W(0, main_display_lists) & 0x3FFFFFF) ||
                      dl == ((uint32_t)MEM_W(4, main_display_lists) & 0x3FFFFFF);
    jfg::interp::Recorder& recorder = jfg::interp::recorder();
    jfg::interp::Frame frame = main ? recorder.take(dl, rate) : recorder.empty(dl, rate);
    if (frame.conflicts > 0 && interp_debug) {
        std::fprintf(stderr, "[interp] frame %llu: %u addresses recorded with two identities (drawn as-is)\n",
                     (unsigned long long)frames_submitted, frame.conflicts);
    }
    const size_t dropped = jfg::interp::handoff().push(std::move(frame));
    if (dropped > 0) {
        std::fprintf(stderr, "[interp] renderer behind: dropped %zu recorded frames\n", dropped);
    }
    if (main) frames_submitted++;
}

extern "C" void jfg_interp_camera(uint8_t* rdram, recomp_context* ctx) {
    const int32_t number = MEM_W(0, camera_number);
    if (number < 0 || number > 3) return;
    const gpr c = cameras + 0x4C * number;
    jfg::interp::CameraPose pose{};
    for (int i = 0; i < 3; i++) pose.position[i] = read_f32(rdram, c + 0xC + 4 * i);
    pose.yaw = MEM_H(0, c);
    pose.pitch = MEM_H(2, c);
    pose.roll = MEM_H(4, c);
    pose.driver = (uint32_t)MEM_W(4 * number, anim_cameras) ^ ((uint32_t)MEM_W(0, static_camera) << 1);
    const uint32_t id = jfg::interp::camera_id(0, number);
    const bool cut = jfg::interp::cut_detector().cut(id, pose, frames_submitted);
    const uint32_t matrix = (uint32_t)MEM_W(0, ctx->r5) & 0x3FFFFFF;
    jfg::interp::recorder().add(matrix, id, cut ? RT64::JFG_SKIP_ALL : 0);
    static uint64_t logged_frame = ~0ull;
    if (interp_debug && cut && logged_frame != frames_submitted) {
        logged_frame = frames_submitted;
        std::fprintf(stderr, "[interp] frame %llu: camera %d cut (at %.0f %.0f %.0f, yaw %04X)\n",
                     (unsigned long long)frames_submitted, number, pose.position[0], pose.position[1], pose.position[2],
                     (uint16_t)pose.yaw);
    }
}

extern "C" void jfg_interp_level_start(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::cut_detector().reset();
    jfg::interp::object_serials().clear();
    jfg::interp::instance_serials().clear();
}

extern "C" void jfg_interp_object_created(uint8_t* rdram, recomp_context* ctx) {
    if (ctx->r2 != 0) jfg::interp::object_serials().release((uint32_t)ctx->r2);
}

extern "C" void jfg_interp_object_freed(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::object_serials().release((uint32_t)ctx->r4);
}

extern "C" void jfg_interp_instance_freed(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::instance_serials().release((uint32_t)ctx->r4);
}

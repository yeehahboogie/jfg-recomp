// Frame interpolation, game side: hooks record matrix identities into jfg::interp::recorder() while a frame's
// display list is built; at the next graphics-task submit the frame goes to the renderer (src/main/rt64_render_context.cpp).
#include "recomp.h"
#include "jfg_interp.h"
#include "jfg_interp_hooks.h"

namespace {
constexpr gpr frame_pacing_mode = (gpr)(int32_t)0x800FECAC; // viFrameSync's retraces per frame: 2 = 30 fps, 3 = 20 fps
// The main display list's two buffers. In gameplay a module also submits a small display list between two main ones,
// after the next main frame's matrices have been made, so only a main submit takes the recorded frame.
constexpr gpr main_display_lists = (gpr)(int32_t)0x800FB400;
}

extern "C" void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dl = (uint32_t)ctx->r4 & 0x3FFFFFF;
    const int mode = MEM_BU(0, frame_pacing_mode);
    const uint16_t rate = (uint16_t)(60 / (mode > 0 ? mode : 2));
    const bool main = dl == ((uint32_t)MEM_W(0, main_display_lists) & 0x3FFFFFF) ||
                      dl == ((uint32_t)MEM_W(4, main_display_lists) & 0x3FFFFFF);
    jfg::interp::Recorder& recorder = jfg::interp::recorder();
    jfg::interp::handoff().push(main ? recorder.take(dl, rate) : recorder.empty(dl, rate));
}

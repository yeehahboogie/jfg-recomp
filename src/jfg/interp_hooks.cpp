// Frame interpolation, game side: hooks record matrix identities into jfg::interp::recorder() while a frame's
// display list is built; at the next graphics-task submit the frame goes to the renderer (src/main/rt64_render_context.cpp).
#include "recomp.h"
#include "jfg_interp.h"
#include "jfg_interp_hooks.h"

namespace {
constexpr gpr frame_pacing_mode = (gpr)(int32_t)0x800FECAC; // viFrameSync's retraces per frame: 2 = 30 fps, 3 = 20 fps
}

extern "C" void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dl = (uint32_t)ctx->r4 & 0x3FFFFFF;
    const int mode = MEM_BU(0, frame_pacing_mode);
    const uint16_t rate = (uint16_t)(60 / (mode > 0 ? mode : 2));
    jfg::interp::handoff().push(jfg::interp::recorder().take(dl, rate));
}

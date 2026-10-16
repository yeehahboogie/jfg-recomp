// JFG_DEBUG_MTXSRC=<frame>: logs every mathMtxF2L conversion and every camConvertMatrixList queueing (matrices
// camTick converts in place) while frame <frame> is built, with the caller's return address, and each graphics-task
// submit, so tools/diag/mtx-provenance.py can name the writer of every matrix the
// display list loads (RT64's dmaMatrix logs the loads).
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "jfg_interp_probe.h"

namespace {
const long probe_frame = std::getenv("JFG_DEBUG_MTXSRC") ? std::atol(std::getenv("JFG_DEBUG_MTXSRC")) : -1;
// The frame whose display list is being built (game thread only). func_8004DA10 runs at the start of each frame
// and submits the display list built during the previous one, so matrices written after submit n belong to the
// frame submitted next.
long building = 0;
}

extern "C" void jfg_probe_submit(uint8_t* rdram, recomp_context* ctx) {
    if (probe_frame < 0) return;
    std::fprintf(stderr, "[mtxsrc] submit %ld dl %08X\n", building, (uint32_t)ctx->r4 & 0x3FFFFFF);
    building++;
}

extern "C" void jfg_probe_f2l(uint8_t* rdram, recomp_context* ctx) {
    if (probe_frame < 0 || building != probe_frame) return;
    std::fprintf(stderr, "[mtxsrc] write %ld %08X ra %08X via F2L\n", building, (uint32_t)ctx->r5 & 0x3FFFFFF,
                 (uint32_t)ctx->r31);
}

extern "C" void jfg_probe_list(uint8_t* rdram, recomp_context* ctx) {
    if (probe_frame < 0 || building != probe_frame) return;
    const uint32_t start = (uint32_t)ctx->r4 & 0x3FFFFFF;
    const int count = (int)(ctx->r5 & 0xFFFF);
    for (int i = 0; i < count && i < 256; i++) {
        std::fprintf(stderr, "[mtxsrc] write %ld %08X ra %08X via list\n", building, start + 0x40 * i, (uint32_t)ctx->r31);
    }
}

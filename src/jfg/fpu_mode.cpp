// JFG's code uses odd-numbered FPU registers as single-precision registers of their own (e.g. mul.s $f13 at
// 0x80073FC8 in the matrix code), which is only valid with the FPU in FR=1 mode. On the console the boot code
// leaves FR=1 set; the runtime starts the game and every thread it creates in FR=0 mode, where odd registers
// alias the upper half of the even one below and such code computes garbage (half of every matrix zero).
#include "jfg_fpu_mode.h"

namespace {
constexpr uint32_t status_fr = 0x04000000;
}

extern "C" void jfg_set_fpu_mode(uint8_t*, recomp_context* ctx) {
    cop0_status_write(ctx, (gpr)(int32_t)((uint32_t)cop0_status_read(ctx) | status_fr));
}

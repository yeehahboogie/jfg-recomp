// The native stand-ins for libultra functions the runtime doesn't provide.
#include <cassert>
#include <cstdio>
#include <vector>
#include "recomp.h"

extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void __osSpSetStatus_recomp(uint8_t* rdram, recomp_context* ctx);

int main() {
    std::vector<uint8_t> ram(0x800000);
    uint8_t* rdram = ram.data();
    recomp_context ctx{};
    ctx.r5 = (gpr)(int32_t)0x80001001;  // bitpattern pointer (odd address: exercises the byte-swizzled layout)
    ctx.r2 = 0x55;
    osPfsIsPlug_recomp(rdram, &ctx);
    assert(MEM_B(0, ctx.r5) == 0x01);  // a pak on controller 1, as the runtime's rumble support assumes
    assert(ctx.r2 == 0);
    ctx.r4 = 0x2; ctx.r2 = 0x77;
    __osSpSetStatus_recomp(rdram, &ctx);  // no RSP status in an HLE runtime: changes nothing
    assert(ctx.r2 == 0x77);
    std::puts("libultra_stubs_test: OK");
}

// jfg_set_fpu_mode must switch a context to FR=1 through the runtime's status-register write, keeping other bits.
#include <cassert>
#include <cstdio>
#include "jfg_fpu_mode.h"

static uint32_t written = 0;
static int writes = 0;
extern "C" void cop0_status_write(recomp_context* ctx, gpr value) { written = (uint32_t)value; writes++; ctx->status_reg = written; }
extern "C" gpr cop0_status_read(recomp_context* ctx) { return (gpr)(int32_t)ctx->status_reg; }

int main() {
    recomp_context ctx{};
    ctx.status_reg = 0x00000001;
    jfg_set_fpu_mode(nullptr, &ctx);
    assert(writes == 1);
    assert(written == (0x04000000u | 0x00000001u));
    std::puts("fpu_mode_test: OK");
}

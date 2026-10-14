// libultra functions that N64Recomp skips ("ignored") but that some of JFG's recompiled code still calls,
// and that N64ModernRuntime doesn't provide. Two get real implementations; the rest are internals of
// Controller Pak / SI code whose public entry points the runtime replaces, so reaching one means a
// caller still needs identifying: stop loudly instead of misbehaving.
#include <cstdio>
#include <cstdlib>

#include "recomp.h"

namespace {
[[noreturn]] void unsupported(const char* name) {
    std::fprintf(stderr,
                 "[jfg] FATAL: reached libultra internal %s, which the runtime doesn't implement.\n"
                 "      Its caller is a libultra function that still needs identifying in "
                 "recomp/function_overrides.toml.\n",
                 name);
    std::fflush(stderr);
    std::abort();
}
}

// s32 osPfsIsPlug(OSMesgQueue* mq, u8* bitpattern): which controllers have a pak plugged in.
// N64ModernRuntime models a Rumble Pak on controller 1 (osMotorInit/__osMotorAccess), so report that.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, ctx->r5) = 0x01;
    ctx->r2 = 0;
}

// void __osSpSetStatus(u32 data): writes SP_STATUS_REG. Rare's scheduler uses it around its RSP tasks;
// the runtime runs RSP tasks itself, so there is no RSP status to change.
extern "C" void __osSpSetStatus_recomp(uint8_t*, recomp_context*) {}

#define JFG_UNSUPPORTED(name) \
    extern "C" void name##_recomp(uint8_t*, recomp_context*) { unsupported(#name); }

JFG_UNSUPPORTED(__osPfsSelectBank)
JFG_UNSUPPORTED(__osContRamRead)
JFG_UNSUPPORTED(__osContRamWrite)
JFG_UNSUPPORTED(__osSiRawStartDma)
JFG_UNSUPPORTED(__osPfsRWInode)
JFG_UNSUPPORTED(__osSiGetAccess)
JFG_UNSUPPORTED(__osSiRelAccess)
JFG_UNSUPPORTED(__osCheckId)
JFG_UNSUPPORTED(__osContAddressCrc)

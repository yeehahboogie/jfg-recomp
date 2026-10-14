// Native replacement for JFG's CIC-6105 challenge routine (main 0x800436C0, called from module 36).
// The original waits for the serial bus, has module 45 build a PIF RAM image (0xFF fill, 0x0F at 0x2E/0x2F,
// the caller's 16-byte buffer copied to 0x30..0x3F: a 15-byte challenge and the command byte) and write it,
// reads the answer back, restarts controller reads, and copies PIF RAM 0x30..0x3F to the caller's buffer. The game later checks the answer in three places (main 0x80007D44,
// main 0x80037890, module 16 +0x164). There is no CIC or raw PIF access here, so compute the answer.
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "jfg_cic.h"
#include "jfg_cic_hook.h"

extern "C" void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx);

namespace {
constexpr int32_t si_event_queue = static_cast<int32_t>(0x800FB090); // $s0 in the original routine
}

extern "C" void jfg_cic_challenge(uint8_t* rdram, recomp_context* ctx) {
    const gpr block = ctx->r4;     // 16 bytes: challenge (15) + command byte, destined for PIF RAM 0x30..0x3F
    const gpr response = ctx->r5;  // 16 bytes: PIF RAM 0x30..0x3F after the CIC has answered

    // Wait for the controller read already in flight before using the serial bus, as the original does.
    ctx->r4 = (gpr)si_event_queue;
    ctx->r5 = 0;
    ctx->r6 = 1; // OS_MESG_BLOCK
    osRecvMesg_recomp(rdram, ctx);

    uint8_t challenge[15], answer[15];
    for (int i = 0; i < 15; i++) {
        challenge[i] = MEM_BU(i, block);
    }
    jfg_cic6105_response(challenge, answer);

    // Restart controller reads so the serial-bus event the caller waits for arrives, as the original does.
    ctx->r4 = (gpr)si_event_queue;
    osContStartReadData_recomp(rdram, ctx);

    for (int i = 0; i < 15; i++) {
        MEM_BU(i, response) = answer[i];
    }
    MEM_BU(15, response) = 0; // the PIF clears the command byte once it has answered

    if (std::getenv("JFG_DEBUG_MODULES") != nullptr) {
        std::fprintf(stderr, "[jfg] CIC challenge ");
        for (int i = 0; i < 15; i++) std::fprintf(stderr, "%02X", challenge[i]);
        std::fprintf(stderr, " -> ");
        for (int i = 0; i < 15; i++) std::fprintf(stderr, "%02X", answer[i]);
        const bool ok = answer[4] == 0xDB && answer[5] == 0x4F && answer[12] == 0xE7 && answer[13] == 0x73;
        std::fprintf(stderr, " (game's checks %s)\n", ok ? "pass" : "FAIL");
    }
}

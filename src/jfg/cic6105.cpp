// The CIC-NUS-6105 lockout chip's challenge/response, which Jet Force Gemini checks for anti-piracy.
// The port has no CIC, so it computes the chip's answer. Algorithm as documented by emulator authors
// (X-Scale's implementation, used by mupen64plus); written independently here.
#include "jfg_cic.h"

namespace {
constexpr uint8_t table_a[16] = {0x4, 0x7, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0xF, 0x8, 0xF, 0x6, 0x3, 0x6, 0x9};
constexpr uint8_t table_b[16] = {0x4, 0x1, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0x9, 0x8, 0x5, 0x6, 0x3, 0xC, 0x9};
}

extern "C" void jfg_cic6105_response(const uint8_t challenge[15], uint8_t response[15]) {
    uint8_t key = 0xB;
    const uint8_t* table = table_a;
    for (int i = 0; i < 30; i++) {
        const uint8_t in = (i & 1) ? (challenge[i / 2] & 0xF) : (challenge[i / 2] >> 4);
        const uint8_t out = (key + 5 * in) & 0xF;
        if (i & 1) {
            response[i / 2] |= out;
        } else {
            response[i / 2] = out << 4;
        }
        key = table[out];

        // Which table the next nibble uses depends on this output nibble (and, with table B, a few exceptions).
        const bool negative = (out >> 3) & 1;
        const uint8_t magnitude = (negative ? ~out : out) & 0x7;
        bool use_b = (magnitude % 3 == 1) ? negative : !negative;
        if (table == table_b && (out == 0x1 || out == 0x9)) {
            use_b = true;
        }
        if (table == table_b && (out == 0xB || out == 0xE)) {
            use_b = false;
        }
        table = use_b ? table_b : table_a;
    }
}

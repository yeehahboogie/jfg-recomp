// The CIC-NUS-6105 challenge/response used by JFG's anti-piracy check (PIF RAM 0x30..0x3E in and out).
#include <cassert>
#include <cstdio>
#include <cstring>
#include "jfg_cic.h"

static void check(const char* challenge_hex, const char* expected_hex) {
    uint8_t challenge[15], expected[15], got[15];
    for (int i = 0; i < 15; i++) {
        std::sscanf(challenge_hex + 2 * i, "%2hhx", &challenge[i]);
        std::sscanf(expected_hex + 2 * i, "%2hhx", &expected[i]);
    }
    jfg_cic6105_response(challenge, got);
    assert(std::memcmp(got, expected, 15) == 0);
}

int main() {
    check("000000000000000000000000000000", "bf9f9f9f9f9f9f9f9f9f9f9f9f9f9f");
    check("101112131415161718191a1b1c1d1e", "043cb9eb42f2f761663439e3c2fad9");
    check("0123456789abcdeffedcba98765432", "b48b98ab7e8338250a9b6019c47548");
    std::puts("cic6105_test: OK");
}

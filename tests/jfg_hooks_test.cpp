// Drives jfg_hooks.cpp with a fake RDRAM and records what it tells the runtime.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include "jfg_hooks.h"

static std::vector<std::string> calls;
extern "C" void load_overlays(uint32_t rom, int32_t ram, uint32_t size) {
    char b[64]; std::snprintf(b, sizeof b, "load %X %X %X", rom, (uint32_t)ram, size); calls.push_back(b);
}
extern "C" void unload_overlays(int32_t ram, uint32_t size) {
    char b[64]; std::snprintf(b, sizeof b, "unload %X %X", (uint32_t)ram, size); calls.push_back(b);
}

static std::vector<uint8_t> rdram(0x400000);
static std::vector<uint8_t> fake_rom(0x1000);
namespace recomp { std::span<const uint8_t> get_rom() { return fake_rom; } }
namespace ultramodern { uint32_t get_remaining_audio_bytes() { return 0x1234; } }
static void w(uint32_t vaddr, uint32_t v) { std::memcpy(&rdram[vaddr - 0x80000000u], &v, 4); }

int main() {
    const uint32_t table = 0x80200000;
    w(0x800FEAA0, table); w(0x800FEAAC, 3);
    w(table + 32 * 2 + 4, 0x1F00000); w(table + 32 * 2 + 8, 0x800); w(table + 32 * 2 + 12, 0x100);
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80300000, 0x800);          // load module 2
    jfg_on_dma(rdram.data(), 0x1F00000 + 0x900, 0x80310000, 0x40);   // its relocation table: ignored
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80340000, 0x800);          // reload_at_new_address
    jfg_on_module_unload(rdram.data(), 2);
    jfg_on_module_unload(rdram.data(), 2);                            // second unload: no-op
    std::vector<std::string> want = {"load 1F00000 80300000 900", "unload 80300000 900",
                                     "load 1F00000 80340000 900", "unload 80340000 900"};
    for (auto& c : calls) std::puts(c.c_str());
    assert(calls == want);
    w(0x80001000, 0x3C018041); w(0x80001004, 0x8C238000);             // lui at,0x8041 ; lw v1,-0x8000(at)
    assert(jfg_patched_hi_lo(rdram.data(), 0x80001000, 0x80001004) == 0x80408000);
    fake_rom[0x454] = 0x01; fake_rom[0x455] = 0x09; fake_rom[0x456] = 0x08; fake_rom[0x457] = 0x2B;
    assert(jfg_cart_word(0x10000454) == 0x0109082Bu);   // cartridge address -> ROM word, big-endian
    assert(jfg_cart_word(0xB0000454) == 0x0109082Bu);   // kseg1 form of the same address
    assert(jfg_cart_word(0x10000FFE) == 0);              // runs past the end of the ROM
    assert(jfg_ai_length() == 0x1234);
    std::puts("jfg_hooks_test: OK");
}

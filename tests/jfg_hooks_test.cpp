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
    // The lazy linker's load path copies code, data and the import table in one DMA (module 36 in the game).
    calls.clear();
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80350000, 0x800 + 0x100 + 0x40);
    assert(calls == std::vector<std::string>{"load 1F00000 80350000 900"});
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80360000, 0x400);  // shorter than the code: not a module load
    assert(calls.size() == 1);
    jfg_on_module_unload(rdram.data(), 2);
    // Empty descriptors (no code or data) share their ROM address with the next module: match the real one.
    calls.clear();
    w(0x800FEAAC, 5);
    w(table + 32 * 3 + 4, 0x1F10000); w(table + 32 * 3 + 8, 0); w(table + 32 * 3 + 12, 0);      // module 3: empty
    w(table + 32 * 4 + 4, 0x1F10000); w(table + 32 * 4 + 8, 0x200); w(table + 32 * 4 + 12, 0x20); // module 4
    jfg_on_dma(rdram.data(), 0x1F10000, 0x80400000, 0x220);
    assert(calls == std::vector<std::string>{"load 1F10000 80400000 220"});
    // Memory reused without an unload the hook saw: the old module there is gone, so forget it first.
    calls.clear();
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80400100, 0x900);   // module 2 over module 4's range
    assert((calls == std::vector<std::string>{"unload 80400000 220", "load 1F00000 80400100 900"}));
    jfg_on_module_unload(rdram.data(), 2);

    // The game's table holds each module's current address (entry +0; 0 once freed). Registrations follow it
    // once confirmed there, catching frees and moves the hooks don't see.
    w(0x800FEAAC, 3);
    calls.clear();
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80300000, 0x800);           // module 2 loaded; table not yet updated
    jfg_on_dma(rdram.data(), 0x1F00000 + 0x900, 0x80310000, 0x40);    // second DMA of the same load: keep it
    jfg_on_lazy_link(rdram.data(), 0x80001008);
    assert(calls == std::vector<std::string>{"load 1F00000 80300000 900"});
    w(table + 32 * 2, 0x80300000);                                     // game records the address: confirmed
    jfg_on_lazy_link(rdram.data(), 0x80001008);
    assert(calls.size() == 1);
    w(table + 32 * 2, 0x80380000);                                     // game moved it (copied, no DMA)
    jfg_on_lazy_link(rdram.data(), 0x80001008);
    assert((calls == std::vector<std::string>{"load 1F00000 80300000 900", "unload 80300000 900", "load 1F00000 80380000 900"}));
    w(table + 32 * 2, 0);                                              // game freed it without the unload hook
    jfg_on_dma(rdram.data(), 0x1E00000, 0x80390000, 0x10);             // any later event notices
    assert(calls.back() == "unload 80380000 900");
    calls.clear();
    jfg_on_module_unload(rdram.data(), 2);                             // already gone: no-op
    assert(calls.empty());

    w(0x80001000, 0x3C018041); w(0x80001004, 0x8C238000);             // lui at,0x8041 ; lw v1,-0x8000(at)
    assert(jfg_patched_hi_lo(rdram.data(), 0x80001000, 0x80001004) == 0x80408000);
    fake_rom[0x454] = 0x01; fake_rom[0x455] = 0x09; fake_rom[0x456] = 0x08; fake_rom[0x457] = 0x2B;
    assert(jfg_cart_word(0x10000454) == 0x0109082Bu);   // cartridge address -> ROM word, big-endian
    assert(jfg_cart_word(0xB0000454) == 0x0109082Bu);   // kseg1 form of the same address
    assert(jfg_cart_word(0x10000FFE) == 0);              // runs past the end of the ROM
    assert(jfg_ai_length() == 0x1234);
    std::puts("jfg_hooks_test: OK");
}

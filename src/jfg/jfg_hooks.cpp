// Keeps N64ModernRuntime's idea of where each JFG code module is loaded in sync with the game's own
// module system, which loads, moves and unloads modules at will.
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "jfg_hooks.h"
#include "librecomp/overlays.hpp"

namespace {
constexpr uint32_t desc_table_ptr = 0x800FEAA0;
constexpr uint32_t desc_count = 0x800FEAAC;

uint32_t read_word(uint8_t* rdram, uint32_t vaddr) {
    // RDRAM is stored as native-endian 32-bit words, so aligned words read directly.
    return *reinterpret_cast<uint32_t*>(rdram + (vaddr - 0x80000000u));
}

struct Loaded { uint32_t ram; uint32_t size; };
std::unordered_map<uint32_t, Loaded> loaded_modules;
const bool debug = std::getenv("JFG_DEBUG_MODULES") != nullptr;
}

extern "C" void jfg_on_dma(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size) {
    uint32_t table = read_word(rdram, desc_table_ptr);
    if (table == 0) {
        return; // module system not initialised yet
    }
    uint32_t count = read_word(rdram, desc_count);
    for (uint32_t i = 1; i < count; i++) {
        uint32_t entry = table + i * 32;
        if (read_word(rdram, entry + 4) != rom || read_word(rdram, entry + 8) != size) {
            continue;
        }
        uint32_t image = read_word(rdram, entry + 8) + read_word(rdram, entry + 12);
        if (auto it = loaded_modules.find(i); it != loaded_modules.end()) {
            unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
            loaded_modules.erase(it);
        }
        load_overlays(rom, static_cast<int32_t>(ram), image);
        loaded_modules[i] = {ram, image};
        if (debug) std::fprintf(stderr, "[jfg] module %u loaded at %08X (%u bytes)\n", i, ram, image);
        return;
    }
}

extern "C" void jfg_on_module_unload(uint8_t*, uint32_t index) {
    auto it = loaded_modules.find(index);
    if (it == loaded_modules.end()) {
        return;
    }
    unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
    if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X\n", index, it->second.ram);
    loaded_modules.erase(it);
}

extern "C" void jfg_on_lazy_link(uint8_t*, uint32_t ra) {
    if (debug) std::fprintf(stderr, "[jfg] lazy link for call at %08X\n", ra - 8);
}

extern "C" uint32_t jfg_patched_hi_lo(uint8_t* rdram, uint32_t hi_site, uint32_t lo_site) {
    uint32_t hi = read_word(rdram, hi_site) & 0xFFFF;
    uint32_t lo = read_word(rdram, lo_site) & 0xFFFF;
    return (hi << 16) + static_cast<int16_t>(lo);
}

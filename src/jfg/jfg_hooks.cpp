// Keeps N64ModernRuntime's idea of where each JFG code module is loaded in sync with the game's own
// module system, which loads, moves and unloads modules at will.
#include <cstdio>
#include <span>
#include <cstdlib>
#include <unordered_map>

#include "jfg_hooks.h"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"

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
    if (debug) std::fprintf(stderr, "[jfg] dma rom %08X -> %08X size %X (module table %08X)\n", rom, ram, size, table);
    if (table == 0) {
        return; // module system not initialised yet
    }
    uint32_t count = read_word(rdram, desc_count);
    for (uint32_t i = 1; i < count; i++) {
        uint32_t entry = table + i * 32;
        // A DMA from the module's first byte loads its code. Load paths differ in how much else they copy
        // along with it (code only; code + data + import table), but always at least the code. Empty
        // descriptors share their ROM address with the next module, so they never match.
        uint32_t text = read_word(rdram, entry + 8);
        if (text == 0 || read_word(rdram, entry + 4) != rom || size < text) {
            continue;
        }
        uint32_t image = text + read_word(rdram, entry + 12);
        // Whatever was registered where this module now sits is gone (the module itself included, if it
        // moved), even when the game freed it by a path the unload hook doesn't see.
        for (auto it = loaded_modules.begin(); it != loaded_modules.end();) {
            bool overlaps = it->second.ram < ram + image && ram < it->second.ram + it->second.size;
            if (it->first == i || overlaps) {
                unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
                if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X (replaced)\n", it->first, it->second.ram);
                it = loaded_modules.erase(it);
            }
            else {
                ++it;
            }
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

extern "C" uint32_t jfg_cart_word(uint32_t cart_address) {
    uint32_t offset = (cart_address & 0x1FFFFFFF) - 0x10000000u;
    std::span<const uint8_t> rom = recomp::get_rom();
    if (offset > rom.size() || rom.size() - offset < 4) {
        return 0;
    }
    return (uint32_t{rom[offset]} << 24) | (uint32_t{rom[offset + 1]} << 16) |
           (uint32_t{rom[offset + 2]} << 8) | uint32_t{rom[offset + 3]};
}

extern "C" uint32_t jfg_ai_length(void) {
    return ultramodern::get_remaining_audio_bytes();
}

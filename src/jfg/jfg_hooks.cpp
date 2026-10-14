// Keeps N64ModernRuntime's idea of where each JFG code module is loaded in sync with the game's own
// module system, which loads, moves and unloads modules at will.
#include <cstdio>
#include <span>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

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

// confirmed: the game's table has recorded this address for the module (entry +0), so from then on the table
// is authoritative for it. (Some loads take two DMAs and record the address only afterwards.)
struct Loaded { uint32_t ram; uint32_t size; bool confirmed; };
std::unordered_map<uint32_t, Loaded> loaded_modules;
const bool debug = std::getenv("JFG_DEBUG_MODULES") != nullptr;

// Registers module `index` at `ram`, dropping whatever was registered there before (the module itself included,
// if it moved), even when the game freed it by a path the unload hook doesn't see.
void register_module(uint32_t index, uint32_t rom, uint32_t ram, uint32_t size, bool confirmed) {
    for (auto it = loaded_modules.begin(); it != loaded_modules.end();) {
        bool overlaps = it->second.ram < ram + size && ram < it->second.ram + it->second.size;
        if (it->first == index || overlaps) {
            unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
            if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X (replaced)\n", it->first, it->second.ram);
            it = loaded_modules.erase(it);
        }
        else {
            ++it;
        }
    }
    load_overlays(rom, static_cast<int32_t>(ram), size);
    loaded_modules[index] = {ram, size, confirmed};
}

// Brings registrations in line with the game's module table: modules it has freed or moved without a DMA
// (memory reorganisation) are unregistered or re-registered where they now are.
void sync_with_table(uint8_t* rdram) {
    uint32_t table = read_word(rdram, desc_table_ptr);
    if (table == 0) {
        return;
    }
    std::vector<std::pair<uint32_t, uint32_t>> changed; // module, new address (0 = freed)
    for (auto& [index, loaded] : loaded_modules) {
        uint32_t current = read_word(rdram, table + index * 32);
        if (current == loaded.ram) {
            loaded.confirmed = true;
        }
        else if (loaded.confirmed) {
            changed.emplace_back(index, current);
        }
    }
    for (auto [index, current] : changed) {
        Loaded old = loaded_modules[index];
        if (current == 0) {
            unload_overlays(static_cast<int32_t>(old.ram), old.size);
            loaded_modules.erase(index);
            if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X (freed)\n", index, old.ram);
        }
        else {
            register_module(index, read_word(rdram, table + index * 32 + 4), current, old.size, true);
            if (debug) std::fprintf(stderr, "[jfg] module %u moved from %08X to %08X\n", index, old.ram, current);
        }
    }
}
}

extern "C" void jfg_on_dma(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size) {
    uint32_t table = read_word(rdram, desc_table_ptr);
    if (debug) std::fprintf(stderr, "[jfg] dma rom %08X -> %08X size %X (module table %08X)\n", rom, ram, size, table);
    if (table == 0) {
        return; // module system not initialised yet
    }
    sync_with_table(rdram);
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
        register_module(i, rom, ram, image, false);
        if (debug) {
            std::fprintf(stderr, "[jfg] module %u loaded at %08X (%u bytes); entry", i, ram, image);
            for (uint32_t k = 0; k < 32; k += 4) std::fprintf(stderr, " %08X", read_word(rdram, entry + k));
            std::fprintf(stderr, "\n");
        }
        return;
    }
}

extern "C" void jfg_on_module_unload(uint8_t* rdram, uint32_t index) {
    sync_with_table(rdram);
    auto it = loaded_modules.find(index);
    if (it == loaded_modules.end()) {
        return;
    }
    unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
    if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X\n", index, it->second.ram);
    loaded_modules.erase(it);
}

extern "C" void jfg_on_lazy_link(uint8_t* rdram, uint32_t ra) {
    if (debug) std::fprintf(stderr, "[jfg] lazy link for call at %08X\n", ra - 8);
    sync_with_table(rdram);
    // JFG_DEBUG_REGISTRY=1: check every registered module's code in RAM still matches its ROM code. Relocated
    // words differ (a minority); a stale registration (another module now in that memory) differs almost entirely.
    static const bool check = std::getenv("JFG_DEBUG_REGISTRY") != nullptr;
    if (!check) return;
    uint32_t table = read_word(rdram, desc_table_ptr);
    std::span<const uint8_t> rom = recomp::get_rom();
    for (const auto& [index, loaded] : loaded_modules) {
        uint32_t entry = table + index * 32;
        uint32_t rom_addr = read_word(rdram, entry + 4), text = read_word(rdram, entry + 8);
        uint32_t words = text / 4, differ = 0;
        for (uint32_t w = 0; w < words; w++) {
            uint32_t o = rom_addr + 4 * w;
            uint32_t expected = (uint32_t{rom[o]} << 24) | (uint32_t{rom[o + 1]} << 16) | (uint32_t{rom[o + 2]} << 8) | rom[o + 3];
            if (read_word(rdram, loaded.ram + 4 * w) != expected) differ++;
        }
        if (words && differ * 2 > words) {
            std::fprintf(stderr, "[jfg] REGISTRY: module %u registered at %08X but %u of %u code words differ from ROM; entry",
                         index, loaded.ram, differ, words);
            for (uint32_t k = 0; k < 32; k += 4) std::fprintf(stderr, " %08X", read_word(rdram, entry + k));
            std::fprintf(stderr, "\n");
        }
    }
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

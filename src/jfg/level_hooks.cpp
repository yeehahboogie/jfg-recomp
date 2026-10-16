// Level survey aids: log level changes, warp to a level (docs/superpowers/plans/2026-10-15-jfg-whole-game.md).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>

#include <mutex>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "jfg_level_hooks.h"
#include "jfg_level_tools.h"
#include "jfg_player_tools.h"
#include "jfg_tools.h"

extern "C" void func_80047C1C(uint8_t* rdram, recomp_context* ctx); // the player's stats (character, player number)
extern "C" void func_8004665C(uint8_t* rdram, recomp_context* ctx); // mainChangeLevel

namespace {
const char* const debug_env = std::getenv("JFG_DEBUG_LEVELS");
const bool debug = debug_env != nullptr;
const bool dump_names = debug && std::strcmp(debug_env, "names") == 0;
// JFG_TAKE_EXIT=<n>: once the level has run for a few seconds, list its exits and hold the player on exit n, so the
// game's own exit logic (exitControl 0x8002561C) changes level. Exits are objects with control type 8.
const int take_exit = []() {
    const char* v = std::getenv("JFG_TAKE_EXIT");
    return (v != nullptr && *v != '\0') ? std::stoi(v) : -1; // empty = unset
}();
constexpr gpr obj_list = (gpr)(int32_t)0x800F2CA4;  // ObjList: pointer to an array of object pointers
constexpr gpr obj_count = (gpr)(int32_t)0x800F2CA8;
constexpr int16_t exit_control_type = 8;
const std::optional<jfg::WarpSpec> warp = []() -> std::optional<jfg::WarpSpec> {
    const char* v = std::getenv("JFG_WARP");
    if (v == nullptr) return std::nullopt;
    return jfg::parse_warp(v); // throws on a malformed value: fail loudly
}();

// Calls a recompiled game function from inside a hook: arguments in a0..a3 then the stack (sp+0x10...), on a scratch
// frame below the current stack. Every register is restored afterwards, so the hooked function continues untouched.
uint32_t call_game(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*),
                   std::initializer_list<uint32_t> args) {
    const recomp_context saved = *ctx;
    const gpr frame = ctx->r29 - 0x100;
    gpr* regs[4] = {&ctx->r4, &ctx->r5, &ctx->r6, &ctx->r7};
    int i = 0;
    for (uint32_t a : args) {
        if (i < 4) *regs[i] = (gpr)(int32_t)a;
        else MEM_W(0x10 + 4 * (i - 4), frame) = (int32_t)a;
        i++;
    }
    ctx->r29 = frame;
    fn(rdram, ctx);
    const uint32_t result = (uint32_t)ctx->r2;
    *ctx = saved;
    return result;
}
constexpr gpr current_level = (gpr)(int32_t)0x800FB114; // levelGetNumber()
constexpr gpr level_count = (gpr)(int32_t)0x800FB124;
}

extern "C" void jfg_level_change(uint8_t* rdram, recomp_context* ctx) {
    // JFG_WARP: a new game asks for the story intro (level 322, "JFG Ship") right after name entry; ask for the
    // target level instead, with the arguments the game's own gameplay transitions use (entry, 0, 16).
    constexpr int32_t new_game_intro = 322;
    static bool redirected = false;
    if (warp && !redirected && (int32_t)ctx->r4 == new_game_intro) {
        redirected = true;
        std::fprintf(stderr, "[levels] warp: new game goes to level %d entry %d\n", warp->level, warp->entry);
        ctx->r4 = warp->level;
        ctx->r5 = warp->entry;
        ctx->r6 = 0;
        ctx->r7 = 16;
    }
    if (!debug) return;
    const gpr sp = ctx->r29;
    static const auto t0 = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[levels] t=%.1f mainChangeLevel(level=%d (0x%X), %d, %d, %d, [sp+10]=0x%X, [sp+14]=0x%X) from level %d of %d\n",
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), (int32_t)ctx->r4, (uint32_t)ctx->r4, (int32_t)ctx->r5, (int32_t)ctx->r6, (int32_t)ctx->r7,
                 (uint32_t)MEM_W(0x10, sp), (uint32_t)MEM_W(0x14, sp), MEM_W(0, current_level), MEM_W(0, level_count));
}

namespace {
float read_f32(uint8_t* rdram, gpr base, int32_t offset) {
    const uint32_t bits = (uint32_t)MEM_W(offset, base);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void write_f32(uint8_t* rdram, gpr base, int32_t offset, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    MEM_W(offset, base) = (int32_t)bits;
}

// JFG_TAKE_EXIT, run each frame for player 1 (obj = the player's object).
void exit_test_tick(uint8_t* rdram, gpr player) {
    static int frames = 0;
    static gpr target = 0;
    static int32_t level = -1;   // the level the exit belongs to
    static bool fired = false;
    constexpr int settle_frames = 90; // let the level start (about 3 s)
    if (take_exit < 0 || ++frames < settle_frames) return;
    if (frames == settle_frames) {
        const gpr list = (gpr)(int32_t)MEM_W(0, obj_list);
        const int32_t count = MEM_W(0, obj_count);
        int found = 0;
        for (int32_t i = 0; i < count; i++) {
            const gpr obj = (gpr)(int32_t)MEM_W(4 * i, list);
            if (obj == 0 || (int16_t)MEM_H(0x48, obj) != exit_control_type) continue;
            const gpr data = (gpr)(int32_t)MEM_W(0x68, obj);
            std::fprintf(stderr, "[levels] exit %d: object %d at (%.1f, %.1f, %.1f) radius %d kind %d in level %d\n", found, i,
                         read_f32(rdram, obj, 0x0C), read_f32(rdram, obj, 0x10), read_f32(rdram, obj, 0x14),
                         data ? MEM_W(0x10, data) : -1, data ? (int16_t)MEM_H(0x14, data) : -1, MEM_W(0, current_level));
            if (found == take_exit) {
                target = obj;
                level = MEM_W(0, current_level);
            }
            found++;
        }
        std::fprintf(stderr, "[levels] %d exits; taking exit %d%s\n", found, take_exit, target ? "" : " (none)");
    }
    // Once: the exit object belongs to its level, so never touch it after a level change.
    if (target == 0 || fired || MEM_W(0, current_level) != level) return;
    // Do what exitControl (0x8002561C) does for a player inside the exit's radius: point the player at the exit and
    // start its countdown; the player's own code then performs the transition. (Moving the player there isn't enough:
    // objects outside the player's current region aren't updated.)
    const gpr pd = (gpr)(int32_t)MEM_W(0x68, player);
    MEM_W(0xF4, pd) = (int32_t)(uint32_t)target;
    MEM_B(0x196, pd) = -120;
    fired = true;
    std::fprintf(stderr, "[levels] exit %d triggered in level %d\n", take_exit, level);
}
}

// JFG_DUMP_OBJECTS=<file>: every 15 frames, append every object to <file> for diffing (tools/diag/objdiff.py). Record:
// "JFGO" frame level count, then per object: index, object address, 0x80 bytes of the object, data address
// (object +0x68) and 0x100 bytes of its data block (zeros if none). Words are big-endian, as in N64 memory.
static void dump_objects_tick(uint8_t* rdram) {
    static FILE* out = []() -> FILE* {
        const char* path = std::getenv("JFG_DUMP_OBJECTS");
        return path != nullptr ? std::fopen(path, "wb") : nullptr;
    }();
    static uint32_t frame = 0;
    if (out == nullptr || frame++ % 15 != 0) return;
    auto put32 = [](uint32_t v) { const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
                                  std::fwrite(b, 1, 4, out); };
    const gpr list = (gpr)(int32_t)MEM_W(0, obj_list);
    const int32_t count = MEM_W(0, obj_count);
    std::fwrite("JFGO", 1, 4, out);
    put32(frame - 1); put32((uint32_t)MEM_W(0, current_level)); put32((uint32_t)count);
    uint8_t bytes[0x100];
    for (int32_t i = 0; i < count; i++) {
        const gpr obj = (gpr)(int32_t)MEM_W(4 * i, list);
        put32((uint32_t)i); put32((uint32_t)obj);
        for (int k = 0; k < 0x80; k++) bytes[k] = obj ? MEM_BU(k, obj) : 0;
        std::fwrite(bytes, 1, 0x80, out);
        const gpr data = obj ? (gpr)(int32_t)MEM_W(0x68, obj) : 0;
        const bool valid = (uint32_t)data >= 0x80000000u && (uint32_t)data < 0x80800000u;
        put32(valid ? (uint32_t)data : 0);
        for (int k = 0; k < 0x100; k++) bytes[k] = valid ? MEM_BU(k, data) : 0;
        std::fwrite(bytes, 1, 0x100, out);
    }
    std::fflush(out);
}

// Player tools: cheats, level skip and the data the stats overlay shows (jfg_player_tools.h).
namespace {
constexpr gpr all_level_cheat = (gpr)(int32_t)0x800A5068;
constexpr gpr invulnerability_cheat = (gpr)(int32_t)0x800A5070;
constexpr gpr all_weapons_cheat = (gpr)(int32_t)0x800A5078;
jfg::Mailbox refill_box, warp_box, exit_box;
std::mutex info_mutex;
jfg::tools::GameInfo info;            // guarded by info_mutex
std::vector<std::string> names;       // guarded by info_mutex
std::atomic<int64_t> last_gameplay_ns{0};
std::atomic<uint64_t> frame_count{0};

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Development aid: JFG_CHEATS=invuln,weapons[,levels][,ammo][,bighead] switches cheats on from the start (scripted runs).
jfg::tools::Cheats& cheat_state() {
    static jfg::tools::Cheats state;
    static bool initialised = false;
    if (!initialised) {
        initialised = true;
        const char* v = std::getenv("JFG_CHEATS");
        const std::string list = v ? v : "";
        state.invulnerable = list.find("invuln") != std::string::npos;
        state.all_weapons = list.find("weapons") != std::string::npos;
        state.all_levels = list.find("levels") != std::string::npos;
        state.infinite_ammo = list.find("ammo") != std::string::npos;
        state.big_head = list.find("bighead") != std::string::npos;
    }
    return state;
}

// A cheat flag follows its switch: written while on, cleared once when switched off. The flags are bytes: the game
// reads them with lbu (the characters' control code for invulnerability and weapons, frontMap for levels).
void apply_flag(uint8_t* rdram, gpr flag, bool on, bool& was_on) {
    if (on) MEM_B(0, flag) = 1;
    else if (was_on) MEM_B(0, flag) = 0;
    was_on = on;
}

// The current level's exits (objects with control type 8), in object-list order.
std::vector<gpr> level_exits(uint8_t* rdram) {
    std::vector<gpr> exits;
    const gpr list = (gpr)(int32_t)MEM_W(0, obj_list);
    const int32_t count = MEM_W(0, obj_count);
    for (int32_t i = 0; i < count; i++) {
        const gpr obj = (gpr)(int32_t)MEM_W(4 * i, list);
        if (obj != 0 && (int16_t)MEM_H(0x48, obj) == exit_control_type) exits.push_back(obj);
    }
    return exits;
}

// Every level's name, read straight from the ROM: the game's levelGetName allocates and frees memory as it goes,
// which during gameplay broke the next level load (a division by zero in trackInit). The asset table (pointer at
// 0x800FE070) holds each asset's offset at [id + 1], from ROM 0xB1880; asset 0x1E is the per-level offset of each
// name in asset 0x1F, and a name is 32 bytes.
std::vector<std::string> read_level_names(uint8_t* rdram, int32_t count) {
    const std::span<const uint8_t> rom = recomp::get_rom();
    const gpr table = (gpr)(int32_t)MEM_W(0, (gpr)(int32_t)0x800FE070);
    auto asset_rom = [&](int id) { return 0xB1880u + (uint32_t)MEM_W(4 * (id + 1), table); };
    auto be32 = [&](uint32_t a) {
        return a + 4 <= rom.size() ? (uint32_t(rom[a]) << 24) | (uint32_t(rom[a + 1]) << 16) | (uint32_t(rom[a + 2]) << 8) | rom[a + 3] : 0u;
    };
    const uint32_t offsets = asset_rom(0x1E), texts = asset_rom(0x1F);
    std::vector<std::string> result;
    for (int32_t n = 0; n < count; n++) {
        const uint32_t at = texts + be32(offsets + 4 * n);
        std::string name;
        for (uint32_t k = 0; k < 32 && at + k < rom.size() && rom[at + k] != 0; k++) name.push_back((char)rom[at + k]);
        result.push_back(name);
    }
    return result;
}

void player_tools_tick(uint8_t* rdram, recomp_context* ctx) {
    const gpr player = ctx->r4;
    const gpr pd = (gpr)(int32_t)MEM_W(0x68, player);
    last_gameplay_ns = now_ns();
    jfg::tools::Cheats& cheats = cheat_state();

    static bool invulnerable_was = false, weapons_was = false, levels_was = false;
    apply_flag(rdram, invulnerability_cheat, cheats.invulnerable, invulnerable_was);
    apply_flag(rdram, all_weapons_cheat, cheats.all_weapons, weapons_was);
    apply_flag(rdram, all_level_cheat, cheats.all_levels, levels_was);

    // The player's stats: owned weapons (bitmask at +0x0A), ammo per weapon (+0x14 + 2w) and its cap (+0x34 + 2w),
    // health segments (+0x02), as ammoPickupControl and healthPickupControl use them.
    auto stats = [&]() {
        return (gpr)(int32_t)call_game(rdram, ctx, func_80047C1C, {(uint32_t)(int32_t)MEM_B(0, pd), (uint32_t)(int32_t)MEM_B(1, pd)});
    };
    if (cheats.infinite_ammo) {
        const gpr s = stats();
        const uint16_t owned = MEM_HU(0x0A, s);
        for (int w = 0; w < 16; w++) {
            if (owned & (1u << w)) MEM_H(0x14 + 2 * w, s) = MEM_H(0x34 + 2 * w, s);
        }
    }
    if (refill_box.take()) {
        // Health is the halfword at +6 of the struct at player +0x4C; full is ((segments * 5) + 4) << 8, as a full
        // health pickup sets it.
        const gpr health = (gpr)(int32_t)MEM_W(0x4C, player);
        if (health != 0) {
            MEM_H(6, health) = (int16_t)(((MEM_H(2, stats()) * 5) + 4) << 8);
            std::fprintf(stderr, "[tools] health refilled\n");
        }
    }
    if (auto level = warp_box.take()) {
        // The arguments the game's own exits use: entry 0, 0, 16, [sp+10] 1, [sp+14] 1.
        std::fprintf(stderr, "[tools] warp to level %d\n", *level);
        call_game(rdram, ctx, func_8004665C, {(uint32_t)*level, 0, 0, 16, 1, 1});
    }
    if (auto index = exit_box.take()) {
        // As exitControl does for a player in range: point the player at the exit and start its countdown; the
        // player's own code then changes level (objects outside the player's region aren't updated, so moving the
        // player there isn't enough).
        const std::vector<gpr> exits = level_exits(rdram);
        if (*index < (int)exits.size()) {
            MEM_W(0xF4, pd) = (int32_t)(uint32_t)exits[*index];
            MEM_B(0x196, pd) = -120;
            std::fprintf(stderr, "[tools] taking exit %d of level %d\n", *index, MEM_W(0, current_level));
        }
    }

    static uint32_t frame = 0;
    if (frame++ % 10 == 0) {
        const float x = read_f32(rdram, player, 0x0C), y = read_f32(rdram, player, 0x10), z = read_f32(rdram, player, 0x14);
        std::vector<float> distances;
        for (gpr exit : level_exits(rdram)) {
            const float dx = read_f32(rdram, exit, 0x0C) - x, dy = read_f32(rdram, exit, 0x10) - y, dz = read_f32(rdram, exit, 0x14) - z;
            distances.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        std::lock_guard lock(info_mutex);
        info.level = MEM_W(0, current_level);
        info.x = x; info.y = y; info.z = z;
        info.exit_distances = std::move(distances);
        if (names.empty()) {
            names = read_level_names(rdram, MEM_W(0, level_count));
        }
    }
}
}

namespace jfg::tools {
Cheats& cheats() { return cheat_state(); }
void request_refill_health() { refill_box.post(1); }
void request_warp(int level) { warp_box.post(level); }
void request_exit(int index) { exit_box.post(index); }
GameInfo game_info() {
    std::lock_guard lock(info_mutex);
    GameInfo copy = info;
    copy.in_gameplay = now_ns() - last_gameplay_ns.load() < 500'000'000;
    return copy;
}
std::vector<std::string> level_names() {
    std::lock_guard lock(info_mutex);
    return names;
}
void count_game_frame() { frame_count++; }
uint64_t game_frames() { return frame_count.load(); }
}

// Called each frame for player 1 from the controlPlayer hook (src/jfg/controls/hooks.cpp), at controlPlayer's entry
// (a0 = the player's object).
void jfg_level_tick(uint8_t* rdram, recomp_context* ctx) {
    exit_test_tick(rdram, ctx->r4);
    player_tools_tick(rdram, ctx);
    dump_objects_tick(rdram);
    static bool started = false; // first gameplay frame seen
    if (!started) {
        started = true;
        if (dump_names) {
            const std::vector<std::string> all = read_level_names(rdram, MEM_W(0, level_count));
            for (size_t n = 0; n < all.size(); n++) {
                std::fprintf(stderr, "[levels] name %zu (0x%zX): %s\n", n, n, all[n].c_str());
            }
        }
    }
    if (!debug) return;
    static int32_t last = -1;
    const int32_t level = MEM_W(0, current_level);
    if (level != last) {
        last = level;
        std::fprintf(stderr, "[levels] playing level %d (0x%X) of %d\n", level, level, MEM_W(0, level_count));
    }
}

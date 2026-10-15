// Level survey aids: log level changes, warp to a level (docs/superpowers/plans/2026-10-15-jfg-whole-game.md).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>

#include "recomp.h"
#include "jfg_level_hooks.h"
#include "jfg_level_tools.h"

extern "C" void func_8004471C(uint8_t* rdram, recomp_context* ctx); // levelGetName

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

// JFG_CHEATS=invuln,weapons[,levels]: switch on the game's own cheat flags every frame (invulnerabilityCheat,
// allweaponsCheat, allLevelCheat), so scripted runs survive and have every weapon.
static void cheats_tick(uint8_t* rdram) {
    static const std::string cheats = []() { const char* v = std::getenv("JFG_CHEATS"); return std::string(v ? v : ""); }();
    if (cheats.empty()) return;
    if (cheats.find("invuln") != std::string::npos) MEM_W(0, (gpr)(int32_t)0x800A5070) = 1;
    if (cheats.find("weapons") != std::string::npos) MEM_W(0, (gpr)(int32_t)0x800A5078) = 1;
    if (cheats.find("levels") != std::string::npos) MEM_W(0, (gpr)(int32_t)0x800A5068) = 1;
}

// Called each frame for player 1 from the controlPlayer hook (src/jfg/controls/hooks.cpp), at controlPlayer's entry
// (a0 = the player's object).
void jfg_level_tick(uint8_t* rdram, recomp_context* ctx) {
    exit_test_tick(rdram, ctx->r4);
    cheats_tick(rdram);
    dump_objects_tick(rdram);
    static bool started = false; // first gameplay frame seen
    if (!started) {
        started = true;
        if (dump_names) {
            const int32_t count = MEM_W(0, level_count);
            for (int32_t n = 0; n < count; n++) {
                const gpr name = (gpr)(int32_t)call_game(rdram, ctx, func_8004471C, {(uint32_t)n});
                char text[33] = {};
                for (int k = 0; k < 32; k++) text[k] = (char)MEM_BU(k, name);
                std::fprintf(stderr, "[levels] name %d (0x%X): %s\n", n, n, text);
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

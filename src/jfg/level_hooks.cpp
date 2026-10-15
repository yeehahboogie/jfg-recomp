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

// Called each frame for player 1 from the controlPlayer hook (src/jfg/controls/hooks.cpp).
void jfg_level_tick(uint8_t* rdram, recomp_context* ctx) {
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

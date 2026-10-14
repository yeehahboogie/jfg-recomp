// Hooks that connect the modern controls to the recompiled game. Addresses: docs/superpowers/specs/2026-10-14-jfg-controls-research.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"
#include "jfg_controls_hooks.h"
#include "gate.h"
#include "settings.h"

namespace {
const bool debug = std::getenv("JFG_DEBUG_CONTROLS") != nullptr;
constexpr gpr disablejoy = (gpr)(int32_t)0x800F6DBC; // tested first by controlReadJoypad (0x80037250)
constexpr gpr cam0 = (gpr)(int32_t)0x800FA4D0;       // camGetListPtr() (0x80041E0C); entry 0 is player 1's camera
constexpr gpr control_mode_keys = (gpr)(int32_t)0x800A18B0; // controlModeKeys: pointer to the active key map
constexpr uint32_t normal_keys = 0x800A18B4;                  // Normal key map table (Expert is 0x800A18D8)
uint32_t frame_no = 0;
uint32_t camera_pd = 0; // dispatcher's a1, captured at entry (it reuses s0 before returning)

bool scheme_modern() { return jfg::controls::settings().modern; }

gpr kseg0(uint32_t addr) { return (gpr)(int32_t)addr; }

float read_f32(uint8_t* rdram, gpr base, int32_t offset) {
    const uint32_t bits = (uint32_t)MEM_W(offset, base);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
}

extern "C" void jfg_controls_latch(uint8_t*, recomp_context*) {}

extern "C" void jfg_controls_begin_player(uint8_t* rdram, recomp_context* ctx) {
    // controlPlayer(obj): pd = *(obj + 0x68)
    const gpr obj = ctx->r4;
    const uint32_t pd = (uint32_t)MEM_W(0x68, obj);
    const jfg::controls::PlayerView v{MEM_B(0x000, kseg0(pd)), MEM_B(0x001, kseg0(pd)) & 3, MEM_BU(0x568, kseg0(pd)),
                                      MEM_W(0, disablejoy) != 0};
    if (v.index == 0) {
        jfg::controls::g_frame = {jfg::controls::modern_applies(v, scheme_modern()), pd, (uint32_t)obj, 0};
    }
}

extern "C" void jfg_controls_force_normal_keymap(uint8_t* rdram, recomp_context*) {
    // The modern translation (src/jfg/controls/translate.cpp) produces Normal-map buttons.
    if (jfg::controls::g_frame.active) {
        MEM_W(0, control_mode_keys) = (int32_t)normal_keys;
    }
}

extern "C" void jfg_controls_camera_begin(uint8_t*, recomp_context* ctx) { camera_pd = (uint32_t)ctx->r5; }

extern "C" void jfg_controls_after_camera(uint8_t* rdram, recomp_context*) {
    auto& f = jfg::controls::g_frame;
    const uint32_t pd = camera_pd;
    if (!debug || f.pd == 0 || pd != f.pd) return;
    const gpr p = kseg0(pd), o = kseg0(f.obj);
    std::fprintf(stderr,
                 "[controls] f=%u active=%d cam=%d idx=%d chr=%d state=%u facing=%04X orbit=%04X pan=%04X follow=%u "
                 "camyaw=%04X campitch=%04X campos=(%.1f,%.1f,%.1f) pos=(%.1f,%.1f,%.1f) objyaw=%04X\n",
                 frame_no++, f.active, f.camera_kind, MEM_B(0, p), MEM_B(1, p) & 3, MEM_BU(0x568, p),
                 (uint16_t)MEM_H(0x11C, p), (uint16_t)MEM_H(0x104, p), (uint16_t)MEM_H(0x10A, p), MEM_BU(0x10C, p),
                 (uint16_t)MEM_H(0x00, cam0), (uint16_t)MEM_H(0x02, cam0), read_f32(rdram, cam0, 0x0C), read_f32(rdram, cam0, 0x10),
                 read_f32(rdram, cam0, 0x14), read_f32(rdram, o, 0x0C), read_f32(rdram, o, 0x10), read_f32(rdram, o, 0x14), (uint16_t)MEM_H(0x00, o));
}

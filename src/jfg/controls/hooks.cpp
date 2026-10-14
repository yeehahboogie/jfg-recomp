// Hooks that connect the modern controls to the recompiled game. Addresses: docs/superpowers/specs/2026-10-14-jfg-controls-research.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "recomp.h"
#include "jfg_controls_hooks.h"
#include "gate.h"
#include "settings.h"
#include "input_state.h"
#include "free_camera.h"
#include "aim.h"
#include "aim_move.h"
#include "recomp_input.h"

namespace {
const bool debug = std::getenv("JFG_DEBUG_CONTROLS") != nullptr;
constexpr gpr disablejoy = (gpr)(int32_t)0x800F6DBC; // tested first by controlReadJoypad (0x80037250)
constexpr gpr cam0 = (gpr)(int32_t)0x800FA4D0;       // camGetListPtr() (0x80041E0C); entry 0 is player 1's camera
constexpr gpr control_mode_keys = (gpr)(int32_t)0x800A18B0; // controlModeKeys: pointer to the active key map
constexpr uint32_t normal_keys = 0x800A18B4;                  // Normal key map table (Expert is 0x800A18D8)
uint32_t frame_no = 0;
uint32_t camera_pd = 0;     // dispatcher's a1, captured at entry (it reuses s0 before returning)
float camera_ticks = 2.f;   // dispatcher's a2: 60 Hz ticks this frame
uint32_t game_frame = 0;    // counts joyRead calls (one per game frame)

// Free camera (docs/superpowers/specs/2026-10-14-jfg-modern-controls-design.md §4.2).
constexpr float mouse_units_per_sensitivity = 0.36f; // orbit units per mouse count per sensitivity point (50 -> ~0.1 deg)
constexpr float look_target_min = 10.f, look_target_max = 120.f; // plausible look-at heights above Juno's origin
jfg::controls::FreeCameraState free_cam;
uint32_t last_follow_frame = 0xFFFFFFFF; // game_frame of the last follow-camera step
struct {
    bool valid = false;
    jfg::controls::Vec3 orig, written;
    int16_t orig_pitch = 0, written_pitch = 0;
} pitch_applied;

// Aim controller (spec §4.3): native controlGetManualAim in Modern.
constexpr gpr control_keys = (gpr)(int32_t)0x800F6DA0;    // controlKeys; controlDkeys (pressed) +4, released +8
constexpr uint16_t c_up = 0x0008, c_down = 0x0004;         // next / previous weapon in Modern; zoom with the zoom weapon
constexpr gpr rim_rate_table = (gpr)(int32_t)0x800A2E60;   // 20 floats
constexpr gpr rim_rate_smooth = (gpr)(int32_t)0x800ABFCC; // lui 0x800B, -0x4034
constexpr gpr zoom_in_vel = (gpr)(int32_t)0x800ABFC4, zoom_out_vel = (gpr)(int32_t)0x800ABFC8;
jfg::controls::AimState aim_state;   // offsets persist across calls in mouse mode
bool aim_mouse_mode = false;
uint32_t last_aim_frame = 0xFFFFFFFF;
int16_t last_yaw_off = 0, last_pitch_off = 0; // for the debug log
float aim_ticks = 2.f;                         // this frame's ticks, from controlGetManualAim

bool scheme_modern() { return jfg::controls::settings().modern; }

gpr kseg0(uint32_t addr) { return (gpr)(int32_t)addr; }

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

jfg::controls::Vec3 read_vec3(uint8_t* rdram, gpr base, int32_t offset) {
    return {read_f32(rdram, base, offset), read_f32(rdram, base, offset + 4), read_f32(rdram, base, offset + 8)};
}

void write_vec3(uint8_t* rdram, gpr base, int32_t offset, jfg::controls::Vec3 v) {
    write_f32(rdram, base, offset, v.x);
    write_f32(rdram, base, offset + 4, v.y);
    write_f32(rdram, base, offset + 8, v.z);
}

bool same(jfg::controls::Vec3 a, jfg::controls::Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

jfg::controls::LookParams look_params() {
    const auto& st = jfg::controls::settings();
    return {recomp::get_mouse_sensitivity() * mouse_units_per_sensitivity, st.stick_yaw_dps * 65536.f / 360.f,
            st.stick_pitch_dps / 90.f, st.invert_mouse_y, st.invert_stick_y};
}

bool is_player0(uint32_t pd) { return jfg::controls::g_frame.active && pd == jfg::controls::g_frame.pd; }
}

extern "C" void jfg_controls_latch(uint8_t*, recomp_context*) {
    jfg::controls::latch();
    game_frame++;
}

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

extern "C" void jfg_controls_camera_begin(uint8_t*, recomp_context* ctx) {
    camera_pd = (uint32_t)ctx->r5;
    const uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&camera_ticks, &bits, sizeof camera_ticks);
}

extern "C" void jfg_free_camera_entry(uint8_t* rdram, recomp_context* ctx) {
    // Undo last frame's pitch orbit so the game's smoothing and collision work from its own camera position.
    if (pitch_applied.valid && same(read_vec3(rdram, cam0, 0x0C), pitch_applied.written) &&
        (int16_t)MEM_H(0x02, cam0) == pitch_applied.written_pitch) {
        write_vec3(rdram, cam0, 0x0C, pitch_applied.orig);
        MEM_H(0x02, cam0) = pitch_applied.orig_pitch;
    }
    pitch_applied.valid = false;
    if (is_player0((uint32_t)ctx->r5)) {
        jfg::controls::g_frame.camera_kind = 1;
    }
}

extern "C" void jfg_free_camera_heading(uint8_t* rdram, recomp_context* ctx) {
    // At 0x8002DFE8 the follow camera has chosen this frame's orbit heading pd+0x104 (s0 = pd). The camera list still
    // holds last frame's camera, i.e. the view on screen.
    const uint32_t pd = (uint32_t)ctx->r16;
    if (!is_player0(pd)) return;
    const gpr p = kseg0(pd), o = kseg0(jfg::controls::g_frame.obj);
    const float delay = jfg::controls::settings().drift_delay;
    // Any frame without a follow-camera step (aim mode, cutscene, menu, other camera) ends here: continue from the
    // view on screen.
    const bool resync = last_follow_frame + 1 != game_frame;
    if (resync) {
        jfg::controls::free_camera_resync(free_cam, jfg::controls::orbit_heading(read_vec3(rdram, cam0, 0x0C),
                                                                                  read_vec3(rdram, o, 0x0C)), delay);
    }
    last_follow_frame = game_frame;
    const bool owned = jfg::controls::free_camera_step(free_cam, jfg::controls::frame(), look_params(), delay,
                                                       camera_ticks / 60.f);
    if (owned || resync) {
        MEM_H(0x104, p) = free_cam.yaw;
        MEM_H(0x10A, p) = 0; // no C-pan on top
    } else {
        jfg::controls::free_camera_follow(free_cam, (int16_t)MEM_H(0x104, p));
    }
}

extern "C" void func_80047C1C(uint8_t* rdram, recomp_context* ctx); // the player's current weapon (idx, chr)

extern "C" int jfg_manual_aim(uint8_t* rdram, recomp_context* ctx) {
    // controlGetManualAim(obj, pd, yawLim, pitchLim, pitchMin, pitchMax, &yawOff, &pitchOff, ticks) at entry:
    // a0..a3 then the caller's stack args at sp+0x10.
    const uint32_t pd = (uint32_t)ctx->r5;
    if (!is_player0(pd)) return 0;
    const gpr p = kseg0(pd), sp = ctx->r29;
    if ((MEM_B(1, p) & 3) == 3) return 0; // hovership: the original
    const jfg::controls::AimLimits lim{(int16_t)ctx->r6, (int16_t)ctx->r7, (int16_t)MEM_W(0x10, sp),
                                       (int16_t)MEM_W(0x14, sp)};
    const gpr yaw_off_ptr = (gpr)(int32_t)MEM_W(0x18, sp), pitch_off_ptr = (gpr)(int32_t)MEM_W(0x1C, sp);
    const float ticks = read_f32(rdram, sp, 0x20);
    aim_ticks = ticks;

    jfg::controls::AimTuning tuning{};
    for (int i = 0; i < 20; i++) tuning.rim_rate[i] = read_f32(rdram, rim_rate_table, 4 * i);
    tuning.rate_smooth = read_f32(rdram, rim_rate_smooth, 0);
    tuning.zoom_in_vel = read_f32(rdram, zoom_in_vel, 0);
    tuning.zoom_out_vel = read_f32(rdram, zoom_out_vel, 0);
    bool tuning_ok = std::isfinite(tuning.rate_smooth) && std::isfinite(tuning.zoom_in_vel) &&
                     std::isfinite(tuning.zoom_out_vel) && std::isfinite(ticks);
    for (float r : tuning.rim_rate) tuning_ok = tuning_ok && std::isfinite(r);
    if (!tuning_ok) { // fail loudly and leave aiming to the original rather than guess
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr, "[controls] controlGetManualAim constants are not finite; using the original aim\n");
        }
        return 0;
    }

    // The zoom weapon (type 7), as the original decides it. From here on the original isn't run, so calling into
    // the game may clobber the argument registers.
    ctx->r4 = MEM_B(0, p);
    ctx->r5 = MEM_B(1, p);
    func_80047C1C(rdram, ctx);
    const gpr weapon = ctx->r2;
    jfg::controls::AimInput in{};
    in.zoom_weapon = weapon != 0 && MEM_BU(0x70, weapon) == 7 && MEM_BU(0x18E, p) == 0;
    const uint16_t keys = (uint16_t)MEM_W(0, control_keys);
    in.zoom_dir = (keys & c_up) ? 1 : (keys & c_down) ? -1 : 0;
    if (in.zoom_weapon) {
        // The wheel / bumpers zoom; don't also cycle the weapon this frame (weapon selection runs after the aim state).
        for (int i = 0; i < 3; i++) MEM_W(4 * i, control_keys) = MEM_W(4 * i, control_keys) & ~(int32_t)(c_up | c_down);
    }
    const jfg::controls::ModernInput& modern = jfg::controls::frame();
    in.mouse_dx = modern.mouse_dx;
    in.mouse_dy = modern.mouse_dy;
    in.stick_x = modern.stick_look_x;
    in.stick_y = modern.stick_look_y;

    // A new aim session starts with the crosshair centred.
    if (last_aim_frame + 1 < game_frame || last_aim_frame > game_frame) {
        aim_state.yaw_off = aim_state.pitch_off = 0;
    }
    last_aim_frame = game_frame;
    aim_state.smoothed_yaw_off = (int16_t)MEM_H(0x1CE, p);
    aim_state.smoothed_pitch = (int16_t)MEM_H(0x1E0, p);
    aim_state.facing = (int16_t)MEM_H(0x11C, p);
    aim_state.pitch_base = (int16_t)MEM_H(0x1E2, p);
    aim_state.yaw_rate = read_f32(rdram, p, 0x1E4);
    aim_state.pitch_rate = read_f32(rdram, p, 0x1E8);
    aim_state.zoom = read_f32(rdram, p, 0x1EC);
    aim_state.zoom_vel = read_f32(rdram, p, 0x1F0);

    const auto& st = jfg::controls::settings();
    const jfg::controls::AimOptions opts{recomp::get_mouse_sensitivity() * mouse_units_per_sensitivity * st.aim_mult,
                                         st.invert_mouse_y, st.invert_stick_y};
    const jfg::controls::AimOutput out =
        jfg::controls::aim_step(aim_state, in, aim_mouse_mode, lim, tuning, opts, ticks);
    aim_state = out.next;
    aim_mouse_mode = out.mouse_mode;

    MEM_H(0, yaw_off_ptr) = aim_state.yaw_off;
    MEM_H(0, pitch_off_ptr) = aim_state.pitch_off;
    MEM_H(0x11C, p) = aim_state.facing;
    MEM_H(0x1E2, p) = aim_state.pitch_base;
    write_f32(rdram, p, 0x1E4, aim_state.yaw_rate);
    write_f32(rdram, p, 0x1E8, aim_state.pitch_rate);
    if (in.zoom_weapon) write_f32(rdram, p, 0x1EC, aim_state.zoom);
    write_f32(rdram, p, 0x1F0, aim_state.zoom_vel);
    last_yaw_off = aim_state.yaw_off;
    last_pitch_off = aim_state.pitch_off;
    ctx->r2 = out.return_value;
    return 1;
}

extern "C" void jfg_aim_move(uint8_t* rdram, recomp_context* ctx) {
    // Animation select (obj, pd, ...) runs after the state handler and before facing -> velocity: walking while aiming
    // sets the speeds here so both see them. Crouch-aim (state 5) keeps the original side-rolls.
    const uint32_t pd = (uint32_t)ctx->r5;
    if (!is_player0(pd)) return;
    const gpr p = kseg0(pd);
    constexpr int aim_state_id = 11;
    if (MEM_BU(0x568, p) != aim_state_id) return;
    const jfg::controls::ModernInput& in = jfg::controls::frame();
    const jfg::controls::MoveOutput out =
        jfg::controls::aim_move(read_f32(rdram, p, 0x04), read_f32(rdram, p, 0x10), in.move_x, in.move_y,
                                (int16_t)MEM_H(0x11C, p), (int16_t)MEM_H(0x00, cam0), aim_ticks);
    write_f32(rdram, p, 0x04, out.forward);
    write_f32(rdram, p, 0x10, out.strafe);
}

namespace {
// The free camera's pitch: after the dispatcher (and its water-line fix-ups) placed the follow camera, orbit it up or
// down around the point it looks at. Undone at the next follow-camera entry.
void apply_free_camera_pitch(uint8_t* rdram) {
    if (jfg::controls::g_frame.camera_kind != 1 || std::fabs(free_cam.pitch) < 0.01f) return;
    const gpr o = kseg0(jfg::controls::g_frame.obj);
    const jfg::controls::Vec3 cam = read_vec3(rdram, cam0, 0x0C), player = read_vec3(rdram, o, 0x0C);
    const int16_t cam_pitch = (int16_t)MEM_H(0x02, cam0);
    // The camera looks at the player from above: its pitch (positive = down) gives the look-at height.
    const float r = std::hypot(cam.x - player.x, cam.z - player.z);
    float target_y = cam.y - r * std::tan(cam_pitch * (3.14159265f / 32768.f)) - player.y;
    if (!(target_y >= look_target_min && target_y <= look_target_max)) target_y = 60.f;
    const jfg::controls::Vec3 head{player.x, player.y + target_y, player.z};
    const jfg::controls::PitchOrbit orbit = jfg::controls::pitch_orbit(cam, head, player.y + 4.f, free_cam.pitch);
    pitch_applied = {true, cam, orbit.pos, cam_pitch,
                     (int16_t)(cam_pitch + (int32_t)std::lround(orbit.delta_elevation * (32768.f / 3.14159265f)))};
    write_vec3(rdram, cam0, 0x0C, orbit.pos);
    MEM_H(0x02, cam0) = pitch_applied.written_pitch;
}
}

extern "C" void jfg_controls_after_camera(uint8_t* rdram, recomp_context*) {
    auto& f = jfg::controls::g_frame;
    const uint32_t pd = camera_pd;
    if (!is_player0(pd)) return;
    apply_free_camera_pitch(rdram);
    const int camera_kind = f.camera_kind;
    f.camera_kind = 0;
    if (!debug) return;
    const gpr p = kseg0(pd), o = kseg0(f.obj);
    const jfg::controls::ModernInput& in = jfg::controls::frame();
    std::fprintf(stderr,
                 "[controls] f=%u active=%d cam=%d idx=%d chr=%d state=%u facing=%04X orbit=%04X pan=%04X follow=%u "
                 "camyaw=%04X campitch=%04X campos=(%.1f,%.1f,%.1f) pos=(%.1f,%.1f,%.1f) objyaw=%04X look=(%.2f,%.2f) mouse=(%.1f,%.1f) move=(%.2f,%.2f) aim=%d fc=(%04X,%.2f,%.2f) ticks=%.1f aimoff=(%d,%d) base=%04X mm=%d speed=(%.2f,%.2f)\n",
                 frame_no++, f.active, camera_kind, MEM_B(0, p), MEM_B(1, p) & 3, MEM_BU(0x568, p),
                 (uint16_t)MEM_H(0x11C, p), (uint16_t)MEM_H(0x104, p), (uint16_t)MEM_H(0x10A, p), MEM_BU(0x10C, p),
                 (uint16_t)MEM_H(0x00, cam0), (uint16_t)MEM_H(0x02, cam0), read_f32(rdram, cam0, 0x0C), read_f32(rdram, cam0, 0x10),
                 read_f32(rdram, cam0, 0x14), read_f32(rdram, o, 0x0C), read_f32(rdram, o, 0x10), read_f32(rdram, o, 0x14), (uint16_t)MEM_H(0x00, o), in.stick_look_x, in.stick_look_y, in.mouse_dx,
                 in.mouse_dy, in.move_x, in.move_y, in.aim_held, (uint16_t)free_cam.yaw, free_cam.pitch, free_cam.idle,
                 camera_ticks, last_yaw_off, last_pitch_off, (uint16_t)MEM_H(0x1E2, p), aim_mouse_mode,
                 read_f32(rdram, p, 0x04), read_f32(rdram, p, 0x10));
}

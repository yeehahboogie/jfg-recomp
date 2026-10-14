#ifndef JFG_CONTROLS_HOOKS_H
#define JFG_CONTROLS_HOOKS_H
/* Modern controls hooks (docs/superpowers/specs/2026-10-14-jfg-modern-controls-design.md).
   Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* joyRead (0x80043130) after osContGetReadData: latch this frame's modern input. */
void jfg_controls_latch(uint8_t* rdram, recomp_context* ctx);
/* controlPlayer (0x80032A48) entry, a0 = obj: per-player gate. */
void jfg_controls_begin_player(uint8_t* rdram, recomp_context* ctx);
/* controlPlayer after it picks controlModeKeys (0x80032B40): Modern always uses the Normal key map. */
void jfg_controls_force_normal_keymap(uint8_t* rdram, recomp_context* ctx);
/* Camera dispatcher (0x8002B418) entry, a1 = pd. */
void jfg_controls_camera_begin(uint8_t* rdram, recomp_context* ctx);
/* Camera dispatcher before its epilogue (every path reaches 0x8002B954). */
void jfg_controls_after_camera(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

#ifndef JFG_INTERP_HOOKS_H
#define JFG_INTERP_HOOKS_H
/* Frame interpolation hooks. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): hand the frame recorded since the last submit to the renderer. */
void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx);
/* Entry of camSetProjMtx (0x80040AA4): the current camera's view x projection is about to be written at *$a1 and
   loaded into slot 0. */
void jfg_interp_camera(uint8_t* rdram, recomp_context* ctx);
/* Entry of camInit (0x8003F640): a level is loading; every camera starts afresh. */
void jfg_interp_level_start(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

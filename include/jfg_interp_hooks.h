#ifndef JFG_INTERP_HOOKS_H
#define JFG_INTERP_HOOKS_H
/* Frame interpolation hooks. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): hand the frame recorded since the last submit to the renderer. */
void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

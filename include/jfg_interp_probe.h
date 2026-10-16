#ifndef JFG_INTERP_PROBE_H
#define JFG_INTERP_PROBE_H
/* JFG_DEBUG_MTXSRC=<frame>: which code wrote each matrix of one frame. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): a graphics task is submitted. */
void jfg_probe_submit(uint8_t* rdram, recomp_context* ctx);
/* Entry of mathMtxF2L (0x80048D84): a float matrix at $a0 is converted into the fixed-point matrix at $a1. */
void jfg_probe_f2l(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

#ifndef JFG_CIC_HOOK_H
#define JFG_CIC_HOOK_H
/* Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Native replacement for the game's CIC-6105 challenge routine (0x800436C0, "cic6105_challenge"). */
void jfg_cic_challenge(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

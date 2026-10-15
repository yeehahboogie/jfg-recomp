#ifndef JFG_LEVEL_HOOKS_H
#define JFG_LEVEL_HOOKS_H
/* Level survey aids (docs/superpowers/plans/2026-10-15-jfg-whole-game.md). Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* mainChangeLevel (0x8004665C) entry: JFG_DEBUG_LEVELS logs the request. */
void jfg_level_change(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

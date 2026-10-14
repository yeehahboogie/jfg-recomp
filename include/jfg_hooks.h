#ifndef JFG_HOOKS_H
#define JFG_HOOKS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called at the start of the game's DMA routine: registers a module when its code is copied in. */
void jfg_on_dma(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size);
/* Called just before the game marks module `index` unloaded. */
void jfg_on_module_unload(uint8_t* rdram, uint32_t index);
/* Called on entry to the game's lazy linker; `ra` is the return address of the call being linked. */
void jfg_on_lazy_link(uint8_t* rdram, uint32_t ra);
/* The ROM word at a cartridge address (0x10000000-based; kseg1 0xB0000000 also accepted), or 0 past the end.
   Replaces the game's direct PI reads in its anti-tamper checks (modules 111 and 133). */
uint32_t jfg_cart_word(uint32_t cart_address);
/* Bytes of audio still queued, as AI_LEN_REG would report (the game reads that register directly). */
uint32_t jfg_ai_length(void);
/* The address the game has patched into a lui/LO16 pair in RAM. */
uint32_t jfg_patched_hi_lo(uint8_t* rdram, uint32_t hi_site, uint32_t lo_site);
#ifdef __cplusplus
}
#endif
#endif

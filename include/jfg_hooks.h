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
/* The address the game has patched into a lui/LO16 pair in RAM. */
uint32_t jfg_patched_hi_lo(uint8_t* rdram, uint32_t hi_site, uint32_t lo_site);
#ifdef __cplusplus
}
#endif
#endif

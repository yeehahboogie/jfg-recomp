#ifndef JFG_FPU_MODE_H
#define JFG_FPU_MODE_H
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Puts a context's FPU in FR=1 mode (GameEntry on_init_callback and thread_create_callback). */
void jfg_set_fpu_mode(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif

#ifndef JFG_CIC_H
#define JFG_CIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The CIC-NUS-6105's answer to a challenge: 15 bytes (30 nibbles) as they sit in PIF RAM 0x30..0x3E. */
void jfg_cic6105_response(const uint8_t challenge[15], uint8_t response[15]);
#ifdef __cplusplus
}
#endif
#endif

/*
 * Portability shims for the UniFi agent.
 *
 * The protocol, crypto and parser modules build both with SDCC for the
 * RTL837x 8051 and with a host compiler for the unit tests. On SDCC every
 * parameter and local lives in XDATA: small-model SDCC would otherwise put
 * them in internal RAM, which the base firmware already uses up.
 */
#ifndef _UF_PORT_H_
#define _UF_PORT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __SDCC
#define UF_X		__xdata
#define UF_C		__code
#define UF_BANKED	__banked
#else
#define UF_X
#define UF_C
#define UF_BANKED
#endif

#define UF_NPORTS	5

void uf_memcpy(UF_X uint8_t * UF_X dst, UF_X const uint8_t * UF_X src, UF_X uint16_t len) UF_BANKED;
void uf_memset(UF_X uint8_t * UF_X dst, UF_X uint8_t v, UF_X uint16_t len) UF_BANKED;
uint8_t uf_memeq_ct(UF_X const uint8_t * UF_X a, UF_X const uint8_t * UF_X b, UF_X uint8_t len) UF_BANKED;

#endif

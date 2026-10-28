/*
 * Small memory helpers with 16-bit lengths. The firmware's own memset takes an
 * 8-bit length, and the host build must not depend on libc behaviour either.
 */
#include "uf_port.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

void uf_memcpy(UF_X uint8_t * UF_X dst, UF_X const uint8_t * UF_X src, UF_X uint16_t len) UF_BANKED
{
	while (len--)
		*dst++ = *src++;
}

void uf_memset(UF_X uint8_t * UF_X dst, UF_X uint8_t v, UF_X uint16_t len) UF_BANKED
{
	while (len--)
		*dst++ = v;
}

/* Constant-time comparison: the loop never exits early on a mismatch */
uint8_t uf_memeq_ct(UF_X const uint8_t * UF_X a, UF_X const uint8_t * UF_X b, UF_X uint8_t len) UF_BANKED
{
	UF_X uint8_t diff = 0;

	while (len--)
		diff |= *a++ ^ *b++;
	return diff == 0;
}

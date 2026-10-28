/*
 * TNBU inform framing: 40-byte header, then AES-CBC or AES-GCM payload.
 * Header: "TNBU", u32 packet version 0, MAC, u16 flags, 16-byte IV/nonce,
 * u32 payload version 1, u32 payload length (big endian). GCM authenticates
 * the whole header and appends a 16-byte tag counted in the payload length.
 */
#ifndef _UF_TNBU_H_
#define _UF_TNBU_H_

#include "uf_port.h"

#define UF_TNBU_HDR		40
#define UF_TNBU_ENC		0x01
#define UF_TNBU_ZLIB		0x02
#define UF_TNBU_SNAPPY		0x04
#define UF_TNBU_GCM		0x08

#define UF_TNBU_BAD		0xffff

/* Reasons uf_tnbu_open() rejected a frame, in uf_tnbu_err */
#define UF_TERR_SHORT		1
#define UF_TERR_MAGIC		2
#define UF_TERR_VERSION		3
#define UF_TERR_LENGTH		4
#define UF_TERR_MAC		5
#define UF_TERR_FLAGS		6
#define UF_TERR_NEED_GCM	7
#define UF_TERR_AUTH		8
#define UF_TERR_PADDING		9

extern UF_X uint8_t uf_tnbu_err;
extern UF_X uint16_t uf_tnbu_flags;

/* Encrypts the n plaintext bytes at frame + 40 with the current AES key and
 * fills in the header. CBC needs 16 spare bytes after the plaintext for
 * padding, GCM needs 16 for the tag. Returns the frame length. */
uint16_t uf_tnbu_seal(UF_X uint8_t * UF_X frame, UF_X const uint8_t * UF_X mac,
		      UF_X const uint8_t * UF_X iv, UF_X uint16_t n, UF_X uint8_t gcm) UF_BANKED;

/* Validates and decrypts a received frame in place with the current AES key.
 * Returns the plaintext length (plaintext at frame + 40) or UF_TNBU_BAD. */
uint16_t uf_tnbu_open(UF_X uint8_t * UF_X frame, UF_X uint16_t len,
		      UF_X const uint8_t * UF_X mac, UF_X uint8_t need_gcm) UF_BANKED;

#endif

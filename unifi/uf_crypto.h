/*
 * AES-128, CBC with PKCS#7 and GCM (16-byte nonce) for the UniFi inform protocol.
 * One key is active at a time: uf_aes_setkey() selects it for all modes.
 */
#ifndef _UF_CRYPTO_H_
#define _UF_CRYPTO_H_

#include "uf_port.h"

#define UF_AES_BLOCK	16
#define UF_CBC_BAD	0xffff

void uf_aes_setkey(UF_X const uint8_t * UF_X key) UF_BANKED;
void uf_aes_encrypt(UF_X uint8_t * UF_X blk) UF_BANKED;
void uf_aes_decrypt(UF_X uint8_t * UF_X blk) UF_BANKED;

/* Pads buf[0..len) in place to a block multiple and returns the new length. */
uint16_t uf_pkcs7_pad(UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;
void uf_cbc_encrypt(UF_X const uint8_t * UF_X iv, UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;
/* Decrypts in place and strips the padding. Returns the plaintext length or UF_CBC_BAD. */
uint16_t uf_cbc_decrypt(UF_X const uint8_t * UF_X iv, UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;

/*
 * GCM takes its arguments in uf_gcm: with six of them SDCC would spill into
 * internal RAM at every call site.
 */
struct uf_gcm_args {
	UF_X const uint8_t *iv;		/* 16 bytes */
	UF_X const uint8_t *aad;
	uint8_t aad_len;
	UF_X uint8_t *buf;		/* processed in place */
	uint16_t len;
	UF_X uint8_t *tag;		/* 16 bytes */
};
extern UF_X struct uf_gcm_args uf_gcm;

/* Encrypts buf in place and writes the tag */
void uf_gcm_seal(void) UF_BANKED;
/* Checks the tag first; decrypts buf in place only when it matches. Returns 1 when authentic. */
uint8_t uf_gcm_open(void) UF_BANKED;

#endif

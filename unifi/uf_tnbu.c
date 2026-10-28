/*
 * TNBU frame sealing and opening for the UniFi inform protocol
 */
#include "uf_tnbu.h"
#include "uf_crypto.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

UF_X uint8_t uf_tnbu_err;
UF_X uint16_t uf_tnbu_flags;

static void put_u32(UF_X uint8_t * UF_X p, UF_X uint16_t v)
{
	p[0] = 0;
	p[1] = 0;
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void header(UF_X uint8_t * UF_X h, UF_X const uint8_t * UF_X mac, UF_X uint8_t flags,
		   UF_X const uint8_t * UF_X iv, UF_X uint16_t len)
{
	h[0] = 'T'; h[1] = 'N'; h[2] = 'B'; h[3] = 'U';
	put_u32(h + 4, 0);
	uf_memcpy(h + 8, mac, 6);
	h[14] = 0;
	h[15] = flags;
	uf_memcpy(h + 16, iv, 16);
	put_u32(h + 32, 1);
	put_u32(h + 36, len);
}

/* Kept at file scope: these stay live across calls (see uf_crypto.c) */
static UF_X uint8_t * UF_X t_frame;
static UF_X uint16_t t_n;
static UF_X uint8_t * UF_X t_iv;
static UF_X uint8_t * UF_X t_body;
static UF_X uint8_t * UF_X t_tag;

/* Sets the frame's pointers, and the GCM arguments for a frame of t_n payload bytes */
static void t_ptrs(void)
{
	t_iv = t_frame + 16;
	t_body = t_frame + UF_TNBU_HDR;
	t_tag = t_body + t_n;
	uf_gcm.iv = t_iv;
	uf_gcm.aad = t_frame;
	uf_gcm.aad_len = UF_TNBU_HDR;
	uf_gcm.buf = t_body;
	uf_gcm.len = t_n;
	uf_gcm.tag = t_tag;
}

uint16_t uf_tnbu_seal(UF_X uint8_t * UF_X frame, UF_X const uint8_t * UF_X mac,
		      UF_X const uint8_t * UF_X iv, UF_X uint16_t n, UF_X uint8_t gcm) UF_BANKED
{
	t_frame = frame;
	t_n = n;
	if (gcm) {
		header(t_frame, mac, UF_TNBU_ENC | UF_TNBU_GCM, iv, t_n + 16);
		t_ptrs();
		uf_gcm_seal();
		return UF_TNBU_HDR + t_n + 16;
	}
	t_ptrs();
	t_n = uf_pkcs7_pad(t_body, t_n);
	header(t_frame, mac, UF_TNBU_ENC, iv, t_n);
	uf_cbc_encrypt(t_iv, t_body, t_n);
	return UF_TNBU_HDR + t_n;
}

static uint8_t fail(UF_X uint8_t e)
{
	uf_tnbu_err = e;
	return 0;
}

static uint8_t check(UF_X uint8_t * UF_X f, UF_X uint16_t len, UF_X const uint8_t * UF_X mac,
		     UF_X uint8_t need_gcm)
{
	UF_X uint16_t body;

	if (len < UF_TNBU_HDR)
		return fail(UF_TERR_SHORT);
	if (f[0] != 'T' || f[1] != 'N' || f[2] != 'B' || f[3] != 'U')
		return fail(UF_TERR_MAGIC);
	if (f[4] | f[5] | f[6] | f[7] | f[32] | f[33] | f[34] || f[35] != 1)
		return fail(UF_TERR_VERSION);
	body = len - UF_TNBU_HDR;
	if (f[36] || f[37] || f[38] != (uint8_t)(body >> 8) || f[39] != (uint8_t)body)
		return fail(UF_TERR_LENGTH);
	if (!uf_memeq_ct(f + 8, mac, 6))
		return fail(UF_TERR_MAC);
	uf_tnbu_flags = ((uint16_t)f[14] << 8) | f[15];
	/* Only encrypted, uncompressed payloads are accepted */
	if (!(uf_tnbu_flags & UF_TNBU_ENC) || (uf_tnbu_flags & ~(UF_TNBU_ENC | UF_TNBU_GCM)))
		return fail(UF_TERR_FLAGS);
	if (need_gcm && !(uf_tnbu_flags & UF_TNBU_GCM))
		return fail(UF_TERR_NEED_GCM);
	if (uf_tnbu_flags & UF_TNBU_GCM) {
		if (body < 16)
			return fail(UF_TERR_LENGTH);
	} else if (!body || (body & 15)) {
		return fail(UF_TERR_LENGTH);
	}
	return 1;
}

uint16_t uf_tnbu_open(UF_X uint8_t * UF_X frame, UF_X uint16_t len,
		      UF_X const uint8_t * UF_X mac, UF_X uint8_t need_gcm) UF_BANKED
{
	uf_tnbu_err = 0;
	t_frame = frame;
	t_n = len - UF_TNBU_HDR;
	if (!check(frame, len, mac, need_gcm))
		return UF_TNBU_BAD;
	if (uf_tnbu_flags & UF_TNBU_GCM) {
		t_n -= 16;
		t_ptrs();
		if (!uf_gcm_open()) {
			uf_tnbu_err = UF_TERR_AUTH;
			return UF_TNBU_BAD;
		}
		return t_n;
	}
	t_ptrs();
	t_n = uf_cbc_decrypt(t_iv, t_body, t_n);
	if (t_n == UF_CBC_BAD) {
		uf_tnbu_err = UF_TERR_PADDING;
		return UF_TNBU_BAD;
	}
	return t_n;
}

/*
 * AES-128 (FIPS-197), CBC with PKCS#7, and GCM (SP 800-38D) with the 16-byte
 * nonce the UniFi inform protocol uses. Byte-oriented for the 8051: no T-tables,
 * the S-boxes are generated into XDATA on first use.
 */
#include "uf_crypto.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

static UF_X uint8_t sbox[256];
static UF_X uint8_t rsbox[256];
static UF_X uint8_t sbox_ready;
static UF_X uint8_t rk[176];

/* Scratch shared by the block functions and GHASH */
static UF_X uint8_t t[4];
static UF_X uint8_t s16[16];
static UF_X uint8_t gh_h[16];
static UF_X uint8_t gh_y[16];
static UF_X uint8_t gh_z[16];
static UF_X uint8_t gh_v[16];
static UF_X uint8_t ctr[16];
static UF_X uint8_t ek_j0[16];
static UF_X uint8_t ks[16];

/*
 * State that stays live across a call is kept at file scope: SDCC must reload
 * globals after a call, while locals it caches would be spilled to internal RAM.
 */
static UF_X uint16_t c_off;
static UF_X const uint8_t * UF_X c_prev;
static UF_X uint8_t c_n;
static UF_X uint8_t * UF_X c_buf;
static UF_X uint8_t * UF_X c_dst;
static UF_X const uint8_t * UF_X c_src;
static UF_X uint16_t c_len;

#define XTIME(x)	((uint8_t)(((x) << 1) ^ (((x) & 0x80) ? 0x1b : 0x00)))

#define ROTL8(x, s)	((uint8_t)(((x) << (s)) | ((x) >> (8 - (s)))))

/* Walks the multiplicative group with generator 3: p = 3^k, q = 3^-k */
static void sbox_init(void)
{
	UF_X uint8_t p = 1, q = 1, x;
	UF_X uint16_t i;

	do {
		p = p ^ (uint8_t)(p << 1) ^ ((p & 0x80) ? 0x1b : 0x00);
		q ^= (uint8_t)(q << 1);
		q ^= (uint8_t)(q << 2);
		q ^= (uint8_t)(q << 4);
		if (q & 0x80)
			q ^= 0x09;
		x = q ^ ROTL8(q, 1) ^ ROTL8(q, 2) ^ ROTL8(q, 3) ^ ROTL8(q, 4);
		sbox[p] = x ^ 0x63;
	} while (p != 1);
	sbox[0] = 0x63;
	for (i = 0; i < 256; i++)
		rsbox[sbox[i]] = (uint8_t)i;
	sbox_ready = 1;
}

void uf_aes_setkey(UF_X const uint8_t * UF_X key) UF_BANKED
{
	UF_X uint8_t i, rcon = 1, tmp;

	if (!sbox_ready)
		sbox_init();
	for (i = 0; i < 16; i++)
		rk[i] = key[i];
	for (i = 16; i < 176; i += 4) {
		t[0] = rk[i - 4]; t[1] = rk[i - 3]; t[2] = rk[i - 2]; t[3] = rk[i - 1];
		if ((i & 15) == 0) {
			tmp = t[0];
			t[0] = sbox[t[1]] ^ rcon;
			t[1] = sbox[t[2]];
			t[2] = sbox[t[3]];
			t[3] = sbox[tmp];
			rcon = XTIME(rcon);
		}
		rk[i] = rk[i - 16] ^ t[0];
		rk[i + 1] = rk[i - 15] ^ t[1];
		rk[i + 2] = rk[i - 14] ^ t[2];
		rk[i + 3] = rk[i - 13] ^ t[3];
	}
}

static void add_round_key(UF_X uint8_t * UF_X b, UF_X uint8_t round)
{
	UF_X uint8_t i;
	UF_X uint8_t * UF_X k = rk + (uint8_t)(round << 4);

	for (i = 0; i < 16; i++)
		b[i] ^= k[i];
}

/* State is column-major: b[4*c + r] */
static void shift_rows(UF_X uint8_t * UF_X b, UF_X uint8_t inv)
{
	UF_X uint8_t r, c, n;

	for (r = 1; r < 4; r++) {
		for (c = 0; c < 4; c++)
			t[c] = b[(c << 2) + r];
		for (c = 0; c < 4; c++) {
			n = inv ? (uint8_t)((c + 4 - r) & 3) : (uint8_t)((c + r) & 3);
			b[(c << 2) + r] = t[n];
		}
	}
}

static UF_X uint8_t m_a0, m_a1, m_a2, m_a3, m_all, m_x;

static void mix_column(UF_X uint8_t * UF_X col)
{
	m_a0 = col[0]; m_a1 = col[1]; m_a2 = col[2]; m_a3 = col[3];
	m_all = m_a0 ^ m_a1 ^ m_a2 ^ m_a3;
	m_x = m_a0 ^ m_a1; col[0] ^= m_all ^ XTIME(m_x);
	m_x = m_a1 ^ m_a2; col[1] ^= m_all ^ XTIME(m_x);
	m_x = m_a2 ^ m_a3; col[2] ^= m_all ^ XTIME(m_x);
	m_x = m_a3 ^ m_a0; col[3] ^= m_all ^ XTIME(m_x);
}

/* InvMixColumns = MixColumns after pre-multiplying with {04}x^2 + {05} */
static void inv_mix_column(UF_X uint8_t * UF_X col)
{
	m_x = col[0] ^ col[2]; m_x = XTIME(m_x); m_a0 = XTIME(m_x);
	m_x = col[1] ^ col[3]; m_x = XTIME(m_x); m_a1 = XTIME(m_x);
	col[0] ^= m_a0; col[1] ^= m_a1; col[2] ^= m_a0; col[3] ^= m_a1;
	mix_column(col);
}

void uf_aes_encrypt(UF_X uint8_t * UF_X blk) UF_BANKED
{
	UF_X uint8_t round, i;

	add_round_key(blk, 0);
	for (round = 1; round <= 10; round++) {
		for (i = 0; i < 16; i++)
			blk[i] = sbox[blk[i]];
		shift_rows(blk, 0);
		if (round != 10)
			for (i = 0; i < 16; i += 4)
				mix_column(blk + i);
		add_round_key(blk, round);
	}
}

void uf_aes_decrypt(UF_X uint8_t * UF_X blk) UF_BANKED
{
	UF_X uint8_t round, i;

	add_round_key(blk, 10);
	for (round = 9; ; round--) {
		shift_rows(blk, 1);
		for (i = 0; i < 16; i++)
			blk[i] = rsbox[blk[i]];
		add_round_key(blk, round);
		if (!round)
			break;
		for (i = 0; i < 16; i += 4)
			inv_mix_column(blk + i);
	}
}

uint16_t uf_pkcs7_pad(UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED
{
	UF_X uint8_t pad = (uint8_t)(16 - (len & 15));
	UF_X uint8_t i;

	for (i = 0; i < pad; i++)
		buf[len + i] = pad;
	return len + pad;
}

void uf_cbc_encrypt(UF_X const uint8_t * UF_X iv, UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED
{
	UF_X uint8_t i;

	c_prev = iv;
	for (c_off = 0; c_off < len; c_off += 16) {
		for (i = 0; i < 16; i++)
			buf[c_off + i] ^= c_prev[i];
		c_prev = buf + c_off;
		uf_aes_encrypt(buf + c_off);
	}
}

uint16_t uf_cbc_decrypt(UF_X const uint8_t * UF_X iv, UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED
{
	UF_X uint8_t i, pad, bad;

	if (!len || (len & 15))
		return UF_CBC_BAD;
	c_buf = buf;
	c_len = len;
	for (i = 0; i < 16; i++)
		gh_v[i] = iv[i];		// previous ciphertext block
	for (c_off = 0; c_off < c_len; c_off += 16) {
		c_dst = c_buf + c_off;
		uf_memcpy(s16, c_dst, 16);
		uf_aes_decrypt(c_dst);
		uf_memcpy(ks, c_dst, 16);
		for (i = 0; i < 16; i++) {
			ks[i] ^= gh_v[i];
			gh_v[i] = s16[i];
		}
		uf_memcpy(c_dst, ks, 16);
	}
	buf = c_buf;
	len = c_len;
	pad = buf[len - 1];
	if (pad == 0 || pad > 16)
		return UF_CBC_BAD;
	bad = 0;
	for (i = 1; i <= pad; i++)
		bad |= buf[len - i] ^ pad;
	if (bad)
		return UF_CBC_BAD;
	return len - pad;
}

/* gh_y = gh_y * gh_h in GF(2^128), bit-reflected as in SP 800-38D */
static void gf_mul(void)
{
	UF_X uint8_t i, j, m, lsb;

	for (i = 0; i < 16; i++) {
		gh_z[i] = 0;
		gh_v[i] = gh_h[i];
	}
	for (i = 0; i < 16; i++) {
		for (m = 0x80; m; m >>= 1) {
			if (gh_y[i] & m)
				for (j = 0; j < 16; j++)
					gh_z[j] ^= gh_v[j];
			lsb = gh_v[15] & 1;
			for (j = 15; j; j--)
				gh_v[j] = (uint8_t)((gh_v[j] >> 1) | (gh_v[j - 1] << 7));
			gh_v[0] >>= 1;
			if (lsb)
				gh_v[0] ^= 0xe1;
		}
	}
	for (i = 0; i < 16; i++)
		gh_y[i] = gh_z[i];
}

/* Absorbs data, zero-padding the final partial block */
static void ghash_update(UF_X const uint8_t * UF_X p, UF_X uint16_t len)
{
	UF_X uint8_t i;

	c_src = p;
	c_len = len;
	for (c_off = 0; c_off < c_len; c_off += c_n) {
		c_n = (c_len - c_off) > 16 ? 16 : (uint8_t)(c_len - c_off);
		for (i = 0; i < c_n; i++)
			gh_y[i] ^= c_src[c_off + i];
		gf_mul();
	}
}

static void ghash_lengths(UF_X uint32_t a_bits, UF_X uint32_t c_bits)
{
	UF_X uint8_t i;

	for (i = 0; i < 16; i++)
		s16[i] = 0;
	s16[4] = (uint8_t)(a_bits >> 24); s16[5] = (uint8_t)(a_bits >> 16);
	s16[6] = (uint8_t)(a_bits >> 8); s16[7] = (uint8_t)a_bits;
	s16[12] = (uint8_t)(c_bits >> 24); s16[13] = (uint8_t)(c_bits >> 16);
	s16[14] = (uint8_t)(c_bits >> 8); s16[15] = (uint8_t)c_bits;
	ghash_update(s16, 16);
}

static void ghash_reset(void)
{
	UF_X uint8_t i;

	for (i = 0; i < 16; i++)
		gh_y[i] = 0;
}

/* Derives H, J0 = GHASH(IV || len) for a non-96-bit IV, and E(K, J0) */
static void gcm_start(UF_X const uint8_t * UF_X iv)
{
	UF_X uint8_t i;

	for (i = 0; i < 16; i++)
		gh_h[i] = 0;
	uf_aes_encrypt(gh_h);
	ghash_reset();
	ghash_update(iv, 16);
	ghash_lengths(0, 128);
	for (i = 0; i < 16; i++)
		ctr[i] = ek_j0[i] = gh_y[i];
	uf_aes_encrypt(ek_j0);
}

static void gctr(UF_X uint8_t * UF_X buf, UF_X uint16_t len)
{
	UF_X uint8_t i;

	for (c_off = 0; c_off < len; c_off += 16) {
		for (i = 15; i >= 12; i--)
			if (++ctr[i])
				break;
		for (i = 0; i < 16; i++)
			ks[i] = ctr[i];
		uf_aes_encrypt(ks);
		c_n = (len - c_off) > 16 ? 16 : (uint8_t)(len - c_off);
		for (i = 0; i < c_n; i++)
			buf[c_off + i] ^= ks[i];
	}
}

/* Tag into s16 for ciphertext buf */
static void gcm_tag(UF_X const uint8_t * UF_X aad, UF_X uint8_t aad_len,
		    UF_X const uint8_t * UF_X buf, UF_X uint16_t len)
{
	UF_X uint8_t i;

	ghash_reset();
	ghash_update(aad, aad_len);
	ghash_update(buf, len);
	ghash_lengths((uint32_t)aad_len << 3, (uint32_t)len << 3);
	for (i = 0; i < 16; i++)
		s16[i] = gh_y[i] ^ ek_j0[i];
}

UF_X struct uf_gcm_args uf_gcm;

void uf_gcm_seal(void) UF_BANKED
{
	gcm_start(uf_gcm.iv);
	gctr(uf_gcm.buf, uf_gcm.len);
	gcm_tag(uf_gcm.aad, uf_gcm.aad_len, uf_gcm.buf, uf_gcm.len);
	uf_memcpy(uf_gcm.tag, s16, 16);
}

uint8_t uf_gcm_open(void) UF_BANKED
{
	gcm_start(uf_gcm.iv);
	gcm_tag(uf_gcm.aad, uf_gcm.aad_len, uf_gcm.buf, uf_gcm.len);
	if (!uf_memeq_ct(s16, uf_gcm.tag, 16))
		return 0;
	gctr(uf_gcm.buf, uf_gcm.len);
	return 1;
}

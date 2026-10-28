#include "ut.h"
#include "../uf_crypto.h"

static void kat_aes(void)
{
	uint8_t key[16], blk[16], exp[16];
	/* FIPS-197 Appendix C.1 */
	ut_unhex("000102030405060708090a0b0c0d0e0f", key, 16);
	ut_unhex("00112233445566778899aabbccddeeff", blk, 16);
	ut_unhex("69c4e0d86a7b0430d8cdb78070b4c55a", exp, 16);
	uf_aes_setkey(key);
	uf_aes_encrypt(blk);
	CHECK(!memcmp(blk, exp, 16));
	uf_aes_decrypt(blk);
	ut_unhex("00112233445566778899aabbccddeeff", exp, 16);
	CHECK(!memcmp(blk, exp, 16));
	/* FIPS-197 Appendix B */
	ut_unhex("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
	ut_unhex("3243f6a8885a308d313198a2e0370734", blk, 16);
	ut_unhex("3925841d02dc09fbdc118597196a0b32", exp, 16);
	uf_aes_setkey(key);
	uf_aes_encrypt(blk);
	CHECK(!memcmp(blk, exp, 16));
}

static void kat_cbc(void)
{
	/* SP 800-38A F.2.1, first two blocks (no padding involved) */
	uint8_t key[16], iv[16], buf[32], exp[32];
	ut_unhex("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
	ut_unhex("000102030405060708090a0b0c0d0e0f", iv, 16);
	ut_unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", buf, 32);
	ut_unhex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2", exp, 32);
	uf_aes_setkey(key);
	uf_cbc_encrypt(iv, buf, 32);
	CHECK(!memcmp(buf, exp, 32));
}

static uint8_t key[16], iv[16], aad[40], pt[5000], ct[5000], buf[5000];

static void gcm_seal(const uint8_t *v, const uint8_t *a, uint8_t al, uint8_t *b, uint16_t n, uint8_t *t)
{
	uf_gcm.iv = v; uf_gcm.aad = a; uf_gcm.aad_len = al; uf_gcm.buf = b; uf_gcm.len = n; uf_gcm.tag = t;
	uf_gcm_seal();
}

static uint8_t gcm_open(const uint8_t *v, const uint8_t *a, uint8_t al, uint8_t *b, uint16_t n, uint8_t *t)
{
	uf_gcm.iv = v; uf_gcm.aad = a; uf_gcm.aad_len = al; uf_gcm.buf = b; uf_gcm.len = n; uf_gcm.tag = t;
	return uf_gcm_open();
}

static void vectors(void)
{
	FILE *f = fopen("vectors.txt", "r");
	static char line[40000];
	char kind[8], k[64], v[64], a[128];
	static char p[20000], c[20000];
	int ngcm = 0, ncbc = 0;

	CHECK(f != NULL);
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		p[0] = c[0] = 0;
		if (!strncmp(line, "gcm", 3)) {
			size_t n, cn;
			uint8_t tag[16];
			int r = sscanf(line, "%7s %63s %63s %127s %19999s %19999s", kind, k, v, a, p, c);
			if (r == 5) { strcpy(c, p); p[0] = 0; }	/* empty plaintext */
			ut_unhex(k, key, 16); ut_unhex(v, iv, 16); ut_unhex(a, aad, 40);
			n = ut_unhex(p, pt, sizeof(pt));
			cn = ut_unhex(c, ct, sizeof(ct));
			CHECK_EQ(cn, n + 16);
			uf_aes_setkey(key);
			memcpy(buf, pt, n);
			gcm_seal(iv, aad, 40, buf, (uint16_t)n, tag);
			CHECK(!memcmp(buf, ct, n));
			CHECK(!memcmp(tag, ct + n, 16));
			/* open: correct tag decrypts */
			memcpy(buf, ct, n);
			CHECK(gcm_open(iv, aad, 40, buf, (uint16_t)n, ct + n) == 1);
			CHECK(!memcmp(buf, pt, n));
			/* tampered ciphertext, tag and AAD are rejected and leave buf untouched */
			if (n) {
				memcpy(buf, ct, n);
				buf[n / 2] ^= 0x01;
				CHECK(gcm_open(iv, aad, 40, buf, (uint16_t)n, ct + n) == 0);
				CHECK(buf[n / 2] == (uint8_t)(ct[n / 2] ^ 0x01));
			}
			memcpy(buf, ct, n);
			memcpy(tag, ct + n, 16); tag[15] ^= 0x80;
			CHECK(gcm_open(iv, aad, 40, buf, (uint16_t)n, tag) == 0);
			aad[39] ^= 1;
			CHECK(gcm_open(iv, aad, 40, buf, (uint16_t)n, ct + n) == 0);
			CHECK(!memcmp(buf, ct, n));
			ngcm++;
		} else if (!strncmp(line, "cbc", 3)) {
			size_t n, cn;
			uint16_t len;
			int r = sscanf(line, "%7s %63s %63s %19999s %19999s", kind, k, v, p, c);
			if (r == 4) { strcpy(c, p); p[0] = 0; }
			ut_unhex(k, key, 16); ut_unhex(v, iv, 16);
			n = ut_unhex(p, pt, sizeof(pt));
			cn = ut_unhex(c, ct, sizeof(ct));
			uf_aes_setkey(key);
			memcpy(buf, pt, n);
			len = uf_pkcs7_pad(buf, (uint16_t)n);
			CHECK_EQ(len, cn);
			uf_cbc_encrypt(iv, buf, len);
			CHECK(!memcmp(buf, ct, cn));
			CHECK_EQ(uf_cbc_decrypt(iv, buf, len), n);
			CHECK(!memcmp(buf, pt, n));
			ncbc++;
		}
	}
	fclose(f);
	CHECK_EQ(ngcm, 13);
	CHECK_EQ(ncbc, 7);
	/* explicit bad padding: 0x00, 0x11, and 0x02 preceded by 0x03 */
	{
		static const uint8_t last[3][2] = { { 0x00, 0x00 }, { 0x05, 0x11 }, { 0x03, 0x02 } };
		for (int i = 0; i < 3; i++) {
			memset(buf, 0x41, 32);
			buf[30] = last[i][0];
			buf[31] = last[i][1];
			uf_cbc_encrypt(iv, buf, 32);
			CHECK(uf_cbc_decrypt(iv, buf, 32) == UF_CBC_BAD);
		}
		memset(buf, 0x41, 32);
		buf[30] = 0x02; buf[31] = 0x02;
		uf_cbc_encrypt(iv, buf, 32);
		CHECK_EQ(uf_cbc_decrypt(iv, buf, 32), 30);
	}
	CHECK(uf_cbc_decrypt(iv, buf, 15) == UF_CBC_BAD);
	CHECK(uf_cbc_decrypt(iv, buf, 0) == UF_CBC_BAD);
}

void test_crypto(void)
{
	kat_aes();
	kat_cbc();
	vectors();
}

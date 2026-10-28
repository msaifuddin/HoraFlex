#include "ut.h"
#include "../uf_crypto.h"
#include "../uf_tnbu.h"

static const uint8_t mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static uint8_t key[16];
static uint8_t frame[8192], pt[8192], f2[8192];
static char line[40000], a[8], p[20000], h[20000];

/* Frames encoded by unifi-stubd's inform.EncodeJSON (zlib off) */
static void open_stubd_frames(void)
{
	FILE *f = fopen("tnbu_stubd.txt", "r");
	int n = 0;

	CHECK(f != NULL);
	if (!f)
		return;
	uf_aes_setkey(key);
	while (fgets(line, sizeof(line), f)) {
		size_t pn, fn;
		uint16_t r;
		int gcm;
		CHECK(sscanf(line, "%7s %19999s %19999s", a, p, h) == 3);
		gcm = !strcmp(a, "true");
		pn = ut_unhex(p, pt, sizeof(pt));
		fn = ut_unhex(h, frame, sizeof(frame));
		r = uf_tnbu_open(frame, (uint16_t)fn, mac, (uint8_t)gcm);
		CHECK_EQ(r, pn);
		CHECK(r != UF_TNBU_BAD && !memcmp(frame + UF_TNBU_HDR, pt, pn));
		CHECK_EQ(uf_tnbu_flags, gcm ? 0x09 : 0x01);
		n++;
	}
	fclose(f);
	CHECK_EQ(n, 6);
}

/* Frames for unifi-stubd's decoder, checked by interop.sh */
static void seal_for_stubd(void)
{
	FILE *f = fopen("tnbu_ours.txt", "w");
	static const char *payloads[] = { "{}", "{\"model\":\"USWED35\"}" };
	uint8_t iv[16];

	CHECK(f != NULL);
	if (!f)
		return;
	uf_aes_setkey(key);
	for (int gcm = 0; gcm < 2; gcm++)
		for (int i = 0; i < 2; i++) {
			uint16_t n = (uint16_t)strlen(payloads[i]), len;
			for (int j = 0; j < 16; j++)
				iv[j] = (uint8_t)(j * 7 + gcm + i);
			memcpy(frame + UF_TNBU_HDR, payloads[i], n);
			len = uf_tnbu_seal(frame, mac, iv, n, (uint8_t)gcm);
			CHECK_EQ(len, gcm ? 40 + n + 16 : 40 + ((n / 16) + 1) * 16);
			for (int j = 0; j < len; j++)
				fprintf(f, "%02x", frame[j]);
			fprintf(f, "\n");
		}
	fclose(f);
}

static uint16_t mk(int gcm)
{
	uint8_t iv[16] = { 1, 2, 3 };
	memcpy(frame + UF_TNBU_HDR, "{\"_type\":\"noop\"}", 16);
	return uf_tnbu_seal(frame, mac, iv, 16, (uint8_t)gcm);
}

static void rejects(void)
{
	uint16_t len;
	uint8_t other[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x56 };

	uf_aes_setkey(key);
	len = mk(1);
	memcpy(f2, frame, len);
	CHECK_EQ(uf_tnbu_open(f2, len, mac, 1), 16);

	memcpy(f2, frame, len);
	CHECK(uf_tnbu_open(f2, len, other, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_MAC);
	memcpy(f2, frame, len); f2[0] = 'X';
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_MAGIC);
	memcpy(f2, frame, len);
	CHECK(uf_tnbu_open(f2, len - 1, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_LENGTH);
	CHECK(uf_tnbu_open(f2, 39, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_SHORT);
	memcpy(f2, frame, len); f2[35] = 2;
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_VERSION);
	/* compressed payloads are refused before any decryption */
	memcpy(f2, frame, len); f2[15] |= UF_TNBU_ZLIB;
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_FLAGS);
	memcpy(f2, frame, len); f2[15] = 0;
	CHECK(uf_tnbu_open(f2, len, mac, 0) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_FLAGS);
	/* header flag change breaks the AAD and fails authentication */
	memcpy(f2, frame, len); f2[20] ^= 1;
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_AUTH);
	memcpy(f2, frame, len); f2[len - 1] ^= 1;
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_AUTH);
	CHECK(!memcmp(f2 + UF_TNBU_HDR, frame + UF_TNBU_HDR, 16));
	/* a CBC frame is refused once GCM is required */
	len = mk(0);
	memcpy(f2, frame, len);
	CHECK(uf_tnbu_open(f2, len, mac, 1) == UF_TNBU_BAD && uf_tnbu_err == UF_TERR_NEED_GCM);
	memcpy(f2, frame, len);
	CHECK_EQ(uf_tnbu_open(f2, len, mac, 0), 16);
	/* wrong key: CBC padding check catches it in practice */
	{
		uint8_t k2[16] = { 9 };
		uf_aes_setkey(k2);
		memcpy(f2, frame, len);
		CHECK(uf_tnbu_open(f2, len, mac, 0) == UF_TNBU_BAD);
	}
}

void test_tnbu(void)
{
	ut_unhex("ba86f2bbe107c7c57eb5f2690775c712", key, 16);
	open_stubd_frames();
	seal_for_stubd();
	rejects();
}

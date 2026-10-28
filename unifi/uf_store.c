/*
 * Two-slot flash records for the UniFi adoption state
 *
 * Record layout (big endian):
 *   0  "UFS1"        4  version 2     5  flags       6  plan size (2)
 *   8  sequence (4)  12 epoch (8)     20 authkey (16)
 *   36 controller IPv4 (4)            40 controller port (2)
 *   42 cfgversion (33, NUL-terminated)
 *   76 reported firmware version (20, NUL-terminated, "" = built-in)
 *   96 plan: struct uf_plan as the firmware lays it out (plan size bytes)
 *   508 CRC-32 of bytes 0..507
 */
#include "uf_store.h"
#include "uf_hw.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

#define REC_VERSION	2
#define PLAN_OFF	96
#define CRC_OFF		(UF_REC_LEN - 4)
#define SCAN_CHUNK	256

UF_X struct uf_state uf_state;
UF_X uint8_t uf_store_slot;
UF_X uint8_t uf_store_foreign;

static UF_X uint8_t rec[UF_REC_LEN];
static UF_X uint8_t chk[SCAN_CHUNK];
static UF_X uint32_t s_crc;
static UF_X uint32_t s_addr;
static UF_X uint32_t s_seq[2];
static UF_X uint8_t s_valid[2];
static UF_X uint16_t s_i;

/* CRC-32 (IEEE, reflected) on four bytes, least significant first: no 32-bit temporaries */
static UF_X uint8_t crc[4];

uint32_t uf_crc32(UF_X const uint8_t * UF_X p, UF_X uint16_t len) UF_BANKED
{
	UF_X uint8_t b, lsb;

	crc[0] = crc[1] = crc[2] = crc[3] = 0xff;
	while (len--) {
		crc[0] ^= *p++;
		for (b = 0; b < 8; b++) {
			lsb = crc[0] & 1;
			crc[0] = (uint8_t)((crc[0] >> 1) | (crc[1] << 7));
			crc[1] = (uint8_t)((crc[1] >> 1) | (crc[2] << 7));
			crc[2] = (uint8_t)((crc[2] >> 1) | (crc[3] << 7));
			crc[3] >>= 1;
			if (lsb) {
				crc[0] ^= 0x20;
				crc[1] ^= 0x83;
				crc[2] ^= 0xb8;
				crc[3] ^= 0xed;
			}
		}
	}
	s_crc = (uint8_t)~crc[3];
	s_crc = (s_crc << 8) | (uint8_t)~crc[2];
	s_crc = (s_crc << 8) | (uint8_t)~crc[1];
	s_crc = (s_crc << 8) | (uint8_t)~crc[0];
	return s_crc;
}

static void put32(UF_X uint8_t * UF_X p, UF_X uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t get32(UF_X const uint8_t * UF_X p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint16_t)p[2] << 8) | p[3];
}

#if PLAN_OFF + 320 > CRC_OFF
#error "store record too small"
#endif

void uf_store_encode(UF_X uint8_t * UF_X r) UF_BANKED
{
	uf_memset(r, 0, UF_REC_LEN);
	r[0] = 'U'; r[1] = 'F'; r[2] = 'S'; r[3] = '1';
	r[4] = REC_VERSION;
	r[5] = uf_state.flags;
	if (uf_state.flags & UF_SF_PLAN) {
		r[6] = (uint8_t)(sizeof(uf_live) >> 8);
		r[7] = (uint8_t)sizeof(uf_live);
		uf_memcpy(r + PLAN_OFF, (UF_X uint8_t *)&uf_live, sizeof(uf_live));
	}
	uf_memcpy(r + 42, (UF_X uint8_t *)uf_state.cfgversion, UF_CFGVER_LEN);
	r[42 + UF_CFGVER_LEN - 1] = 0;
	uf_memcpy(r + 76, (UF_X uint8_t *)uf_state.fwver, UF_FWVER_LEN);
	r[76 + UF_FWVER_LEN - 1] = 0;
	put32(r + 8, uf_state.seq);
	uf_memcpy(r + 12, uf_state.epoch, 8);
	uf_memcpy(r + 20, uf_state.authkey, 16);
	uf_memcpy(r + 36, uf_state.ctrl_ip, 4);
	r[40] = (uint8_t)(uf_state.ctrl_port >> 8);
	r[41] = (uint8_t)uf_state.ctrl_port;
	put32(r + CRC_OFF, uf_crc32(r, CRC_OFF));
}

static uint8_t valid(UF_X const uint8_t * UF_X r)
{
	return r[0] == 'U' && r[1] == 'F' && r[2] == 'S' && r[3] == '1' && r[4] == REC_VERSION
		&& uf_crc32(r, CRC_OFF) == get32(r + CRC_OFF);
}

/* Fills uf_state from a record. Returns 0 when the record is not valid */
uint8_t uf_store_decode(UF_X const uint8_t * UF_X r) UF_BANKED
{
	if (!valid(r))
		return 0;
	uf_state.flags = r[5];
	uf_state.seq = get32(r + 8);
	uf_memcpy(uf_state.epoch, r + 12, 8);
	uf_memcpy(uf_state.authkey, r + 20, 16);
	uf_memcpy(uf_state.ctrl_ip, r + 36, 4);
	uf_state.ctrl_port = ((uint16_t)r[40] << 8) | r[41];
	uf_memcpy((UF_X uint8_t *)uf_state.cfgversion, r + 42, UF_CFGVER_LEN);
	uf_state.cfgversion[UF_CFGVER_LEN - 1] = 0;
	uf_memcpy((UF_X uint8_t *)uf_state.fwver, r + 76, UF_FWVER_LEN);
	uf_state.fwver[UF_FWVER_LEN - 1] = 0;
	/* A plan laid out by a different build is dropped, not misread */
	if ((uf_state.flags & UF_SF_PLAN) && (((uint16_t)r[6] << 8) | r[7]) == sizeof(uf_plan))
		uf_memcpy((UF_X uint8_t *)&uf_plan, r + PLAN_OFF, sizeof(uf_plan));
	else
		uf_state.flags &= ~UF_SF_PLAN;
	return 1;
}

static uint32_t slot_addr(UF_X uint8_t slot)
{
	return slot ? UF_STORE_SLOT_B : UF_STORE_SLOT_A;
}

static void read_slot(UF_X uint8_t slot)
{
	if (!uh_flash_read(slot_addr(slot), rec, UF_REC_LEN))
		uf_memset(rec, 0, UF_REC_LEN);
}

void uf_store_load(void) UF_BANKED
{
	UF_X uint8_t i;

	for (i = 0; i < 2; i++) {
		read_slot(i);
		s_valid[i] = valid(rec);
		s_seq[i] = s_valid[i] ? get32(rec + 8) : 0;
	}
	uf_store_slot = 0xff;
	if (s_valid[0] && (!s_valid[1] || s_seq[0] >= s_seq[1]))
		uf_store_slot = 0;
	else if (s_valid[1])
		uf_store_slot = 1;

	uf_memset((UF_X uint8_t *)&uf_state, 0, sizeof(uf_state));
	if (uf_store_slot != 0xff) {
		read_slot(uf_store_slot);
		uf_store_decode(rec);
	}
}

/*
 * A sector may be erased only if everything past the record area is blank.
 * The record area itself may hold anything: an old record, a torn write,
 * or the remains of an interrupted erase.
 */
static uint8_t sector_is_ours(void)
{
	UF_X uint16_t j;

	for (s_i = 0; s_i < 0x1000; s_i += SCAN_CHUNK) {
		if (!uh_flash_read(s_addr + s_i, chk, SCAN_CHUNK))
			return 0;
		if (s_i < UF_REC_LEN)
			continue;
		for (j = 0; j < SCAN_CHUNK; j++)
			if (chk[j] != 0xff)
				return 0;
	}
	return 1;
}

uint8_t uf_store_commit(void) UF_BANKED
{
	UF_X uint8_t target;
	UF_X uint16_t i;

	if (uf_store_foreign)
		return 0;
	target = (uf_store_slot == 0) ? 1 : 0;
	s_addr = slot_addr(target);
	if (!sector_is_ours()) {
		uf_store_foreign = 1;
		uh_log("unifi: store sector holds unknown data, writes disabled: ", (uint16_t)(s_addr >> 12));
		return 0;
	}
	uf_state.seq++;
	uf_store_encode(rec);
	if (!uh_flash_erase(s_addr))
		return 0;
	for (s_i = 0; s_i < UF_REC_LEN; s_i += SCAN_CHUNK) {
		if (!uh_flash_read(s_addr + s_i, chk, SCAN_CHUNK))
			return 0;
		for (i = 0; i < SCAN_CHUNK; i++)
			if (chk[i] != 0xff)
				return 0;
	}
	if (!uh_flash_write(s_addr, rec, UF_REC_LEN))
		return 0;
	for (s_i = 0; s_i < UF_REC_LEN; s_i += SCAN_CHUNK) {
		if (!uh_flash_read(s_addr + s_i, chk, SCAN_CHUNK))
			return 0;
		for (i = 0; i < SCAN_CHUNK; i++)
			if (chk[i] != rec[s_i + i])
				return 0;
	}
	uf_store_slot = target;
	return 1;
}

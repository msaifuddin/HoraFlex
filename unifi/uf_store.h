/*
 * Persistent adoption state in two flash sectors.
 *
 * Each sector holds one 512-byte record with a sequence number and CRC-32:
 * adoption state, the GCM nonce epoch, and the switch configuration last
 * applied from the controller with its cfgversion.
 * A commit always writes the sector that does not hold the newest valid
 * record, so an interrupted erase or write leaves the previous record intact.
 * A sector is only erased when everything in it past the record area is
 * blank: unknown data in the chosen sectors stops all writes.
 */
#ifndef _UF_STORE_H_
#define _UF_STORE_H_

#include "uf_port.h"
#include "uf_cfg.h"

/*
 * In the middle of the blank 0x101000-0x1f8fff range of a 2 MiB flash
 * (HC-SWTGW215AS dump): clear of the image staging area of web updates,
 * which writes a terminating byte at 0x100000, and of the vendor logo
 * (0x1f9000) and factory data with the MAC (0x1fc000).
 */
#ifndef UF_STORE_SLOT_A
#define UF_STORE_SLOT_A	0x180000UL
#endif
#ifndef UF_STORE_SLOT_B
#define UF_STORE_SLOT_B	0x181000UL
#endif

#define UF_REC_LEN	512

#define UF_SF_ADOPTED	0x01
#define UF_SF_GCM	0x02
#define UF_SF_PLAN	0x04	/* record holds an applied plan and its cfgversion */

struct uf_state {
	uint8_t flags;
	uint32_t seq;
	uint8_t epoch[8];	/* GCM nonce epoch, big endian, only ever grows */
	uint8_t authkey[16];
	uint8_t ctrl_ip[4];
	uint16_t ctrl_port;
	char cfgversion[UF_CFGVER_LEN];
};

extern UF_X struct uf_state uf_state;
/* Slot the current record came from: 0 = A, 1 = B, 0xff = none valid */
extern UF_X uint8_t uf_store_slot;
/* Set when a slot holds data that is not ours; no writes happen then */
extern UF_X uint8_t uf_store_foreign;

/* Loads the newest valid record, or defaults (unadopted, epoch 0). A stored
 * plan is loaded into uf_plan (UF_SF_PLAN set in uf_state.flags) */
void uf_store_load(void) UF_BANKED;
/* Writes uf_state, and uf_live when UF_SF_PLAN is set, as the next record.
 * Returns 1 when written and verified */
uint8_t uf_store_commit(void) UF_BANKED;

/* Exposed for tests */
uint32_t uf_crc32(UF_X const uint8_t * UF_X p, UF_X uint16_t len) UF_BANKED;
void uf_store_encode(UF_X uint8_t * UF_X rec) UF_BANKED;
uint8_t uf_store_decode(UF_X const uint8_t * UF_X rec) UF_BANKED;

#endif

/*
 * Discovery packet and inform JSON for the UniFi agent
 */
#ifndef _UF_MSG_H_
#define _UF_MSG_H_

#include "uf_port.h"
#include "uf_cfg.h"

struct uf_tport {
	uint8_t enabled;
	uint8_t up;
	uint16_t speed;		/* Mbit/s, 0 when down */
	uint8_t full_duplex;
	uint8_t rx_pkts[8];	/* big endian 64-bit */
	uint8_t tx_pkts[8];
	uint32_t rx_err;
	uint32_t tx_err;
	uint16_t macs;
};

struct uf_tele {
	struct uf_tport p[UF_NPORTS];
};

/* What the device says about itself */
struct uf_self {
	uint8_t mac[6];
	uint8_t ip[4];
	uint32_t uptime;
	uint32_t time;		/* unix seconds, 0 when unknown */
	uint8_t adopted;
	uint8_t uplink;		/* UniFi port, 0 when unknown */
	uint8_t inform_ip[4];
	uint16_t inform_port;
	char cfgversion[UF_CFGVER_LEN];	/* "" reports "?" */
	char hostname[UF_NAME_LEN];
	char version[UF_FWVER_LEN];	/* firmware version reported to the controller */
	uint32_t disc_seq;
};

extern UF_X struct uf_tele uf_tele;
extern UF_X struct uf_self uf_self;

/* Returns the discovery packet length */
uint16_t uf_disc_build(UF_X uint8_t * UF_X buf) UF_BANKED;
/* Writes the inform JSON into buf[0..max). Returns its length, 0 if it did not fit */
uint16_t uf_inform_build(UF_X uint8_t * UF_X buf, UF_X uint16_t max) UF_BANKED;

#endif

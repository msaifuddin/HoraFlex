/*
 * UniFi device agent: discovery, inform cycle, adoption and provisioning.
 *
 * The platform glue calls uf_agent_poll() from the main loop, drives the TCP
 * connection with uf_xfer/uf_resp, and reports the end of each exchange with
 * uf_agent_done().
 */
#ifndef _UF_AGENT_H_
#define _UF_AGENT_H_

#include "uf_port.h"

#ifndef UF_RX_SIZE
#define UF_RX_SIZE	8192
#endif
#ifndef UF_TX_SIZE
#define UF_TX_SIZE	5120
#endif

/* Agent phases */
#define UF_AP_OFF	0	/* disabled */
#define UF_AP_IDLE	1	/* waiting for the next inform */
#define UF_AP_BUSY	2	/* exchange in progress */

/* Outcome of an exchange, for uf_agent_done() */
#define UF_XR_OK	0	/* response complete (uf_resp) */
#define UF_XR_CONNECT	1	/* could not connect */
#define UF_XR_ABORT	2	/* reset, timeout or protocol error */

#define UF_INTERVAL_MIN	5
#define UF_INTERVAL_MAX	300
#define UF_BACKOFF_MAX	120
#define UF_XFER_TIMEOUT	20

struct uf_agent {
	uint8_t phase;
	uint8_t enabled;
	/* UDP discovery makes the controller adopt over SSH, which this device
	 * does not have (UCG, Network 10.6.109: "RPC adopt failed"). Without it
	 * the controller adopts through the inform channel. Off by default. */
	uint8_t discovery;
	uint32_t next_inform;
	uint32_t next_disc;
	uint32_t busy_since;
	uint16_t interval;
	uint16_t backoff;
	uint8_t epoch_ok;	/* this boot's nonce epoch is committed */
	uint8_t nonce_ctr[8];
	uint8_t applied;	/* a system_cfg was applied this boot */
	uint32_t time_base;	/* controller UTC seconds minus uptime, 0 when unknown */
	/* counters and last results for status and tests */
	uint16_t n_inform;
	uint16_t n_ok;
	uint16_t n_fail;
	uint16_t n_auth_fail;
	uint16_t n_oversize;
	uint32_t last_oversize;
	uint16_t max_resp;
	uint8_t last_cfg_err;
	uint8_t last_apply_err;
	uint16_t last_status;
	uint8_t last_terr;
};

extern UF_X struct uf_agent uf_agent;
extern UF_X uint8_t uf_rx[UF_RX_SIZE];
extern UF_X uint8_t uf_tx[UF_TX_SIZE];

/* Loads persistent state; ctrl_ip/port is the configured controller for an unadopted device */
void uf_agent_init(UF_X const uint8_t * UF_X mac, UF_X const uint8_t * UF_X ctrl_ip, UF_X uint16_t ctrl_port) UF_BANKED;
void uf_agent_poll(void) UF_BANKED;
void uf_agent_done(UF_X uint8_t result) UF_BANKED;
/* Clears the adoption locally (CLI) */
uint8_t uf_agent_forget(void) UF_BANKED;
/* The glue aborts the exchange when this returns 1 */
uint8_t uf_agent_expired(void) UF_BANKED;

#endif

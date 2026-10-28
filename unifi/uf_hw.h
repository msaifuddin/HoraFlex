/*
 * Platform hooks the UniFi agent needs. uf_hw.c implements them on the RTL837x;
 * the host tests provide fakes.
 */
#ifndef _UF_HW_H_
#define _UF_HW_H_

#include "uf_port.h"

/* SPI flash: return 1 on success */
uint8_t uh_flash_read(UF_X uint32_t addr, UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;
uint8_t uh_flash_erase(UF_X uint32_t addr) UF_BANKED;	/* 4 KiB sector */
uint8_t uh_flash_write(UF_X uint32_t addr, UF_X const uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;

/* Seconds since boot */
uint32_t uh_uptime(void) UF_BANKED;
/* Unpredictable bytes for CBC IVs (not used for GCM nonces) */
void uh_random(UF_X uint8_t * UF_X buf, UF_X uint8_t len) UF_BANKED;
void uh_log(UF_C const char * UF_X msg, UF_X uint16_t val) UF_BANKED;
void uh_log_x(UF_C const char * UF_X msg, UF_X const char * UF_X s) UF_BANKED;

/* Management IP when the interface is usable, else returns 0 */
uint8_t uh_ip(UF_X uint8_t * UF_X ip) UF_BANKED;

/* Fills uf_tele from the switch */
void uh_telemetry(void) UF_BANKED;
/* UniFi port (1..5) the controller's packets arrive on, 0 when unknown */
uint8_t uh_uplink(void) UF_BANKED;
/* Programs uf_plan into the switch. Returns 0 or a UF_AE_* error */
uint8_t uh_apply(void) UF_BANKED;

/* Transport: open a TCP connection; results come back through uf_agent_* */
uint8_t uh_tcp_open(UF_X const uint8_t * UF_X ip, UF_X uint16_t port) UF_BANKED;
/* Ask for a UDP discovery broadcast at the next opportunity */
void uh_udp_discovery(void) UF_BANKED;

#define UF_AE_NONE	0
#define UF_AE_VERIFY	1	/* hardware read-back differs from the plan */
#define UF_AE_TABLE	2	/* hardware table/scan failed */

#endif

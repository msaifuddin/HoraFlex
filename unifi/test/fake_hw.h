/* Host fakes for uf_hw.h, controlled by the tests */
#ifndef FAKE_HW_H
#define FAKE_HW_H
#include "ut.h"
#include "../uf_hw.h"
#include "../uf_cfg.h"

#define FK_FLASH_SIZE	0x200000

extern uint8_t fk_flash[FK_FLASH_SIZE];
extern long fk_write_budget;	/* bytes that may still be programmed, -1 = unlimited */
extern int fk_erase_fail;
extern int fk_erases, fk_writes;
extern uint32_t fk_uptime;
extern uint8_t fk_ip[4];
extern int fk_ip_ready;
extern uint8_t fk_uplink;
extern uint8_t fk_apply_result;
extern int fk_apply_count;
extern struct uf_plan fk_applied;
extern int fk_tcp_ok, fk_tcp_opens;
extern uint8_t fk_tcp_ip[4];
extern uint16_t fk_tcp_port;
extern int fk_disc_requests;
extern char fk_log[8192];

void fk_reset_flash(void);
#endif

/* Host fakes for uf_hw.h */
#include "fake_hw.h"
#include "../uf_msg.h"
#include "../uf_cfg.h"

uint8_t fk_flash[FK_FLASH_SIZE];
long fk_write_budget = -1;
int fk_erase_fail;
int fk_erases, fk_writes;
uint32_t fk_uptime;
uint8_t fk_ip[4] = { 192, 0, 2, 20 };
int fk_ip_ready = 1;
uint8_t fk_uplink = 1;
uint8_t fk_apply_result;
int fk_apply_count;
struct uf_plan fk_applied;
int fk_tcp_ok = 1;
int fk_tcp_opens;
uint8_t fk_tcp_ip[4];
uint16_t fk_tcp_port;
int fk_disc_requests;
uint8_t fk_rand;
char fk_log[8192];

void fk_reset_flash(void)
{
	memset(fk_flash, 0xff, sizeof(fk_flash));
	fk_write_budget = -1;
	fk_erase_fail = 0;
	fk_erases = fk_writes = 0;
}

uint8_t uh_flash_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
	if (addr + len > FK_FLASH_SIZE)
		return 0;
	memcpy(buf, fk_flash + addr, len);
	return 1;
}

uint8_t uh_flash_erase(uint32_t addr)
{
	fk_erases++;
	if (addr & 0xfff || addr >= FK_FLASH_SIZE)
		return 0;
	if (fk_erase_fail) {
		/* power lost half way through: half the sector is erased */
		memset(fk_flash + addr, 0xff, 0x800);
		return 0;
	}
	memset(fk_flash + addr, 0xff, 0x1000);
	return 1;
}

/* NOR programming only clears bits. fk_write_budget simulates power loss */
uint8_t uh_flash_write(uint32_t addr, const uint8_t *buf, uint16_t len)
{
	fk_writes++;
	if (addr + len > FK_FLASH_SIZE)
		return 0;
	for (uint16_t i = 0; i < len; i++) {
		if (fk_write_budget == 0)
			return 0;
		if (fk_write_budget > 0)
			fk_write_budget--;
		fk_flash[addr + i] &= buf[i];
	}
	return 1;
}

uint32_t uh_uptime(void) { return fk_uptime; }

void uh_random(uint8_t *buf, uint8_t len)
{
	while (len--)
		*buf++ = (uint8_t)(fk_rand++ * 37 + 11);
}

void uh_log(const char *msg, uint16_t val)
{
	size_t n = strlen(fk_log);
	snprintf(fk_log + n, sizeof(fk_log) - n, "%s%u\n", msg, val);
	if (getenv("UT_VERBOSE"))
		fprintf(stderr, "LOG %s%u\n", msg, val);
	if (strlen(fk_log) > sizeof(fk_log) - 512)
		fk_log[0] = 0;
}

void uh_log_x(const char *msg, const char *s)
{
	size_t n = strlen(fk_log);
	snprintf(fk_log + n, sizeof(fk_log) - n, "%s%s\n", msg, s);
	if (getenv("UT_VERBOSE"))
		fprintf(stderr, "LOG %s%s\n", msg, s);
	if (strlen(fk_log) > sizeof(fk_log) - 512)
		fk_log[0] = 0;
}

uint8_t uh_ip(uint8_t *ip)
{
	memcpy(ip, fk_ip, 4);
	return (uint8_t)fk_ip_ready;
}

void uh_telemetry(void)
{
	for (int i = 0; i < UF_NPORTS; i++) {
		struct uf_tport *p = &uf_tele.p[i];
		memset(p, 0, sizeof(*p));
		p->enabled = 1;
		p->up = (i == 0 || i == 2);
		p->speed = i == 0 ? 2500 : 1000;
		p->full_duplex = 1;
		p->rx_pkts[7] = (uint8_t)(10 + i);
		p->tx_pkts[0] = (i == 2) ? 0x01 : 0;	/* > 2^56, exercises 64-bit decimal */
		p->rx_err = (uint32_t)i;
		p->macs = (uint16_t)i;
	}
}

uint8_t uh_uplink(void) { return fk_uplink; }

uint8_t uh_apply(void)
{
	fk_apply_count++;
	if (!fk_apply_result)
		fk_applied = uf_plan;
	return fk_apply_result;
}

uint8_t uh_tcp_open(const uint8_t *ip, uint16_t port)
{
	fk_tcp_opens++;
	memcpy(fk_tcp_ip, ip, 4);
	fk_tcp_port = port;
	return (uint8_t)fk_tcp_ok;
}

void uh_udp_discovery(void) { fk_disc_requests++; }

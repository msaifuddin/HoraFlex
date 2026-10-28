/*
 * RTL837x platform glue for the UniFi agent: flash, clock, logging, switch
 * telemetry and configuration, uIP transport, CLI and diagnostics.
 *
 * UniFi port p (1..5) is front-panel port p, i.e. logical port
 * machine.phys_to_log_port[p - 1]. Other logical ports, the SFP+ in
 * particular, are never reported and never changed by the controller; VLAN
 * writes keep their existing membership.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_flash.h"
#include "rtl837x_port.h"
#include "rtl837x_phy.h"
#include "phy.h"
#include "rtl837x_sfr.h"
#include "machine.h"
#include "dhcp.h"
#include "uip/uip.h"
#include "uf_hw.h"
#include "uf_agent.h"
#include "uf_cfg.h"
#include "uf_msg.h"
#include "uf_store.h"
#include "uf_http.h"
#include "uf_glue.h"

#pragma codeseg BANK5
#pragma constseg BANK5

#define DISC_PORT	10001

extern __code const struct machine machine;
extern __xdata uint8_t sfr_data[4];
extern volatile __xdata uint32_t ticks;
extern __xdata struct flash_region_t flash_region;
extern __xdata struct dhcp_state dhcp_state;
extern __xdata char port_names[9][PORT_NAME_SIZE];
extern __xdata uint8_t cmd_buffer[];
extern __xdata uint8_t cmd_words_len;
extern __xdata uint8_t cmd_words_b[];

__xdata uint8_t uf_rx_port = 0xff;	/* ingress port of the frame being processed */

static __xdata struct uip_conn * __xdata uf_conn;
static __xdata struct uip_udp_conn * __xdata uf_udp;
static __xdata uint8_t disc_pending;
static __xdata uint8_t uplink_log = 0xff;	/* logical port the controller talks through */
static __xdata uint8_t uplink_forced;		/* UniFi port set by CLI, 0 = automatic */
static __xdata uint8_t ctrl_ip[4];
static __xdata uint16_t ctrl_port = 8080;
static __xdata uint8_t g_started;

/* Diagnostics: longest inform cycle, response handling and apply, in 5 ms ticks.
 * Stack depth is measured with the HEALTH=1 build ("health" command). */
__xdata uint16_t uf_t_build_max, uf_t_open_max, uf_t_apply_max, uf_t_last_open;

/* Scratch kept at file scope so nothing spills into internal RAM */
static __xdata uint8_t h_i, h_l, h_b;
static __xdata uint16_t h_vid;
static __xdata uint16_t h_mem, h_tag, h_keep_mem, h_keep_tag;
static __xdata uint32_t h_m;

/* The tick ISR can update ticks between the two byte loads; read until stable */
static uint16_t ticks16(void)
{
	__xdata uint16_t a = (uint16_t)ticks, b;

	do {
		b = a;
		a = (uint16_t)ticks;
	} while (a != b);
	return a;
}

uint8_t uh_flash_read(__xdata uint32_t addr, __xdata uint8_t * __xdata buf, __xdata uint16_t len) __banked
{
	flash_region.addr = addr;
	flash_region.len = len;
	flash_read_bulk(buf);
	return 1;
}

uint8_t uh_flash_erase(__xdata uint32_t addr) __banked
{
	/* The store slots are the only sectors this agent ever erases */
	if (addr != UF_STORE_SLOT_A && addr != UF_STORE_SLOT_B)
		return 0;
	flash_region.addr = addr;
	flash_sector_erase();
	return 1;
}

uint8_t uh_flash_write(__xdata uint32_t addr, __xdata const uint8_t * __xdata buf, __xdata uint16_t len) __banked
{
	if ((addr & ~0xfffUL) != UF_STORE_SLOT_A && (addr & ~0xfffUL) != UF_STORE_SLOT_B)
		return 0;
	flash_region.addr = addr;
	flash_region.len = len;
	flash_write_bytes((__xdata uint8_t *)buf);
	return 1;
}

uint32_t uh_uptime(void) __banked
{
	reg_read_m(RTL837X_REG_SEC_COUNTER);
	return ((uint32_t)sfr_data[0] << 24) | ((uint32_t)sfr_data[1] << 16) | ((uint16_t)sfr_data[2] << 8) | sfr_data[3];
}

void uh_random(__xdata uint8_t * __xdata buf, __xdata uint8_t len) __banked
{
	while (len) {
		get_random_32();
		for (h_i = 0; h_i < 4 && len; h_i++, len--)
			*buf++ = sfr_data[h_i] ^ (uint8_t)ticks;
	}
}

void uh_log(__code const char * __xdata msg, __xdata uint16_t val) __banked
{
	print_string(msg);
	itoa_short(val);
	write_char('\n');
}

void uh_log_x(__code const char * __xdata msg, __xdata const char * __xdata s) __banked
{
	print_string(msg);
	print_string_x((__xdata char *)s);
	write_char('\n');
}

uint8_t uh_ip(__xdata uint8_t * __xdata ip) __banked
{
	memcpy(ip, (__xdata void *)uip_hostaddr, 4);
	if (!(ip[0] | ip[1] | ip[2] | ip[3]))
		return 0;
	return !dhcp_state.state || dhcp_state.state == DHCP_LEASING;
}

static uint8_t log_port(__xdata uint8_t p)
{
	return machine.phys_to_log_port[p - 1];
}

static uint8_t link_speed_code(void)
{
	reg_read_m(RTL837X_REG_LINKS_STS);
	if (!((sfr_data[(h_l >> 3) + 1] >> (h_l & 7)) & 1))
		return 0xff;
	if (h_l < 8)
		reg_read_m(RTL837X_REG_LINKS);
	else
		reg_read_m(RTL837X_REG_LINKS_89);
	h_b = sfr_data[3 - ((h_l & 7) >> 1)];
	return (h_l & 1) ? h_b >> 4 : h_b & 0xf;
}

static void stat_be64(__xdata uint8_t * __xdata dst)
{
	reg_read_m(RTL837X_STAT_V_HIGH);
	dst[0] = sfr_data[0]; dst[1] = sfr_data[1]; dst[2] = sfr_data[2]; dst[3] = sfr_data[3];
	reg_read_m(RTL837X_STAT_V_LOW);
	dst[4] = sfr_data[0]; dst[5] = sfr_data[1]; dst[6] = sfr_data[2]; dst[7] = sfr_data[3];
}

static uint32_t stat32(__xdata uint16_t reg)
{
	reg_read_m(reg);
	return ((uint32_t)sfr_data[0] << 24) | ((uint32_t)sfr_data[1] << 16) | ((uint16_t)sfr_data[2] << 8) | sfr_data[3];
}

void uh_telemetry(void) __banked
{
	__xdata struct uf_tport * __xdata t;
	__xdata uint8_t code;

	for (h_i = 0; h_i < UF_NPORTS; h_i++) {
		t = &uf_tele.p[h_i];
		h_l = log_port(h_i + 1);
		t->enabled = uf_live.nv == 0xff || (uf_live.enable & (1 << h_i));
		code = link_speed_code();
		t->up = code != 0xff;
		switch (code) {
		case 0: t->speed = 10; break;
		case 1: t->speed = 100; break;
		case 2: t->speed = 1000; break;
		case 5: t->speed = 2500; break;
		default: t->speed = 0; break;
		}
		/* 1G and above are full duplex only; 10/100 duplex is not read yet */
		t->full_duplex = t->up;
		STAT_GET(STAT_COUNTER_TX_PKTS, h_l);
		stat_be64(t->tx_pkts);
		STAT_GET(STAT_COUNTER_RX_PKTS, h_l);
		stat_be64(t->rx_pkts);
		STAT_GET(STAT_COUNTER_ERR_PKTS, h_l);
		t->tx_err = stat32(RTL837X_STAT_V_LOW);
		t->rx_err = stat32(RTL837X_STAT_V_HIGH);
	}
}

uint8_t uh_uplink(void) __banked
{
	if (uplink_forced)
		return uplink_forced;
	for (h_i = 1; h_i <= UF_NPORTS; h_i++)
		if (log_port(h_i) == uplink_log)
			return h_i;
	return 0;
}

/* Membership of VLAN h_vid as logical-port masks, into h_mem/h_tag */
static void vlan_read(void)
{
	vlan_get(h_vid);
	h_m = ((uint32_t)sfr_data[0] << 24) | ((uint32_t)sfr_data[1] << 16) | ((uint16_t)sfr_data[2] << 8) | sfr_data[3];
	h_mem = (uint16_t)h_m & 0x3ff;
	h_tag = h_mem & ~((uint16_t)(h_m >> 10) & 0x3ff);
}

/* Logical mask of the five copper ports */
static uint16_t copper_mask(void)
{
	__xdata uint16_t m = 0;

	for (h_i = 1; h_i <= UF_NPORTS; h_i++)
		m |= (uint16_t)1 << log_port(h_i);
	return m;
}

/* UniFi port mask to logical port mask */
static uint16_t to_logical(__xdata uint8_t mask)
{
	__xdata uint16_t m = 0;

	for (h_i = 0; h_i < UF_NPORTS; h_i++)
		if (mask & (1 << h_i))
			m |= (uint16_t)1 << log_port(h_i + 1);
	return m;
}

/*
 * Writes VLAN h_vid with the copper membership given in UniFi masks, keeping
 * every other port's current membership (SFP+, CPU). An empty result deletes
 * the VLAN, except VLAN 1.
 */
static void vlan_write(__xdata uint8_t member, __xdata uint8_t tagged)
{
	__xdata uint16_t cu = copper_mask();

	vlan_read();
	h_keep_mem = h_mem & ~cu & ~((uint16_t)1 << CPU_PORT);
	h_keep_tag = h_tag & ~cu & ~((uint16_t)1 << CPU_PORT);
	vlan_settings.vlan = h_vid;
	vlan_settings.members = h_keep_mem | to_logical(member);
	vlan_settings.tagged = h_keep_tag | to_logical(tagged);
	if (!vlan_settings.members && h_vid != 1)
		vlan_delete(h_vid);
	else
		vlan_create();
}

static uint8_t vlan_check(__xdata uint8_t member, __xdata uint8_t tagged)
{
	__xdata uint16_t cu = copper_mask();

	vlan_read();
	return (h_mem & cu) == to_logical(member) && (h_tag & cu) == to_logical(tagged);
}

static uint8_t in_plan(void)
{
	for (h_b = 0; h_b < uf_plan.nv; h_b++)
		if (uf_plan.vid[h_b] == h_vid)
			return 1;
	return 0;
}

uint8_t uh_apply(void) __banked
{
	__xdata uint8_t i, bit;
	__xdata uint16_t t0 = ticks16();

	/* 1. VLANs in the plan */
	for (i = 0; i < uf_plan.nv; i++) {
		h_vid = uf_plan.vid[i];
		vlan_write(uf_plan.member[i], uf_plan.tagged[i]);
	}
	/* 2. Copper membership of VLANs no longer in the plan. Before the first
	 * apply of a boot every VLAN is checked, later only the ones applied last. */
	if (uf_live.nv == 0xff) {
		for (h_vid = 1; h_vid < 4095; h_vid++) {
			if (in_plan())
				continue;
			vlan_read();
			if (h_mem & copper_mask())
				vlan_write(0, 0);
		}
	} else {
		for (i = 0; i < uf_live.nv; i++) {
			h_vid = uf_live.vid[i];
			if (!in_plan())
				vlan_write(0, 0);
		}
	}
	/* 3. Port settings */
	for (i = 0; i < UF_NPORTS; i++) {
		bit = 1 << i;
		h_l = log_port(i + 1);
		port_pvid_set(h_l, uf_plan.pvid[i]);
		if (uf_plan.has_name & bit) {
			memcpy(port_names[h_l], uf_plan.name[i], PORT_NAME_SIZE);
			port_names[h_l][PORT_NAME_SIZE - 1] = 0;
		}
		/* Only touch the PHY on a change: forcing autoneg again drops the link */
		if ((uf_plan.enable & bit) != (uf_live.nv == 0xff ? bit : (uf_live.enable & bit))) {
			phy_settings.port = h_l;
			phy_settings.duplex = PHY_DUPLEX_BOTH;
			phy_settings.speed = (uf_plan.enable & bit) ? PHY_SPEED_AUTO : PHY_OFF;
			phy_set_speed();
		}
	}
	/* 4. Read back */
	for (i = 0; i < uf_plan.nv; i++) {
		h_vid = uf_plan.vid[i];
		if (!vlan_check(uf_plan.member[i], uf_plan.tagged[i]))
			return UF_AE_VERIFY;
	}
	for (i = 0; i < UF_NPORTS; i++)
		if (port_pvid_get(log_port(i + 1)) != uf_plan.pvid[i])
			return UF_AE_VERIFY;
	t0 = ticks16() - t0;
	if (t0 > uf_t_apply_max)
		uf_t_apply_max = t0;
	return UF_AE_NONE;
}

uint8_t uh_tcp_open(__xdata const uint8_t * __xdata ip, __xdata uint16_t port) __banked
{
	__xdata uip_ipaddr_t a;

	uip_ipaddr(&a, ip[0], ip[1], ip[2], ip[3]);
	uf_conn = uip_connect(&a, HTONS(port));
	return uf_conn != 0;
}

void uh_udp_discovery(void) __banked
{
	disc_pending = 1;
}

/* Called by the uIP dispatcher for every connection that is not httpd's */
static void send_next(void)
{
	__xdata uint16_t n = uf_xfer_next(uip_mss());

	if (n)
		uip_send(uf_xfer.buf + uf_xfer.acked, n);
}

static void finish(__xdata uint8_t result)
{
	__xdata uint16_t t0;

	uf_conn = 0;
	t0 = ticks16();
	uf_agent_done(result);
	t0 = ticks16() - t0;
	uf_t_last_open = t0;
	if (t0 > uf_t_open_max)
		uf_t_open_max = t0;
}

void uf_tcp_appcall(void) __banked
{
	__xdata uint8_t st;

	if (uip_conn != uf_conn || !uf_conn) {
		/* A connection we already finished with */
		if (!uip_closed() && !uip_aborted() && !uip_timedout())
			uip_abort();
		return;
	}
	if (uip_aborted() || uip_timedout()) {
		finish(UF_XR_ABORT);
		return;
	}
	if (uip_acked())
		uf_xfer_acked();
	if (uip_newdata()) {
		if (uf_rx_port < 10)
			uplink_log = uf_rx_port;
		st = uf_resp_feed(uip_appdata, uip_len);
		if (st == UF_RS_DONE || st == UF_RS_ERROR) {
			uip_close();
			finish(st == UF_RS_DONE ? UF_XR_OK : UF_XR_ABORT);
			return;
		}
	}
	if (uip_closed()) {
		st = uf_resp_closed();
		finish(st == UF_RS_DONE ? UF_XR_OK : UF_XR_ABORT);
		return;
	}
	if (uip_rexmit()) {
		st = 0;
		if (uf_xfer_rexmit())
			uip_send(uf_xfer.buf + uf_xfer.acked, uf_xfer_rexmit());
		return;
	}
	if (uip_poll() && uf_agent_expired()) {
		uip_abort();
		finish(UF_XR_ABORT);
		return;
	}
	if (uip_connected() || uip_acked() || uip_poll())
		send_next();
}

void uf_udp_appcall(void) __banked
{
	__xdata uint16_t n;

	if (uip_udp_conn != uf_udp || !uip_poll() || !disc_pending)
		return;
	disc_pending = 0;
	uf_self.disc_seq++;
	n = uf_disc_build(uip_appdata);
	uip_udp_send(n);
}

static void copy_hostname(void)
{
	for (h_i = 0; h_i < UF_NAME_LEN - 1 && h_i < 23 && hostname[h_i]; h_i++)
		uf_self.hostname[h_i] = hostname[h_i];
	uf_self.hostname[h_i] = 0;
}

static void start(void)
{
	__xdata uip_ipaddr_t bcast;

	if (!g_started) {
		uip_ipaddr(&bcast, 255, 255, 255, 255);
		uf_udp = uip_udp_new(&bcast, HTONS(DISC_PORT));
		if (uf_udp)
			uip_udp_bind(uf_udp, HTONS(DISC_PORT));
		uf_agent_init(uip_ethaddr.addr, ctrl_ip, ctrl_port);
		g_started = 1;
	}
	uf_agent.enabled = 1;
}

void uf_glue_poll(void) __banked
{
	__xdata uint16_t t0;

	if (!g_started || !uf_agent.enabled)
		return;
	copy_hostname();
	t0 = ticks16();
	uf_agent_poll();
	t0 = ticks16() - t0;
	if (t0 > uf_t_build_max)
		uf_t_build_max = t0;
}

/* ---- CLI: unifi [status|controller <ip> [port]|on|off|forget|uplink <1-5|auto>] ---- */

static uint8_t word_is(__xdata uint8_t w, __code const char * __xdata s)
{
	__xdata uint8_t * __xdata p;

	if (w >= cmd_words_len)
		return 0;
	p = cmd_buffer + cmd_words_b[w];
	while (*s && *p == (uint8_t)*s) {
		p++;
		s++;
	}
	return !*s && (*p == 0 || *p == ' ');
}

/* Decimal at word w, 0xffff when missing or bad */
static uint16_t word_num(__xdata uint8_t w)
{
	__xdata uint8_t * __xdata p;
	__xdata uint16_t v = 0;

	if (w >= cmd_words_len)
		return 0xffff;
	p = cmd_buffer + cmd_words_b[w];
	if (*p < '0' || *p > '9')
		return 0xffff;
	while (*p >= '0' && *p <= '9') {
		if (v > 6553)
			return 0xffff;
		v = v * 10 + (*p++ - '0');
	}
	return v;
}

static uint8_t word_ip(__xdata uint8_t w, __xdata uint8_t * __xdata ip)
{
	__xdata uint8_t * __xdata p;
	__xdata uint16_t v;

	if (w >= cmd_words_len)
		return 0;
	p = cmd_buffer + cmd_words_b[w];
	for (h_i = 0; h_i < 4; h_i++) {
		v = 0;
		if (*p < '0' || *p > '9')
			return 0;
		while (*p >= '0' && *p <= '9' && v < 256)
			v = v * 10 + (*p++ - '0');
		if (v > 255 || (h_i < 3 && *p++ != '.'))
			return 0;
		ip[h_i] = (uint8_t)v;
	}
	return *p == 0 || *p == ' ';
}

static void print_ip4(__xdata const uint8_t * __xdata ip)
{
	for (h_i = 0; h_i < 4; h_i++) {
		if (h_i)
			write_char('.');
		itoa(ip[h_i]);
	}
}

static void status(void)
{
	print_string("UniFi agent: ");
	print_string(!g_started ? "not configured" : (uf_agent.enabled ? "on" : "off"));
	print_string("\n Controller: ");
	print_ip4(g_started ? uf_state.ctrl_ip : ctrl_ip);
	write_char(':');
	itoa_short(g_started ? uf_state.ctrl_port : ctrl_port);
	print_string("\n Adopted: ");
	print_string(uf_state.flags & UF_SF_ADOPTED ? "yes" : "no");
	print_string(uf_state.flags & UF_SF_GCM ? " (AES-GCM)" : "");
	print_string("\n cfgversion: ");
	if (uf_self.cfgversion[0])
		print_string_x(uf_self.cfgversion);
	else
		write_char('?');
	print_string("\n Uplink port: ");
	itoa(uh_uplink());
	print_string(uplink_forced ? " (set)" : " (auto)");
	print_string("\n Informs/ok/fail: ");
	itoa_short(uf_agent.n_inform); write_char('/');
	itoa_short(uf_agent.n_ok); write_char('/');
	itoa_short(uf_agent.n_fail);
	print_string("\n Auth failures: ");
	itoa_short(uf_agent.n_auth_fail);
	print_string(" last reason ");
	itoa(uf_agent.last_terr);
	print_string("\n Last HTTP status: ");
	itoa_short(uf_agent.last_status);
	print_string(" interval: ");
	itoa_short(uf_agent.interval);
	print_string("\n Largest response: ");
	itoa_short(uf_agent.max_resp);
	print_string(" of ");
	itoa_short(UF_RX_SIZE);
	print_string(", oversize: ");
	itoa_short(uf_agent.n_oversize);
	print_string("\n Config error: ");
	itoa(uf_agent.last_cfg_err);
	print_string(" apply error: ");
	itoa(uf_agent.last_apply_err);
	print_string("\n Store slot: ");
	itoa(uf_store_slot);
	print_string(" seq ");
	print_long(uf_state.seq);
	print_string(uf_store_foreign ? " WRITES DISABLED (foreign data)" : "");
	print_string("\n Max ticks (5 ms) inform cycle/response/apply: ");
	itoa_short(uf_t_build_max); write_char('/');
	itoa_short(uf_t_open_max); write_char('/');
	itoa_short(uf_t_apply_max);
	write_char('\n');
}

void uf_cli(void) __banked
{
	__xdata uint16_t v;
	__xdata uint8_t ip[4];

	if (cmd_words_len < 2 || word_is(1, "status")) {
		status();
	} else if (word_is(1, "controller") && word_ip(2, ip)) {
		v = cmd_words_len > 3 ? word_num(3) : 8080;
		if (!v || v == 0xffff) {
			print_string("Invalid port\n");
			return;
		}
		memcpy(ctrl_ip, ip, 4);
		ctrl_port = v;
		if (g_started && !(uf_state.flags & UF_SF_ADOPTED)) {
			memcpy(uf_state.ctrl_ip, ip, 4);
			uf_state.ctrl_port = v;
		}
		start();
	} else if (word_is(1, "on")) {
		if (ctrl_ip[0] | g_started)
			start();
		else
			print_string("Set the controller first: unifi controller <ip> [port]\n");
	} else if (word_is(1, "off")) {
		uf_agent.enabled = 0;
	} else if (word_is(1, "discovery")) {
		uf_agent.discovery = word_is(2, "on");
	} else if (word_is(1, "forget")) {
		if (g_started)
			uf_agent_forget();
	} else if (word_is(1, "uplink")) {
		v = word_num(2);
		if (word_is(2, "auto"))
			uplink_forced = 0;
		else if (v >= 1 && v <= UF_NPORTS)
			uplink_forced = (uint8_t)v;
		else
			print_string("unifi uplink <1-5|auto>\n");
	} else {
		print_string("unifi [status|controller <ip> [port]|on|off|forget|uplink <1-5|auto>|discovery on|off]\n");
	}
}

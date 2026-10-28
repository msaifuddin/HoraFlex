/*
 * UniFi discovery TLVs and the inform JSON document
 *
 * Discovery follows unifi-stubd: version 2, type 6, big-endian TLVs for
 * MAC+IP, MAC, uptime, hostname, firmware, model, serial, sequence and the
 * factory-default flag. The serial is the MAC in upper-case hex.
 */
#include "uf_msg.h"
#include "uf_ident.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

UF_X struct uf_tele uf_tele;
UF_X struct uf_self uf_self;

static UF_C const char hexd[] = "0123456789ABCDEF";
static UF_C const char hexl[] = "0123456789abcdef";

/* Output cursor shared by the writers; w_ok drops to 0 once max is reached */
static UF_X uint8_t * UF_X w_buf;
static UF_X uint16_t w_len, w_max;
static UF_X uint8_t w_ok;
static UF_X uint8_t w_dig[21];
static UF_X uint8_t w_num[8];

static void put(UF_X uint8_t c)
{
	if (w_len < w_max)
		w_buf[w_len++] = c;
	else
		w_ok = 0;
}

static void lit(UF_C const char * UF_X s)
{
	while (*s)
		put((uint8_t)*s++);
}

/* JSON string body with escaping */
static void xstr(UF_X const char * UF_X s)
{
	UF_X uint8_t c;

	while ((c = (uint8_t)*s++)) {
		if (c == '"' || c == '\\') {
			put('\\');
			put(c);
		} else if (c < 0x20 || c >= 0x7f) {
			put('?');
		} else {
			put(c);
		}
	}
}

static void cstr(UF_C const char * UF_X s)
{
	put('"');
	lit(s);
	put('"');
}

/* Decimal of the big-endian unsigned integer in w_num[8 - n .. 8) */
static void num_be(UF_X uint8_t n)
{
	UF_X uint8_t i, nd = 0, nz;
	UF_X uint16_t r;

	do {
		r = 0;
		nz = 0;
		for (i = 8 - n; i < 8; i++) {
			r = (r << 8) | w_num[i];
			w_num[i] = (uint8_t)(r / 10);
			r %= 10;
			nz |= w_num[i];
		}
		w_dig[nd++] = (uint8_t)('0' + r);
	} while (nz);
	while (nd)
		put(w_dig[--nd]);
}

static void u32(UF_X uint32_t v)
{
	w_num[4] = (uint8_t)(v >> 24); w_num[5] = (uint8_t)(v >> 16);
	w_num[6] = (uint8_t)(v >> 8); w_num[7] = (uint8_t)v;
	num_be(4);
}

static void u64(UF_X const uint8_t * UF_X be)
{
	uf_memcpy(w_num, be, 8);
	num_be(8);
}

static void ip_raw(UF_X const uint8_t * UF_X a)
{
	UF_X uint8_t i;

	for (i = 0; i < 4; i++) {
		if (i)
			put('.');
		u32(a[i]);
	}
}

static void ip(UF_X const uint8_t * UF_X a)
{
	put('"');
	ip_raw(a);
	put('"');
}

static UF_X const uint8_t * UF_X w_mac;
static UF_X uint8_t w_sep, w_i, w_b;

static void mac(UF_X const uint8_t * UF_X m, UF_X uint8_t sep)
{
	w_mac = m;
	w_sep = sep;
	for (w_i = 0; w_i < 6; w_i++) {
		if (w_i && w_sep)
			put(':');
		w_b = w_mac[w_i];
		put((uint8_t)(w_sep ? hexl[w_b >> 4] : hexd[w_b >> 4]));
		w_b = w_mac[w_i];
		put((uint8_t)(w_sep ? hexl[w_b & 15] : hexd[w_b & 15]));
	}
}

static void key(UF_C const char * UF_X k)
{
	put(',');
	cstr(k);
	put(':');
}

static void bool_(UF_X uint8_t b)
{
	lit(b ? "true" : "false");
}

/* Discovery TLV header: type, 16-bit length */
static void tlv(UF_X uint8_t type, UF_X uint8_t len)
{
	put(type);
	put(0);
	put(len);
}

static void tlv_c(UF_X uint8_t type, UF_C const char * UF_X s)
{
	UF_X uint8_t n = 0;

	while (s[n])
		n++;
	tlv(type, n);
	lit(s);
}

static void be32(UF_X uint32_t v)
{
	put((uint8_t)(v >> 24)); put((uint8_t)(v >> 16)); put((uint8_t)(v >> 8)); put((uint8_t)v);
}

uint16_t uf_disc_build(UF_X uint8_t * UF_X buf) UF_BANKED
{
	UF_X uint8_t i, n;

	w_buf = buf;
	w_len = 4;
	w_max = 256;
	w_ok = 1;
	tlv(0x02, 10);
	for (i = 0; i < 6; i++)
		put(uf_self.mac[i]);
	for (i = 0; i < 4; i++)
		put(uf_self.ip[i]);
	tlv(0x01, 6);
	for (i = 0; i < 6; i++)
		put(uf_self.mac[i]);
	tlv(0x0a, 4);
	be32(uf_self.uptime);
	for (n = 0; uf_self.hostname[n] && n < UF_NAME_LEN; n++)
		;
	if (n) {
		tlv(0x0b, n);
		for (i = 0; i < n; i++)
			put((uint8_t)uf_self.hostname[i]);
	}
	for (n = 0; uf_self.version[n] && n < UF_FWVER_LEN; n++)
		;
	tlv(0x16, n);
	for (i = 0; i < n; i++)
		put((uint8_t)uf_self.version[i]);
	tlv_c(0x15, UNIFI_MODEL);
	tlv(0x13, 12);
	mac(uf_self.mac, 0);
	tlv(0x12, 4);
	be32(uf_self.disc_seq);
	tlv(0x17, 1);
	put(uf_self.adopted ? 0 : 1);
	buf[0] = 0x02;
	buf[1] = 0x06;
	buf[2] = (uint8_t)((w_len - 4) >> 8);
	buf[3] = (uint8_t)(w_len - 4);
	return w_len;
}

static void port_entry(UF_X uint8_t i)
{
	UF_X struct uf_tport * UF_X p = &uf_tele.p[i];

	lit("{\"port_idx\":");
	u32(i + 1);
	key("name");
	put('"');
	if (uf_live.nv != 0xff && (uf_live.has_name & (1 << i))) {
		xstr(uf_live.name[i]);
	} else {
		lit("Port ");
		u32(i + 1);
	}
	put('"');
	key("enable"); bool_(p->enabled);
	key("up"); bool_(p->up);
	key("is_uplink"); bool_(uf_self.uplink == i + 1);
	lit(",\"op_mode\":\"switch\",\"media\":\"GE\",\"autoneg\":true,\"flowctrl_rx\":false,\"flowctrl_tx\":false"
	    ",\"port_poe\":false,\"poe_caps\":0,\"max_speed\":2500,\"speed_caps\":[10,100,1000,2500]"
	    ",\"stp_pathcost\":20000,\"satisfaction\":100,\"rx_dropped\":0,\"tx_dropped\":0"
	    ",\"rx_bytes\":0,\"tx_bytes\":0,\"mac_table\":[]");
	key("stp_state"); cstr(p->up ? "forwarding" : "disabled");
	key("full_duplex"); bool_(p->up && p->full_duplex);
	key("speed"); u32(p->up ? p->speed : 0);
	key("rx_packets"); u64(p->rx_pkts);
	key("tx_packets"); u64(p->tx_pkts);
	key("rx_errors"); u32(p->rx_err);
	key("tx_errors"); u32(p->tx_err);
	put('}');
}

uint16_t uf_inform_build(UF_X uint8_t * UF_X buf, UF_X uint16_t max) UF_BANKED
{
	UF_X uint8_t i;
	UF_X uint16_t speed = 0;

	w_buf = buf;
	w_len = 0;
	w_max = max;
	w_ok = 1;

	for (i = 0; i < UF_NPORTS; i++)
		if (uf_self.uplink == i + 1 && uf_tele.p[i].up)
			speed = uf_tele.p[i].speed;

	lit("{\"mac\":\""); mac(uf_self.mac, 1); put('"');
	key("ip"); ip(uf_self.ip);
	key("hostname"); put('"'); xstr(uf_self.hostname); put('"');
	key("model"); cstr(UNIFI_MODEL);
	key("model_display"); cstr(UNIFI_MODEL_DISPLAY);
	lit(",\"type\":\"usw\"");
	key("version"); put('"'); xstr(uf_self.version); put('"');
	key("serial"); put('"'); mac(uf_self.mac, 0); put('"');
	key("num_port"); u32(UF_NPORTS);
	key("state"); u32(uf_self.adopted ? 2 : 1);
	key("adopted"); bool_(uf_self.adopted);
	key("default"); bool_(!uf_self.adopted);
	lit(",\"discovery_response\":true,\"required_version\":\"5.0.0\"");
	key("cfgversion"); put('"');
	if (uf_self.cfgversion[0])
		xstr(uf_self.cfgversion);
	else
		put('?');
	put('"');
	key("uptime"); u32(uf_self.uptime);
	if (uf_self.time) {
		key("time"); u32(uf_self.time);
	}
	key("inform_url"); lit("\"http://"); ip_raw(uf_self.inform_ip);
	put(':'); u32(uf_self.inform_port); lit("/inform\"");
	key("inform_ip"); ip(uf_self.inform_ip);
	lit(",\"sys_stats\":{\"loadavg_1\":0.1,\"loadavg_5\":0.1,\"loadavg_15\":0.1,"
	    "\"mem_total\":65536,\"mem_used\":32768,\"mem_buffer\":0");
	key("uptime"); u32(uf_self.uptime);
	lit("},\"system-stats\":{\"cpu\":5,\"mem\":50");
	key("uptime"); u32(uf_self.uptime);
	lit("},\"if_table\":[{\"name\":\"eth0\",\"mac\":\""); mac(uf_self.mac, 1); put('"');
	key("ip"); ip(uf_self.ip);
	key("num_port"); u32(UF_NPORTS);
	key("up"); bool_(speed != 0);
	key("speed"); u32(speed);
	lit(",\"full_duplex\":true}],\"ethernet_table\":[{\"name\":\"eth0\",\"mac\":\"");
	mac(uf_self.mac, 1);
	lit("\",\"num_port\":"); u32(UF_NPORTS);
	lit("}],\"port_table\":[");
	for (i = 0; i < UF_NPORTS; i++) {
		if (i)
			put(',');
		port_entry(i);
	}
	lit("],\"lldp_table\":[]}");
	return w_ok ? w_len : 0;
}

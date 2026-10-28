/*
 * mgmt_cfg / system_cfg parsing and switch plan for the UniFi agent.
 *
 * Observed system_cfg (UCG Ultra, Network 10.6.106):
 *   switch.managementvlan=1
 *   switch.vlan.<V>.id=<vid>  .mode=untagged|tagged  .status=enabled
 *   switch.vlan.<V>.port.<P>.mode=untagged|tagged|exclude
 *   switch.port.<P>.name=..  .opmode=switch  .pvid=<vid>  .status=disabled
 * (status is only sent for a disabled port; Network 10.6.109)
 * A VLAN's mode is the membership of every port without a port override.
 */
#include "uf_cfg.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

UF_X struct uf_mgmt uf_mgmt;
UF_X struct uf_sys uf_sys;
UF_X struct uf_plan uf_plan;
UF_X struct uf_plan uf_live;

/* Current line: key is [ks, ke), value is [vs, ve); cur walks the key */
static UF_X const char * UF_X ks;
static UF_X const char * UF_X ke;
static UF_X const char * UF_X vs;
static UF_X const char * UF_X ve;
static UF_X const char * UF_X cur;

/* Splits the line starting at s; returns the start of the next line, or 0 at the end */
static UF_X const char *next_line(UF_X const char * UF_X s)
{
	UF_X const char * UF_X p = s;

	ks = s;
	ke = vs = 0;
	while (*p && *p != '\n') {
		if (!ke && *p == '=') {
			ke = p;
			vs = p + 1;
		}
		p++;
	}
	ve = p;
	if (ve > s && ve[-1] == '\r')
		ve--;
	if (!ke)
		ke = ve;
	cur = ks;
	return *p ? p + 1 : 0;
}

static uint8_t eat(UF_C const char * UF_X pfx)
{
	UF_X const char * UF_X p = cur;

	while (*pfx) {
		if (p >= ke || *p != *pfx)
			return 0;
		p++;
		pfx++;
	}
	cur = p;
	return 1;
}

static uint8_t key_rest_is(UF_C const char * UF_X s)
{
	return eat(s) && cur == ke;
}

static uint8_t val_is(UF_C const char * UF_X s)
{
	UF_X const char * UF_X p = vs;

	while (*s) {
		if (p >= ve || *p != *s)
			return 0;
		p++;
		s++;
	}
	return p == ve;
}

/* Decimal from p up to end, at most 65534; 0xffff when empty or bad */
static UF_X uint16_t d_v;

static uint16_t dec(UF_X const char * UF_X p, UF_X const char * UF_X end)
{
	if (p >= end || end - p > 5)
		return 0xffff;
	d_v = 0;
	while (p < end) {
		if (*p < '0' || *p > '9' || d_v > 6553 || (d_v == 6553 && *p > '4'))
			return 0xffff;
		d_v = (d_v << 3) + (d_v << 1) + (uint8_t)(*p++ - '0');
	}
	return d_v;
}

/* Index in the key: digits from cur to the next '.', cur left after the '.' */
static uint16_t key_index(void)
{
	UF_X const char * UF_X p = cur;
	UF_X uint16_t v;

	while (p < ke && *p != '.')
		p++;
	if (p >= ke)
		return 0xffff;
	v = dec(cur, p);
	cur = p + 1;
	return v;
}

static uint8_t hexnib(UF_X char c)
{
	if (c >= '0' && c <= '9')
		return (uint8_t)(c - '0');
	if (c >= 'a' && c <= 'f')
		return (uint8_t)(c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (uint8_t)(c - 'A' + 10);
	return 0xff;
}

/* "http://a.b.c.d[:port]/..." into uf_mgmt.inform_ip/port */
static void parse_inform_url(void)
{
	UF_X const char * UF_X p = vs;
	UF_X const char * UF_X q;
	UF_X uint8_t i;
	UF_X uint16_t v;

	if (ve - p < 8 || p[0] != 'h' || p[1] != 't' || p[2] != 't' || p[3] != 'p' || p[4] != ':' || p[5] != '/' || p[6] != '/')
		return;
	p += 7;
	for (i = 0; i < 4; i++) {
		q = p;
		while (q < ve && *q >= '0' && *q <= '9')
			q++;
		v = dec(p, q);
		if (v > 255)
			return;
		uf_mgmt.inform_ip[i] = (uint8_t)v;
		if (i < 3) {
			if (q >= ve || *q != '.')
				return;
			q++;
		}
		p = q;
	}
	uf_mgmt.inform_port = 80;
	if (p < ve && *p == ':') {
		q = ++p;
		while (q < ve && *q >= '0' && *q <= '9')
			q++;
		v = dec(p, q);
		if (!v || v == 0xffff)
			return;
		uf_mgmt.inform_port = v;
		p = q;
	}
	if (p < ve && *p != '/')
		return;
	uf_mgmt.has_inform = 1;
}

void uf_mgmt_parse(UF_X const char * UF_X s) UF_BANKED
{
	UF_X uint8_t i, hi, lo;
	UF_X uint16_t n;

	uf_memset((UF_X uint8_t *)&uf_mgmt, 0, sizeof(uf_mgmt));
	while (s) {
		s = next_line(s);
		if (!vs)
			continue;
		if (key_rest_is("authkey")) {
			if (ve - vs != 32) {
				uf_mgmt.bad = 1;
				continue;
			}
			for (i = 0; i < 16; i++) {
				hi = hexnib(vs[i << 1]);
				lo = hexnib(vs[(i << 1) + 1]);
				if (hi > 15 || lo > 15) {
					uf_mgmt.bad = 1;
					break;
				}
				uf_mgmt.authkey[i] = (uint8_t)((hi << 4) | lo);
			}
			if (i == 16)
				uf_mgmt.has_authkey = 1;
		} else if (key_rest_is("use_aes_gcm")) {
			uf_mgmt.use_gcm = val_is("true");
		} else if (key_rest_is("cfgversion")) {
			n = ve - vs;
			if (!n || n >= UF_CFGVER_LEN) {
				uf_mgmt.bad = 1;
				continue;
			}
			for (i = 0; i < n; i++)
				uf_mgmt.cfgversion[i] = vs[i];
			uf_mgmt.cfgversion[n] = 0;
			uf_mgmt.has_cfgversion = 1;
		} else if (key_rest_is("inform_url")) {
			parse_inform_url();
		}
	}
}

static UF_X uint16_t s_line;	/* system_cfg line being parsed */
static UF_X uint8_t p_j, p_n, p_mem, p_tag;
static UF_X struct uf_svlan * UF_X p_sv;

static void sys_fail(UF_X uint8_t e)
{
	if (!uf_sys.err) {
		uf_sys.err = e;
		uf_sys.err_line = s_line;
	}
}

static UF_X struct uf_svlan *vlan_slot(UF_X uint16_t idx)
{
	UF_X uint8_t i;
	UF_X struct uf_svlan * UF_X v;

	for (i = 0; i < uf_sys.nvlans; i++)
		if (uf_sys.v[i].idx == idx)
			return &uf_sys.v[i];
	if (uf_sys.nvlans >= UF_MAX_VLANS)
		return 0;
	v = &uf_sys.v[uf_sys.nvlans++];
	uf_memset((UF_X uint8_t *)v, 0, sizeof(*v));
	v->idx = idx;
	return v;
}

static uint8_t vlan_mode(void)
{
	if (val_is("untagged"))
		return UF_VM_UNTAGGED;
	if (val_is("tagged"))
		return UF_VM_TAGGED;
	if (val_is("exclude"))
		return UF_VM_EXCLUDE;
	return UF_VM_UNSET;
}

static void note_unknown(void)
{
	UF_X uint8_t i = 0;
	UF_X char * UF_X d;

	if (uf_sys.n_unknown >= UF_UNKNOWN_KEYS)
		return;
	d = uf_sys.unknown[uf_sys.n_unknown++];
	while (ks + i < ke && i < UF_UNKNOWN_LEN - 1) {
		d[i] = ks[i];
		i++;
	}
	d[i] = 0;
}

static void sys_vlan_line(void)
{
	UF_X uint16_t idx, port, v;
	UF_X struct uf_svlan * UF_X sv;
	UF_X uint8_t m;

	idx = key_index();
	if (idx == 0xffff)
		{ sys_fail(UF_CE_SYNTAX); return; }
	sv = vlan_slot(idx);
	if (!sv)
		{ sys_fail(UF_CE_TOO_MANY_VLANS); return; }
	if (key_rest_is("id")) {
		v = dec(vs, ve);
		if (!v || v > 4094)
			{ sys_fail(UF_CE_VALUE); return; }
		sv->vid = v;
	} else if (key_rest_is("mode")) {
		m = vlan_mode();
		if (m == UF_VM_UNSET)
			{ sys_fail(UF_CE_VALUE); return; }
		sv->mode = m;
	} else if (key_rest_is("status")) {
		if (val_is("disabled"))
			sv->disabled = 1;
		else if (!val_is("enabled"))
			{ sys_fail(UF_CE_VALUE); return; }
	} else if (eat("port.")) {
		port = key_index();
		if (port == 0xffff || !key_rest_is("mode"))
			{ sys_fail(UF_CE_SYNTAX); return; }
		m = vlan_mode();
		if (m == UF_VM_UNSET)
			{ sys_fail(UF_CE_VALUE); return; }
		if (port >= 1 && port <= UF_NPORTS)
			sv->port_mode[port - 1] = m;
	}
}

static void sys_port_line(void)
{
	UF_X uint16_t port, v;
	UF_X uint8_t i;
	UF_X char c;
	UF_X struct uf_sport * UF_X sp;

	port = key_index();
	if (port == 0xffff)
		{ sys_fail(UF_CE_SYNTAX); return; }
	if (port < 1 || port > UF_NPORTS)
		return;		/* not one of ours; nothing to apply */
	sp = &uf_sys.p[port - 1];
	if (key_rest_is("pvid")) {
		v = dec(vs, ve);
		if (!v || v > 4094)
			{ sys_fail(UF_CE_VALUE); return; }
		sp->pvid = v;
	} else if (key_rest_is("status")) {
		if (val_is("disabled"))
			sp->disabled = 1;
		else if (!val_is("enabled"))
			{ sys_fail(UF_CE_VALUE); return; }
	} else if (key_rest_is("opmode")) {
		if (!val_is("switch"))
			{ sys_fail(UF_CE_OPMODE); return; }
	} else if (key_rest_is("name")) {
		for (i = 0; vs + i < ve && i < UF_NAME_LEN - 1; i++) {
			c = vs[i];
			sp->name[i] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? c : '_';
		}
		sp->name[i] = 0;
		sp->has_name = 1;
	} else {
		note_unknown();
	}
}

uint8_t uf_sys_parse(UF_X const char * UF_X s) UF_BANKED
{
	UF_X uint16_t v;

	s_line = 0;
	uf_memset((UF_X uint8_t *)&uf_sys, 0, sizeof(uf_sys));
	uf_sys.mgmt_vlan = 1;
	while (s) {
		s = next_line(s);
		s_line++;
		if (!eat("switch."))
			continue;
		uf_sys.n_switch++;
		if (!vs) {
			sys_fail(UF_CE_SYNTAX);
			continue;
		}
		if (key_rest_is("managementvlan")) {
			v = dec(vs, ve);
			if (!v || v > 4094)
				sys_fail(UF_CE_VALUE);
			else
				uf_sys.mgmt_vlan = v;
		} else if (eat("vlan.")) {
			sys_vlan_line();
		} else if (eat("port.")) {
			sys_port_line();
		}
	}
	return uf_sys.err;
}

uint8_t uf_plan_build(void) UF_BANKED
{
	UF_X uint8_t i, j, m, bit;

	uf_memset((UF_X uint8_t *)&uf_plan, 0, sizeof(uf_plan));
	if (uf_sys.err)
		return uf_sys.err;
	if (uf_sys.mgmt_vlan != 1)
		return UF_CE_MGMT_VLAN;
	uf_plan.mgmt_vlan = uf_sys.mgmt_vlan;

	for (i = 0; i < uf_sys.nvlans; i++) {
		p_sv = &uf_sys.v[i];
		if (p_sv->disabled)
			continue;
		if (!p_sv->vid)
			return UF_CE_VLAN_NO_ID;
		for (j = 0; j < uf_plan.nv; j++)
			if (uf_plan.vid[j] == p_sv->vid)
				return UF_CE_DUP_VID;
		p_n = uf_plan.nv;
		uf_plan.vid[p_n] = p_sv->vid;
		p_mem = p_tag = 0;
		for (p_j = 0; p_j < UF_NPORTS; p_j++) {
			m = p_sv->port_mode[p_j] ? p_sv->port_mode[p_j] : p_sv->mode;
			bit = (uint8_t)(1 << p_j);
			if (m == UF_VM_UNTAGGED) {
				p_mem |= bit;
			} else if (m == UF_VM_TAGGED) {
				p_mem |= bit;
				p_tag |= bit;
			}
		}
		uf_plan.member[p_n] = p_mem;
		uf_plan.tagged[p_n] = p_tag;
		uf_plan.nv++;
	}

	/* A port without pvid uses the default network, VLAN 1 */
	for (p_j = 0; p_j < UF_NPORTS; p_j++) {
		uf_plan.pvid[p_j] = uf_sys.p[p_j].pvid ? uf_sys.p[p_j].pvid : 1;
		if (uf_sys.p[p_j].has_name) {
			uf_plan.has_name |= (uint8_t)(1 << p_j);
			uf_memcpy((UF_X uint8_t *)uf_plan.name[p_j], (UF_X uint8_t *)uf_sys.p[p_j].name, UF_NAME_LEN);
		}
	}
	uf_plan.enable = (1 << UF_NPORTS) - 1;
	for (p_j = 0; p_j < UF_NPORTS; p_j++)
		if (uf_sys.p[p_j].disabled)
			uf_plan.enable &= (uint8_t)~(1 << p_j);
	return UF_CE_NONE;
}

void uf_plan_default(void) UF_BANKED
{
	uf_memset((UF_X uint8_t *)&uf_plan, 0, sizeof(uf_plan));
	uf_plan.mgmt_vlan = 1;
	uf_plan.nv = 1;
	uf_plan.vid[0] = 1;
	uf_plan.member[0] = (1 << UF_NPORTS) - 1;
	for (p_j = 0; p_j < UF_NPORTS; p_j++)
		uf_plan.pvid[p_j] = 1;
	uf_plan.enable = (1 << UF_NPORTS) - 1;
}

uint8_t uf_plan_protect(UF_X uint8_t uplink) UF_BANKED
{
	UF_X uint8_t i, bit;

	if (uplink < 1 || uplink > UF_NPORTS)
		return UF_CE_UPLINK_UNKNOWN;
	bit = (uint8_t)(1 << (uplink - 1));
	if (!(uf_plan.enable & bit))
		return UF_CE_PROTECT_DISABLE;
	/* The controller is reached through the uplink with the management VLAN
	 * untagged; it must stay an untagged member and the port's PVID. */
	if (uf_plan.pvid[uplink - 1] != uf_plan.mgmt_vlan)
		return UF_CE_PROTECT_VLAN;
	for (i = 0; i < uf_plan.nv; i++)
		if (uf_plan.vid[i] == uf_plan.mgmt_vlan)
			return ((uf_plan.member[i] & bit) && !(uf_plan.tagged[i] & bit)) ? UF_CE_NONE : UF_CE_PROTECT_VLAN;
	return UF_CE_PROTECT_VLAN;
}

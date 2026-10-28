#include "ut.h"
#include "../uf_json.h"
#include "../uf_cfg.h"

static uint8_t buf[16384];

static uint16_t load(const char *json)
{
	size_t n = strlen(json);
	memcpy(buf, json, n);
	buf[n] = 0xa5;	/* the scanner must not rely on a terminator */
	return (uint16_t)n;
}

static void json_basics(void)
{
	uint16_t n;

	n = load("{\"_type\":\"noop\",\"interval\":10}");
	CHECK(uf_json_scan(buf, n));
	CHECK_EQ(uf_jv[UF_JK_TYPE].type, UF_JT_STRING);
	CHECK_STR(buf + uf_jv[UF_JK_TYPE].off, "noop");
	CHECK_EQ(uf_json_uint(buf, UF_JK_INTERVAL, 0), 10);
	CHECK_EQ(uf_jv[UF_JK_SYSTEM_CFG].type, UF_JT_ABSENT);

	/* escapes, nested values and unknown keys */
	n = load(" { \"x\" : [1,{\"a\":\"}\"},[]], \"_type\" : \"set\\\"p\\u0041\\n\\u00e9\\/\" ,"
		 "\"server_time_in_utc\":1792158536051, \"cmd\":{\"a\":[\"]\"]} } ");
	CHECK(uf_json_scan(buf, n));
	CHECK_STR(buf + uf_jv[UF_JK_TYPE].off, "set\"pA\n?/");
	CHECK_EQ(uf_jv[UF_JK_TYPE].len, 9);
	CHECK_EQ(uf_json_uint(buf, UF_JK_SERVER_TIME, 3), 1792158536UL);
	CHECK_EQ(uf_jv[UF_JK_CMD].type, UF_JT_NESTED);

	n = load("{}");
	CHECK(uf_json_scan(buf, n));
	CHECK_EQ(uf_json_uint(buf, UF_JK_INTERVAL, 0), 0xffffffffUL);

	/* malformed input is rejected */
	static const char *bad[] = {
		"", "[]", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x", "{\"a\":\"x}",
		"{\"a\":\"\\q\"}", "{\"a\":\"\\u12\"}", "{\"a\":[}", "{\"a\":]}", "{\"a\":\"\x01\"}",
		"{\"a\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		n = load(bad[i]);
		CHECK(!uf_json_scan(buf, n));
	}
	/* truncation at every length never reads past n and never accepts */
	n = load("{\"_type\":\"setparam\",\"cfgversion\":\"ab\",\"x\":[{\"y\":\"z\\n\"}],\"interval\":5}");
	for (uint16_t k = 0; k < n; k++) {
		static uint8_t t[256];
		memcpy(t, "{\"_type\":\"setparam\",\"cfgversion\":\"ab\",\"x\":[{\"y\":\"z\\n\"}],\"interval\":5}", n);
		CHECK(!uf_json_scan(t, k));
	}
	CHECK_EQ(uf_json_uint((uint8_t *)"{}", UF_JK_INTERVAL, 0), 0xffffffffUL);
}

static char *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	static char d[16384];
	*n = f ? fread(d, 1, sizeof(d) - 1, f) : 0;
	if (f)
		fclose(f);
	d[*n] = 0;
	return d;
}

/* Real setparam from a UCG Ultra (Network 10.6.106), sanitised. The authkey
 * line was appended for the test; that push carried none. */
static void golden(void)
{
	size_t n;
	char *s = read_file("setparam_golden.json", &n);

	CHECK(n > 2800);
	memcpy(buf, s, n);
	CHECK(uf_json_scan(buf, (uint16_t)n));
	CHECK_STR(buf + uf_jv[UF_JK_TYPE].off, "setparam");
	CHECK_STR(buf + uf_jv[UF_JK_CFGVERSION].off, "7066c2ce192f33b9");
	CHECK_EQ(uf_jv[UF_JK_MGMT_CFG].type, UF_JT_STRING);
	CHECK_EQ(uf_jv[UF_JK_SYSTEM_CFG].type, UF_JT_STRING);
	CHECK_EQ(uf_json_uint(buf, UF_JK_SERVER_TIME, 3), 1792158536UL);

	uf_mgmt_parse((const char *)buf + uf_jv[UF_JK_MGMT_CFG].off);
	CHECK(!uf_mgmt.bad);
	CHECK(uf_mgmt.has_authkey);
	CHECK_EQ(uf_mgmt.authkey[0], 0x00);
	CHECK_EQ(uf_mgmt.authkey[15], 0xff);
	CHECK_EQ(uf_mgmt.use_gcm, 1);
	CHECK(uf_mgmt.has_cfgversion);
	CHECK_STR(uf_mgmt.cfgversion, "7066c2ce192f33b9");
	CHECK(!uf_mgmt.has_inform);

	CHECK_EQ(uf_sys_parse((const char *)buf + uf_jv[UF_JK_SYSTEM_CFG].off), UF_CE_NONE);
	CHECK_EQ(uf_sys.mgmt_vlan, 1);
	CHECK_EQ(uf_sys.nvlans, 2);
	CHECK_EQ(uf_sys.n_unknown, 0);
	CHECK_EQ(uf_plan_build(), UF_CE_NONE);
	CHECK_EQ(uf_plan.nv, 2);
	CHECK_EQ(uf_plan.vid[0], 1);
	CHECK_EQ(uf_plan.vid[1], 2);
	/* VLAN 1 (default untagged): port 1 and 4 by default, 3 and 5 tagged, 2 excluded */
	CHECK_EQ(uf_plan.member[0], 0x1d);
	CHECK_EQ(uf_plan.tagged[0], 0x14);
	/* VLAN 2 (default tagged): port 1 tagged, 2/3/5 untagged, 4 excluded */
	CHECK_EQ(uf_plan.member[1], 0x17);
	CHECK_EQ(uf_plan.tagged[1], 0x01);
	CHECK_EQ(uf_plan.pvid[0], 1);
	CHECK_EQ(uf_plan.pvid[1], 2);
	CHECK_EQ(uf_plan.pvid[2], 2);
	CHECK_EQ(uf_plan.pvid[3], 1);
	CHECK_EQ(uf_plan.pvid[4], 2);
	CHECK_EQ(uf_plan.has_name, 0x1f);
	CHECK_STR(uf_plan.name[4], "Port 5");
	CHECK_EQ(uf_plan.enable, 0x1f);
	/* uplink guard: ports 1 and 4 carry VLAN 1 untagged with PVID 1 */
	CHECK_EQ(uf_plan_protect(1), UF_CE_NONE);
	CHECK_EQ(uf_plan_protect(4), UF_CE_NONE);
	CHECK_EQ(uf_plan_protect(2), UF_CE_PROTECT_VLAN);
	CHECK_EQ(uf_plan_protect(3), UF_CE_PROTECT_VLAN);
	CHECK_EQ(uf_plan_protect(0), UF_CE_UPLINK_UNKNOWN);
	CHECK_EQ(uf_plan_protect(6), UF_CE_UPLINK_UNKNOWN);
}

static uint8_t sys(const char *s)
{
	uint8_t e = uf_sys_parse(s);
	return e ? e : uf_plan_build();
}

static void system_cfg_cases(void)
{
	/* access port: only native VLAN 10 on port 3 */
	CHECK_EQ(sys("switch.vlan.1.id=1\nswitch.vlan.1.mode=untagged\n"
		     "switch.vlan.2.id=10\nswitch.vlan.2.mode=exclude\n"
		     "switch.port.3.pvid=10\nswitch.vlan.1.port.3.mode=exclude\nswitch.vlan.2.port.3.mode=untagged\n"), 0);
	CHECK_EQ(uf_plan.member[0], 0x1b);
	CHECK_EQ(uf_plan.tagged[0], 0);
	CHECK_EQ(uf_plan.member[1], 0x04);
	CHECK_EQ(uf_plan.tagged[1], 0);
	CHECK_EQ(uf_plan.pvid[2], 10);
	/* native A + tagged B + C on port 3, CRLF line ends, VLAN index != VLAN id */
	CHECK_EQ(sys("switch.vlan.5.id=1\r\nswitch.vlan.5.mode=untagged\r\n"
		     "switch.vlan.7.id=20\r\nswitch.vlan.7.mode=exclude\r\nswitch.vlan.7.port.3.mode=tagged\r\n"
		     "switch.vlan.9.id=30\r\nswitch.vlan.9.mode=exclude\r\nswitch.vlan.9.port.3.mode=tagged\r\n"), 0);
	CHECK_EQ(uf_plan.nv, 3);
	CHECK_EQ(uf_plan.vid[1], 20);
	CHECK_EQ(uf_plan.member[1], 0x04);
	CHECK_EQ(uf_plan.tagged[1], 0x04);
	CHECK_EQ(uf_plan.vid[2], 30);
	CHECK_EQ(uf_plan.tagged[2], 0x04);
	CHECK_EQ(uf_plan.member[0], 0x1f);
	/* disabled VLAN is left out */
	CHECK_EQ(sys("switch.vlan.1.id=1\nswitch.vlan.1.mode=untagged\n"
		     "switch.vlan.2.id=2\nswitch.vlan.2.mode=tagged\nswitch.vlan.2.status=disabled\n"), 0);
	CHECK_EQ(uf_plan.nv, 1);
	/* ports outside 1..5 are ignored, unknown port keys are recorded */
	CHECK_EQ(sys("switch.vlan.1.id=1\nswitch.vlan.1.mode=untagged\nswitch.vlan.1.port.6.mode=tagged\n"
		     "switch.port.6.pvid=5\nswitch.port.2.isolation=enabled\nswitch.port.2.foo=1\n"
		     "switch.something_new=1\nunrelated.key=1\n# comment\n\n"), 0);
	CHECK_EQ(uf_plan.member[0], 0x1f);
	CHECK_EQ(uf_sys.n_unknown, 2);
	CHECK_STR(uf_sys.unknown[0], "switch.port.2.isolation");
	/* port disable as sent by Network 10.6.109 */
	CHECK_EQ(sys("switch.vlan.1.id=1\nswitch.vlan.1.mode=untagged\nswitch.port.2.status=disabled\n"), 0);
	CHECK_EQ(uf_plan.enable, 0x1d);
	CHECK_EQ(uf_sys.n_unknown, 0);
	CHECK_EQ(uf_plan_protect(1), UF_CE_NONE);
	CHECK_EQ(uf_plan_protect(2), UF_CE_PROTECT_DISABLE);
	CHECK_EQ(sys("switch.port.2.status=enabled\n"), 0);
	CHECK_EQ(uf_plan.enable, 0x1f);
	CHECK_EQ(sys("switch.port.2.status=off\n"), UF_CE_VALUE);
	/* errors reject the whole configuration */
	CHECK_EQ(sys("switch.vlan.1.id=0\n"), UF_CE_VALUE);
	CHECK_EQ(sys("switch.vlan.1.id=4095\n"), UF_CE_VALUE);
	CHECK_EQ(sys("switch.vlan.1.mode=hybrid\n"), UF_CE_VALUE);
	CHECK_EQ(sys("switch.vlan.1.mode=tagged\n"), UF_CE_VLAN_NO_ID);
	CHECK_EQ(sys("switch.vlan.1.id=5\nswitch.vlan.2.id=5\n"), UF_CE_DUP_VID);
	CHECK_EQ(sys("switch.vlan.x.id=5\n"), UF_CE_SYNTAX);
	CHECK_EQ(sys("switch.port.1.pvid=abc\n"), UF_CE_VALUE);
	CHECK_EQ(sys("switch.port.1.opmode=mirror\n"), UF_CE_OPMODE);
	CHECK_EQ(sys("switch.managementvlan=20\n"), UF_CE_MGMT_VLAN);
	CHECK_EQ(sys("switch.vlan.1.port.2.mode\n"), UF_CE_SYNTAX);
	{
		static char big[8192];
		int o = 0;
		for (int i = 1; i <= UF_MAX_VLANS + 1; i++)
			o += snprintf(big + o, sizeof(big) - o, "switch.vlan.%d.id=%d\nswitch.vlan.%d.mode=exclude\n", i, i, i);
		CHECK_EQ(sys(big), UF_CE_TOO_MANY_VLANS);
		CHECK(uf_sys.err_line > 0);
	}
	/* port name sanitising and truncation */
	CHECK_EQ(sys("switch.port.2.name=Desk \"PC\" \\ very long name that goes past the limit\n"), 0);
	CHECK_EQ(strlen(uf_plan.name[1]), UF_NAME_LEN - 1);
	CHECK(!strchr(uf_plan.name[1], '"') && !strchr(uf_plan.name[1], '\\'));
	CHECK_EQ(uf_plan.has_name, 0x02);
}

static void mgmt_cases(void)
{
	uf_mgmt_parse("authkey=0011\nuse_aes_gcm=false");
	CHECK(uf_mgmt.bad && !uf_mgmt.has_authkey && !uf_mgmt.use_gcm);
	uf_mgmt_parse("authkey=0011223344556677889900aabbccddeg\n");
	CHECK(uf_mgmt.bad && !uf_mgmt.has_authkey);
	uf_mgmt_parse("authkey=A0B1C2D3E4F5A6B7C8D9EAFBACBDCEDF\r\ncfgversion=abc\r\nuse_aes_gcm=true\r\n");
	CHECK(!uf_mgmt.bad && uf_mgmt.has_authkey && uf_mgmt.authkey[0] == 0xa0 && uf_mgmt.authkey[15] == 0xdf);
	CHECK_STR(uf_mgmt.cfgversion, "abc");
	CHECK_EQ(uf_mgmt.use_gcm, 1);
	uf_mgmt_parse("cfgversion=0123456789012345678901234567890123\n");
	CHECK(uf_mgmt.bad && !uf_mgmt.has_cfgversion);
	uf_mgmt_parse("inform_url=http://192.0.2.1:8080/inform\n");
	CHECK(uf_mgmt.has_inform && uf_mgmt.inform_ip[0] == 192 && uf_mgmt.inform_ip[3] == 1 && uf_mgmt.inform_port == 8080);
	uf_mgmt_parse("inform_url=http://192.0.2.1:65534/inform\n");
	CHECK(uf_mgmt.has_inform && uf_mgmt.inform_port == 65534);
	uf_mgmt_parse("inform_url=http://192.0.2.1:65536/inform\n");
	CHECK(!uf_mgmt.has_inform);
	CHECK_EQ(sys("switch.vlan.1.id=99999\n"), UF_CE_VALUE);
	uf_mgmt_parse("inform_url=http://unifi:8080/inform\n");
	CHECK(!uf_mgmt.has_inform);
	uf_mgmt_parse("inform_url=http://10.0.0.300:8080/inform\n");
	CHECK(!uf_mgmt.has_inform);
	uf_mgmt_parse("");
	CHECK(!uf_mgmt.has_authkey && !uf_mgmt.bad);
}

void test_cfg(void)
{
	json_basics();
	golden();
	system_cfg_cases();
	mgmt_cases();
}

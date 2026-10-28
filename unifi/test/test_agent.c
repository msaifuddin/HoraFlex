/*
 * Agent scenarios against a scripted controller: discovery, pending adoption,
 * CBC adoption, GCM provisioning, reboot, forget, tampering, oversize
 * responses, flash faults and nonce uniqueness.
 */
#include "fake_hw.h"
#include "../uf_agent.h"
#include "../uf_crypto.h"
#include "../uf_tnbu.h"
#include "../uf_json.h"
#include "../uf_msg.h"
#include "../uf_store.h"
#include "../uf_http.h"

static const uint8_t MAC[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t CTRL[4] = { 192, 0, 2, 1 };
static uint8_t defkey[16];
static uint8_t authkey[16];
static const char *AUTHKEY_HEX = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

static uint8_t req[8192];
static char plain[8192];
static uint8_t last_iv[16];
static int last_flags;

/* Decodes the request currently in the send window with key k */
static int take_request(const uint8_t *k)
{
	uint16_t n = uf_xfer.len;
	char *hdr_end;
	uint16_t off, pn;

	memcpy(req, uf_xfer.buf, n);
	req[n] = 0;
	static const char start[] = "POST /inform HTTP/1.1\r\nHost: 192.0.2.1:8080\r\n"
		"User-Agent: AirControl Agent v1.0\r\nAccept: application/x-binary\r\n"
		"Content-Type: application/x-binary\r\nContent-Length: ";
	if (strncmp((char *)req, start, sizeof(start) - 1))
		return -1;
	hdr_end = strstr((char *)req, "\r\n\r\n");
	if (!hdr_end)
		return -1;
	off = (uint16_t)(hdr_end + 4 - (char *)req);
	{
		char *cl = strstr((char *)req, "Content-Length: ");
		if (!cl || (uint16_t)atoi(cl + 16) != n - off)
			return -1;
	}
	memcpy(last_iv, req + off + 16, 16);
	last_flags = req[off + 15];
	uf_aes_setkey(k);
	pn = uf_tnbu_open(req + off, n - off, MAC, 0);
	if (pn == UF_TNBU_BAD)
		return -1;
	memcpy(plain, req + off + 40, pn);
	plain[pn] = 0;
	return pn;
}

/* Sends an HTTP response in odd-sized pieces, then ends the exchange */
static void deliver(const uint8_t *http, size_t n)
{
	size_t off = 0, step = 7;

	while (off < n && uf_resp.state < UF_RS_DONE) {
		size_t k = n - off < step ? n - off : step;
		uf_resp_feed(http + off, (uint16_t)k);
		off += k;
		step = step * 3 % 1400 + 1;
	}
	uf_agent_done(UF_XR_OK);
}

static uint8_t frame[16384], http[17000];

/* mode: 0 CBC default key, 1 CBC authkey, 2 GCM authkey */
static void respond(const char *json, int mode)
{
	uint8_t iv[16];
	uint16_t n = (uint16_t)strlen(json), len;
	int h;

	for (int i = 0; i < 16; i++)
		iv[i] = (uint8_t)(0xa0 + i + n);
	uf_aes_setkey(mode ? authkey : defkey);
	memcpy(frame + 40, json, n);
	len = uf_tnbu_seal(frame, MAC, iv, n, mode == 2);
	h = snprintf((char *)http, sizeof(http), "HTTP/1.1 200 OK\r\nContent-Type: application/x-binary\r\nContent-Length: %u\r\n\r\n", len);
	memcpy(http + h, frame, len);
	deliver(http, (size_t)h + len);
}

static void respond_status(int status)
{
	char s[128];
	int n = snprintf(s, sizeof(s), "HTTP/1.1 %d Not Found\r\nContent-Length: 0\r\n\r\n", status);
	deliver((uint8_t *)s, (size_t)n);
}

/* Runs the clock until the agent starts an exchange (or gives up) */
static int run_until_busy(int max_secs)
{
	for (int i = 0; i < max_secs * 10 && uf_agent.phase != UF_AP_BUSY; i++) {
		if (i % 10 == 0)
			fk_uptime++;
		uf_agent_poll();
	}
	return uf_agent.phase == UF_AP_BUSY;
}

static void boot(void)
{
	uf_agent_init(MAC, CTRL, 8080);
	uf_agent.discovery = 1;
}

static char setparam_adopt[4096];
static char setparam_cfg[8192];

static void build_messages(void)
{
	FILE *f = fopen("setparam_golden.json", "r");
	size_t n = f ? fread(setparam_cfg, 1, sizeof(setparam_cfg) - 1, f) : 0;
	if (f)
		fclose(f);
	setparam_cfg[n] = 0;
	/* the fixture carries a placeholder authkey; use the one this device was adopted with */
	{
		char *k = strstr(setparam_cfg, "authkey=00112233445566778899aabbccddeeff");
		if (k)
			memcpy(k + 8, AUTHKEY_HEX, 32);
	}
	/* adoption push: mgmt_cfg with the authkey plus a system_cfg that must be ignored (CBC) */
	snprintf(setparam_adopt, sizeof(setparam_adopt),
		 "{\"_type\":\"setparam\",\"cfgversion\":\"0000000000000001\",\"mgmt_cfg\":\"capability=notif\\n"
		 "cfgversion=0000000000000001\\nuse_aes_gcm=true\\nmgmt_url=https://192.0.2.1:8443/manage/site/default\\n"
		 "authkey=%s\\n\",\"system_cfg\":\"switch.vlan.1.id=1\\nswitch.vlan.1.mode=untagged\\n\","
		 "\"server_time_in_utc\":\"1792158536051\"}", AUTHKEY_HEX);
}

static int log_has(const char *s)
{
	return strstr(fk_log, s) != NULL;
}

static void discovery_golden(void)
{
	FILE *f = fopen("disc_stubd.txt", "r");
	char hex[1024] = { 0 };
	uint8_t exp[512], got[512];
	size_t en;
	uint16_t gn;

	CHECK(f && fgets(hex, sizeof(hex), f));
	if (f)
		fclose(f);
	en = ut_unhex(hex, exp, sizeof(exp));
	memset(&uf_self, 0, sizeof(uf_self));
	memcpy(uf_self.mac, MAC, 6);
	memcpy(uf_self.ip, (uint8_t[]){ 192, 0, 2, 20 }, 4);
	strcpy(uf_self.hostname, "hc-swtgw215as");
	uf_self.uptime = 1234;
	uf_self.disc_seq = 7;
	gn = uf_disc_build(got);
	CHECK_EQ(gn, en);
	CHECK(!memcmp(got, exp, en));
}

static void pending_and_adoption(void)
{
	fk_reset_flash();
	fk_uptime = 100;
	fk_log[0] = 0;
	boot();
	CHECK(!uf_self.adopted);
	/* discovery is off unless enabled */
	fk_disc_requests = 0;
	uf_agent_init(MAC, CTRL, 8080);
	CHECK(run_until_busy(5));
	CHECK_EQ(fk_disc_requests, 0);
	uf_agent_done(UF_XR_ABORT);
	boot();
	CHECK(run_until_busy(5));
	CHECK(fk_disc_requests > 0);
	CHECK(!memcmp(fk_tcp_ip, CTRL, 4) && fk_tcp_port == 8080);
	/* unadopted inform: CBC, default key, state 1 */
	CHECK(take_request(defkey) > 0);
	CHECK_EQ(last_flags, 0x01);
	CHECK(strstr(plain, "\"model\":\"USWED35\""));
	CHECK(strstr(plain, "\"state\":1"));
	CHECK(strstr(plain, "\"default\":true"));
	CHECK(strstr(plain, "\"cfgversion\":\"?\""));
	CHECK(strstr(plain, "\"mac\":\"02:11:22:33:44:55\""));
	CHECK(strstr(plain, "\"serial\":\"021122334455\""));
	CHECK(strstr(plain, "\"num_port\":5"));
	CHECK(strstr(plain, "\"port_poe\":false"));
	CHECK(!strstr(plain, "\"port_idx\":6"));
	CHECK(strstr(plain, "\"tx_packets\":72057594037927936"));	/* 2^56 */
	{
		FILE *f = fopen("inform_sample.json", "w");
		if (f) {
			fputs(plain, f);
			fclose(f);
		}
	}
	/* pending adoption: the controller answers 404 */
	respond_status(404);
	CHECK_EQ(uf_agent.phase, UF_AP_IDLE);
	CHECK_EQ(uf_agent.backoff, 0);
	CHECK(!uf_self.adopted);

	/* adoption push arrives under the default key */
	CHECK(run_until_busy(15));
	CHECK(take_request(defkey) > 0);
	fk_apply_count = 0;
	respond(setparam_adopt, 0);
	CHECK(uf_self.adopted);
	CHECK(uf_state.flags == (UF_SF_ADOPTED | UF_SF_GCM));
	CHECK(!memcmp(uf_state.authkey, authkey, 16));
	CHECK_EQ(fk_apply_count, 0);	/* CBC system_cfg is never applied */
	CHECK(log_has("system_cfg ignored"));
	CHECK(uf_store_slot != 0xff);
	CHECK(uf_agent.time_base != 0);
	CHECK_EQ(uf_state.epoch[7], 1);

	/* next inform is GCM under the authkey with nonce epoch 1, counter 0 */
	CHECK(run_until_busy(3));
	CHECK(take_request(authkey) > 0);
	CHECK_EQ(last_flags, 0x09);
	CHECK_EQ(last_iv[7], 1);
	CHECK_EQ(last_iv[15], 0);
	CHECK(strstr(plain, "\"state\":2"));
	CHECK(strstr(plain, "\"cfgversion\":\"?\""));
	CHECK(strstr(plain, "\"time\":"));
	/* full provisioning under GCM */
	fk_uplink = 1;
	respond(setparam_cfg, 2);
	CHECK_EQ(fk_apply_count, 1);
	CHECK_EQ(fk_applied.nv, 2);
	CHECK_EQ(fk_applied.member[1], 0x17);
	CHECK_EQ(uf_agent.last_cfg_err, 0);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");
	/* the cfgversion is reported back, with the second nonce */
	CHECK(run_until_busy(3));
	CHECK(take_request(authkey) > 0);
	CHECK(strstr(plain, "\"cfgversion\":\"7066c2ce192f33b9\""));
	CHECK_EQ(last_iv[15], 1);
	respond("{\"_type\":\"noop\",\"interval\":30,\"server_time_in_utc\":\"1792158600000\"}", 2);
	CHECK_EQ(uf_agent.interval, 30);
	CHECK_EQ(uf_agent.next_inform, fk_uptime + 30);
}

static void tamper_and_policy(void)
{
	/* continues from an adopted GCM device */
	int applied = fk_apply_count;
	char bad[8192];

	CHECK(run_until_busy(40));
	take_request(authkey);
	/* tampered GCM response: rejected before parsing, nothing applied */
	{
		uint8_t iv[16] = { 1 };
		uint16_t n = (uint16_t)strlen(setparam_cfg), len;
		int h;
		uf_aes_setkey(authkey);
		memcpy(frame + 40, setparam_cfg, n);
		len = uf_tnbu_seal(frame, MAC, iv, n, 1);
		frame[40 + 100] ^= 0x20;
		h = snprintf((char *)http, sizeof(http), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", len);
		memcpy(http + h, frame, len);
		deliver(http, (size_t)h + len);
	}
	CHECK_EQ(fk_apply_count, applied);
	CHECK_EQ(uf_tnbu_err, UF_TERR_AUTH);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");

	/* a CBC response under the default key is refused once on GCM */
	CHECK(run_until_busy(40));
	take_request(authkey);
	respond(setparam_adopt, 0);
	CHECK_EQ(uf_tnbu_err, UF_TERR_NEED_GCM);
	CHECK(!memcmp(uf_state.authkey, authkey, 16));

	/* config that cuts the uplink (port 1) off VLAN 1 is rejected whole */
	CHECK(run_until_busy(40));
	take_request(authkey);
	snprintf(bad, sizeof(bad), "{\"_type\":\"setparam\",\"cfgversion\":\"bad1\",\"system_cfg\":\""
		 "switch.vlan.1.id=1\\nswitch.vlan.1.mode=untagged\\nswitch.vlan.1.port.1.mode=exclude\\n"
		 "switch.vlan.2.id=2\\nswitch.vlan.2.mode=untagged\\n\"}");
	respond(bad, 2);
	CHECK_EQ(fk_apply_count, applied);
	CHECK_EQ(uf_agent.last_cfg_err, UF_CE_PROTECT_VLAN);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");

	/* hardware apply failure: cfgversion is not acknowledged */
	CHECK(run_until_busy(40));
	take_request(authkey);
	fk_apply_result = UF_AE_VERIFY;
	snprintf(bad, sizeof(bad), "{\"_type\":\"setparam\",\"cfgversion\":\"hwfail\",\"system_cfg\":\""
		 "switch.vlan.1.id=1\\nswitch.vlan.1.mode=untagged\\n\"}");
	respond(bad, 2);
	fk_apply_result = 0;
	CHECK_EQ(uf_agent.last_apply_err, UF_AE_VERIFY);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");

	/* unknown types and commands are logged and ignored */
	CHECK(run_until_busy(40));
	take_request(authkey);
	fk_log[0] = 0;
	respond("{\"_type\":\"upgrade\",\"url\":\"http://x/fw.bin\",\"version\":\"9.9.9\"}", 2);
	CHECK(log_has("firmware upgrade request ignored"));
	CHECK(uf_self.adopted);
	CHECK(run_until_busy(40));
	take_request(authkey);
	respond("{\"_type\":\"cmd\",\"cmd\":\"set-locate\"}", 2);
	CHECK(log_has("command ignored: set-locate"));
	CHECK(run_until_busy(40));
	take_request(authkey);
	respond("{\"_type\":\"brand-new\"}", 2);
	CHECK(log_has("unknown response type ignored: brand-new"));
	CHECK(uf_self.adopted);
}

static void oversize(void)
{
	static char big[12000];
	int o;

	CHECK(run_until_busy(40));
	take_request(authkey);
	o = snprintf(big, sizeof(big), "{\"_type\":\"setparam\",\"cfgversion\":\"huge\",\"system_cfg\":\"");
	while (o < 9000)
		o += snprintf(big + o, sizeof(big) - o, "switch.port.1.name=Port 1\\n");
	snprintf(big + o, sizeof(big) - o, "\"}");
	int before = fk_apply_count;
	fk_log[0] = 0;
	respond(big, 2);
	CHECK_EQ(uf_agent.n_oversize, 1);
	CHECK(uf_agent.last_oversize > UF_RX_SIZE);
	CHECK(log_has("response larger than buffer"));
	CHECK_EQ(fk_apply_count, before);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");
	CHECK(uf_self.adopted);
}

/* Network 10.6.109 answered a stale cfgversion with a full push whose
 * top-level cfgversion was "?"; the version comes from mgmt_cfg */
static void stale_cfgversion_push(void)
{
	char msg[1024];

	CHECK(run_until_busy(40));
	take_request(authkey);
	snprintf(msg, sizeof(msg), "{\"_type\":\"setparam\",\"cfgversion\":\"?\",\"mgmt_cfg\":\"cfgversion=7066c2ce192f33b9\\n"
		 "use_aes_gcm=true\\n\",\"system_cfg\":\"switch.vlan.1.id=1\\nswitch.vlan.1.mode=untagged\\n"
		 "switch.vlan.2.id=2\\nswitch.vlan.2.mode=exclude\\nswitch.port.2.pvid=2\\nswitch.vlan.1.port.2.mode=exclude\\n"
		 "switch.vlan.2.port.2.mode=untagged\\nswitch.port.3.pvid=2\\nswitch.vlan.1.port.3.mode=tagged\\n"
		 "switch.vlan.2.port.3.mode=untagged\\nswitch.vlan.2.port.5.mode=untagged\\nswitch.vlan.1.port.5.mode=tagged\\n"
		 "switch.port.5.pvid=2\\nswitch.vlan.2.port.1.mode=tagged\\n\"}");
	respond(msg, 2);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");
	CHECK_STR(uf_state.cfgversion, "7066c2ce192f33b9");
	/* without any usable version nothing is claimed */
	CHECK(run_until_busy(40));
	take_request(authkey);
	respond("{\"_type\":\"setparam\",\"cfgversion\":\"?\",\"system_cfg\":\"switch.vlan.1.id=1\\n"
		"switch.vlan.1.mode=untagged\\nswitch.vlan.2.id=2\\nswitch.vlan.2.mode=exclude\\nswitch.port.2.pvid=2\\n"
		"switch.vlan.1.port.2.mode=exclude\\nswitch.vlan.2.port.2.mode=untagged\\nswitch.port.3.pvid=2\\n"
		"switch.vlan.1.port.3.mode=tagged\\nswitch.vlan.2.port.3.mode=untagged\\nswitch.vlan.2.port.5.mode=untagged\\n"
		"switch.vlan.1.port.5.mode=tagged\\nswitch.port.5.pvid=2\\nswitch.vlan.2.port.1.mode=tagged\\n\"}", 2);
	CHECK_EQ(uf_self.cfgversion[0], 0);
	/* restore the golden config for the reboot test */
	CHECK(run_until_busy(40));
	take_request(authkey);
	respond(setparam_cfg, 2);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");
}

static void reboot_resumes(void)
{
	uint8_t epoch_before = uf_state.epoch[7];
	int applies = fk_apply_count;

	fk_uptime = 5;
	memset(&fk_applied, 0, sizeof(fk_applied));
	boot();
	CHECK(uf_self.adopted);
	/* the stored plan is applied at boot and its cfgversion reported, since the
	 * controller answers a rebooted device's "?" with noop */
	CHECK_EQ(fk_apply_count, applies + 1);
	CHECK_EQ(fk_applied.nv, 2);
	CHECK_EQ(fk_applied.member[1], 0x17);
	CHECK_EQ(fk_applied.pvid[1], 2);
	CHECK_STR(uf_self.cfgversion, "7066c2ce192f33b9");
	CHECK(run_until_busy(5));
	CHECK(take_request(authkey) > 0);
	CHECK_EQ(last_flags, 0x09);
	CHECK_EQ(last_iv[7], epoch_before + 1);
	CHECK_EQ(last_iv[15], 0);
	CHECK(strstr(plain, "\"cfgversion\":\"7066c2ce192f33b9\""));
	respond("{\"_type\":\"noop\",\"interval\":10}", 2);
	CHECK_EQ(fk_apply_count, applies + 1);
}

static void forget(void)
{
	uint8_t epoch[8];

	/* an authenticated reset clears the adoption */
	CHECK(run_until_busy(40));
	take_request(authkey);
	memcpy(epoch, uf_state.epoch, 8);
	respond("{\"_type\":\"setdefault\"}", 2);
	CHECK(!uf_self.adopted);
	CHECK_EQ(uf_state.flags, 0);
	/* ports are back to VLAN 1 untagged, PVID 1, enabled */
	CHECK_EQ(fk_applied.nv, 1);
	CHECK_EQ(fk_applied.vid[0], 1);
	CHECK_EQ(fk_applied.member[0], 0x1f);
	CHECK_EQ(fk_applied.tagged[0], 0);
	CHECK_EQ(fk_applied.pvid[2], 1);
	CHECK_EQ(fk_applied.enable, 0x1f);
	CHECK_EQ(uf_state.cfgversion[0], 0);
	CHECK(!memcmp(uf_state.epoch, epoch, 8));
	/* survives a reboot as unadopted, with the epoch kept */
	boot();
	CHECK(!uf_self.adopted);
	CHECK(!memcmp(uf_state.epoch, epoch, 8));
	CHECK(run_until_busy(5));
	CHECK(take_request(defkey) > 0);
	CHECK_EQ(last_flags, 0x01);
	/* an unauthenticated reset while adopted does nothing */
	respond(setparam_adopt, 0);
	CHECK(uf_self.adopted);
	CHECK(run_until_busy(5));
	take_request(authkey);
	respond("{\"_type\":\"setdefault\"}", 1);	/* CBC under the authkey: refused, GCM required */
	CHECK(uf_self.adopted);
}

static void backoff(void)
{
	uint16_t seen[6];

	fk_tcp_ok = 0;
	for (int i = 0; i < 6; i++) {
		uint16_t fails = uf_agent.n_fail;
		for (int t = 0; t < 2000 && uf_agent.n_fail == fails; t++) {
			if (t % 10 == 0)
				fk_uptime++;
			uf_agent_poll();
		}
		seen[i] = (uint16_t)(uf_agent.next_inform - fk_uptime);
	}
	fk_tcp_ok = 1;
	CHECK_EQ(seen[0], 10);
	CHECK_EQ(seen[1], 20);
	CHECK_EQ(seen[2], 40);
	CHECK_EQ(seen[3], 80);
	CHECK_EQ(seen[4], 120);
	CHECK_EQ(seen[5], 120);
	/* the exchange timeout aborts a hung transfer */
	CHECK(run_until_busy(200));
	CHECK(!uf_agent_expired());
	fk_uptime += UF_XFER_TIMEOUT + 1;
	CHECK(uf_agent_expired());
	uf_agent_done(UF_XR_ABORT);
	CHECK_EQ(uf_agent.phase, UF_AP_IDLE);
	/* a success resets the backoff */
	CHECK(run_until_busy(200));
	take_request(authkey);
	respond("{\"_type\":\"noop\",\"interval\":10}", 2);
	CHECK_EQ(uf_agent.backoff, 0);
}

static void store_faults(void)
{
	uint8_t epoch[8];
	uint32_t seq;

	/* torn record write: the previous slot still loads */
	memcpy(epoch, uf_state.epoch, 8);
	seq = uf_state.seq;
	fk_write_budget = 20;
	CHECK(!uf_store_commit());
	fk_write_budget = -1;
	uf_store_load();
	CHECK_EQ(uf_state.seq, seq);
	CHECK(!memcmp(uf_state.epoch, epoch, 8));
	/* interrupted erase: same */
	fk_erase_fail = 1;
	CHECK(!uf_store_commit());
	fk_erase_fail = 0;
	uf_store_load();
	CHECK_EQ(uf_state.seq, seq);
	/* and the torn sector is reused afterwards */
	CHECK(uf_store_commit());
	uf_store_load();
	CHECK_EQ(uf_state.seq, seq + 1);
	/* foreign data in the target sector blocks every write */
	{
		uint8_t target = uf_store_slot ? 0 : 1;
		uint32_t a = target ? UF_STORE_SLOT_B : UF_STORE_SLOT_A;
		fk_flash[a + 0x800] = 0x12;
		CHECK(!uf_store_commit());
		CHECK(uf_store_foreign);
		CHECK_EQ(fk_flash[a + 0x800], 0x12);
		uf_store_foreign = 0;
		fk_flash[a + 0x800] = 0xff;
	}
	/* CRC catches a flipped bit */
	{
		uint8_t r[UF_REC_LEN];
		uf_store_encode(r);
		CHECK(uf_store_decode(r));
		r[25] ^= 4;
		CHECK(!uf_store_decode(r));
	}
	/* CRC-32 check value */
	CHECK_EQ(uf_crc32((const uint8_t *)"123456789", 9), 0xcbf43926UL);
	/* nothing outside the two slots was ever touched */
	for (uint32_t a = 0; a < FK_FLASH_SIZE; a += 0x1000) {
		if (a == UF_STORE_SLOT_A || a == UF_STORE_SLOT_B)
			continue;
		int blank = 1;
		for (int i = 0; i < 0x1000 && blank; i++)
			blank = fk_flash[a + i] == 0xff;
		if (!blank) {
			CHECK(blank);
			break;
		}
	}
}

/*
 * Nonce uniqueness: many boots with power lost at random points of the epoch
 * write. Every (key, nonce) pair the device sends must be new.
 */
#define MAX_NONCES 20000
static uint8_t nonces[MAX_NONCES][32];
static int n_nonces;

static void nonce_uniqueness(void)
{
	unsigned seed = 12345;
	int dup = 0, gcm_msgs = 0;

	fk_reset_flash();
	n_nonces = 0;
	fk_uptime = 1;
	boot();
	CHECK(run_until_busy(5));
	take_request(defkey);
	respond(setparam_adopt, 0);
	CHECK(uf_self.adopted);
	for (int b = 0; b < 300; b++) {
		seed = seed * 1103515245 + 12345;
		/* power loss during this boot's epoch write in a third of the boots */
		if ((seed >> 16) % 3 == 0)
			fk_write_budget = (long)((seed >> 8) % UF_REC_LEN);
		fk_uptime = 1;
		boot();
		for (int m = 0; m < 4; m++) {
			if (!run_until_busy(60))
				break;
			if (take_request(authkey) < 0) {
				uf_agent_done(UF_XR_ABORT);
				continue;
			}
			if (last_flags & UF_TNBU_GCM) {
				gcm_msgs++;
				memcpy(nonces[n_nonces], authkey, 16);
				memcpy(nonces[n_nonces] + 16, last_iv, 16);
				for (int k = 0; k < n_nonces && !dup; k++)
					dup = !memcmp(nonces[k], nonces[n_nonces], 32);
				if (n_nonces < MAX_NONCES - 1)
					n_nonces++;
			}
			respond("{\"_type\":\"noop\",\"interval\":10}", 2);
		}
		/* power comes back for the next boot */
		fk_write_budget = -1;
	}
	CHECK(!dup);
	if (gcm_msgs <= 300)
		fprintf(stderr, "gcm_msgs=%d n_nonces=%d\n", gcm_msgs, n_nonces);
	CHECK(gcm_msgs > 300);
}

static void request_header(void)
{
	/* the longest Host value must still produce a complete header */
	static uint8_t b[UF_HTTP_HDR_MAX + 64];
	const uint8_t ip[4] = { 255, 255, 255, 255 };
	uint16_t s = uf_http_request(b, UF_HTTP_HDR_MAX, 65535, ip, 65534);
	char h[UF_HTTP_HDR_MAX + 1];

	CHECK(s != 0xffff);
	if (s == 0xffff)
		return;
	memcpy(h, b + s, UF_HTTP_HDR_MAX - s);
	h[UF_HTTP_HDR_MAX - s] = 0;
	CHECK(strstr(h, "Host: 255.255.255.255:65534\r\n") != NULL);
	CHECK(strstr(h, "Content-Length: 65535\r\n\r\n") != NULL);
	CHECK(!strncmp(h + strlen(h) - 4, "\r\n\r\n", 4));
	/* too little room in front of the body is reported, not truncated */
	CHECK_EQ(uf_http_request(b, 100, 10, ip, 80), 0xffff);
}

static void xfer_and_resp(void)
{
	uint8_t buf[3000];
	uint16_t n;

	/* send window: chunks of MSS, advance only on ACK, rexmit repeats the chunk */
	uf_xfer_start(buf, 3000);
	n = uf_xfer_next(1460);
	CHECK_EQ(n, 1460);
	CHECK_EQ(uf_xfer_next(1460), 0);		/* nothing new while in flight */
	CHECK_EQ(uf_xfer_rexmit(), 1460);
	CHECK_EQ(uf_xfer.acked, 0);
	uf_xfer_acked();
	CHECK_EQ(uf_xfer.acked, 1460);
	CHECK_EQ(uf_xfer_next(1460), 1460);
	CHECK_EQ(uf_xfer_rexmit(), 1460);
	uf_xfer_acked();
	CHECK(!uf_xfer_done());
	CHECK_EQ(uf_xfer_next(536), 80);
	uf_xfer_acked();
	CHECK(uf_xfer_done());
	CHECK_EQ(uf_xfer_next(1460), 0);
	uf_xfer_acked();				/* a duplicate ACK changes nothing */
	CHECK_EQ(uf_xfer.acked, 3000);

	/* response parser */
	{
		static uint8_t body[64];
		const char *r = "HTTP/1.1 200 OK\r\ncontent-LENGTH:  5\r\nX-Long: "
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			"\r\n\r\nhelloEXTRA";
		uf_resp_start(body, sizeof(body));
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_DONE);
		CHECK_EQ(uf_resp.status, 200);
		CHECK_EQ(uf_resp.body, 5);
		CHECK(!memcmp(body, "hello", 5));

		uf_resp_start(body, sizeof(body));
		r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_ERROR);
		CHECK_EQ(uf_resp.err, UF_RE_CHUNKED);

		uf_resp_start(body, sizeof(body));
		r = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc";
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_BODY);
		CHECK_EQ(uf_resp_closed(), UF_RS_ERROR);
		CHECK_EQ(uf_resp.err, UF_RE_TRUNCATED);

		uf_resp_start(body, sizeof(body));
		r = "HTTP/1.0 200 OK\r\n\r\nno length";
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_BODY);
		CHECK_EQ(uf_resp_closed(), UF_RS_DONE);
		CHECK_EQ(uf_resp.body, 9);

		uf_resp_start(body, 8);
		r = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n01234567890123456789";
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_DONE);
		CHECK(uf_resp.oversize);
		CHECK_EQ(uf_resp.seen, 20);
		CHECK_EQ(uf_resp.body, 0);

		uf_resp_start(body, sizeof(body));
		r = "SSH-2.0-x\r\n";
		CHECK_EQ(uf_resp_feed((const uint8_t *)r, (uint16_t)strlen(r)), UF_RS_ERROR);
		CHECK_EQ(uf_resp.err, UF_RE_STATUS);
	}
}

void test_agent(void)
{
	ut_unhex("ba86f2bbe107c7c57eb5f2690775c712", defkey, 16);
	ut_unhex(AUTHKEY_HEX, authkey, 16);
	build_messages();
	discovery_golden();
	request_header();
	xfer_and_resp();
	pending_and_adoption();
	tamper_and_policy();
	oversize();
	stale_cfgversion_push();
	reboot_resumes();
	forget();
	backoff();
	store_faults();
	nonce_uniqueness();
}

/*
 * UniFi device agent
 *
 * Unadopted, the device announces itself over UDP and informs with AES-CBC
 * under the public default key MD5("ubnt"). Adoption arrives as a setparam
 * whose mgmt_cfg carries the device authkey and use_aes_gcm; it is stored
 * before the device switches keys. From then on informs use AES-GCM and
 * switch configuration (system_cfg) is only applied from responses whose
 * GCM tag verified under that key.
 *
 * GCM nonces are a persistent 64-bit epoch followed by a 64-bit counter. The
 * epoch is incremented and written to flash before the first GCM message of
 * each boot (and with a new key), and the counter only lives in RAM, so a
 * nonce never repeats under one key, including across power loss: an epoch
 * is only used after the record holding it was written and read back.
 */
#include "uf_agent.h"
#include "uf_crypto.h"
#include "uf_tnbu.h"
#include "uf_json.h"
#include "uf_cfg.h"
#include "uf_msg.h"
#include "uf_store.h"
#include "uf_http.h"
#include "uf_hw.h"
#include "uf_ident.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

/* Request layout in uf_tx: HTTP header | TNBU header | payload | pad or tag */
#define TX_FRAME	UF_HTTP_HDR_MAX
#define TX_PLAIN	(TX_FRAME + UF_TNBU_HDR)

#define MODE_CBC_DEFAULT	0
#define MODE_CBC_AUTHKEY	1
#define MODE_GCM		2

#define DISC_INTERVAL		10

UF_X struct uf_agent uf_agent;
UF_X uint8_t uf_rx[UF_RX_SIZE];
UF_X uint8_t uf_tx[UF_TX_SIZE];

/* MD5("ubnt") */
static UF_C const uint8_t default_key[16] = {
	0xba, 0x86, 0xf2, 0xbb, 0xe1, 0x07, 0xc7, 0xc5, 0x7e, 0xb5, 0xf2, 0x69, 0x07, 0x75, 0xc7, 0x12
};

static UF_X uint8_t a_key[16];
static UF_X uint8_t a_iv[16];
static UF_X uint8_t a_mode;
static UF_X uint16_t a_n;
static UF_X uint32_t a_now;
static UF_X uint8_t * UF_X a_p;		/* response plaintext */
static UF_X uint8_t a_auth;		/* response GCM tag verified */

static UF_C const char build_version[] = UNIFI_VERSION;

/* A version string the controller may give us: digits and dots, starting with a digit */
static uint8_t version_ok(UF_X const char * UF_X v)
{
	UF_X uint8_t i;

	if (!v || v[0] < '0' || v[0] > '9')
		return 0;
	for (i = 0; v[i]; i++)
		if (i >= UF_FWVER_LEN - 1 || ((v[i] < '0' || v[i] > '9') && v[i] != '.'))
			return 0;
	return 1;
}

/* The version reported: one accepted from an upgrade request, else the built-in one */
static void set_version(void)
{
	UF_X uint8_t i;

	if (version_ok(uf_state.fwver)) {
		uf_memcpy((UF_X uint8_t *)uf_self.version, (UF_X uint8_t *)uf_state.fwver, UF_FWVER_LEN);
		return;
	}
	for (i = 0; build_version[i] && i < UF_FWVER_LEN - 1; i++)
		uf_self.version[i] = build_version[i];
	uf_self.version[i] = 0;
}

static uint8_t adopted(void)
{
	return (uf_state.flags & UF_SF_ADOPTED) != 0;
}

static uint8_t mode_now(void)
{
	if (!adopted())
		return MODE_CBC_DEFAULT;
	return (uf_state.flags & UF_SF_GCM) ? MODE_GCM : MODE_CBC_AUTHKEY;
}

static void use_key(UF_X uint8_t mode)
{
	UF_X uint8_t i;

	for (i = 0; i < 16; i++)
		a_key[i] = mode == MODE_CBC_DEFAULT ? default_key[i] : uf_state.authkey[i];
	uf_aes_setkey(a_key);
}

static void be_inc(UF_X uint8_t * UF_X p)
{
	UF_X uint8_t i = 8;

	while (i--)
		if (++p[i])
			break;
}

/* Moves to a fresh nonce epoch and stores it. Returns 1 once it is on flash */
static uint8_t new_epoch(void)
{
	be_inc(uf_state.epoch);
	if (!uf_store_commit()) {
		uh_log("unifi: could not store nonce epoch, GCM paused", 0);
		uf_agent.epoch_ok = 0;
		return 0;
	}
	uf_memset(uf_agent.nonce_ctr, 0, 8);
	uf_agent.epoch_ok = 1;
	return 1;
}

static void schedule(UF_X uint16_t secs)
{
	uf_agent.next_inform = a_now + secs;
}

static void succeeded(void)
{
	uf_agent.n_ok++;
	uf_agent.backoff = 0;
	schedule(uf_agent.interval);
}

static void failed(void)
{
	uf_agent.n_fail++;
	if (!uf_agent.backoff)
		uf_agent.backoff = uf_agent.interval;
	else if (uf_agent.backoff < UF_BACKOFF_MAX)
		uf_agent.backoff <<= 1;
	if (uf_agent.backoff > UF_BACKOFF_MAX)
		uf_agent.backoff = UF_BACKOFF_MAX;
	schedule(uf_agent.backoff);
}

void uf_agent_init(UF_X const uint8_t * UF_X mac, UF_X const uint8_t * UF_X ctrl_ip, UF_X uint16_t ctrl_port) UF_BANKED
{
	uf_memset((UF_X uint8_t *)&uf_agent, 0, sizeof(uf_agent));
	uf_memset((UF_X uint8_t *)&uf_self, 0, sizeof(uf_self));
	uf_memset((UF_X uint8_t *)&uf_live, 0, sizeof(uf_live));
	uf_live.nv = 0xff;
	uf_memcpy(uf_self.mac, mac, 6);
	uf_store_load();
	set_version();
	/* An adopted device keeps the controller it was adopted by */
	if (!adopted() || !(uf_state.ctrl_ip[0] | uf_state.ctrl_ip[1] | uf_state.ctrl_ip[2] | uf_state.ctrl_ip[3])) {
		uf_memcpy(uf_state.ctrl_ip, ctrl_ip, 4);
		uf_state.ctrl_port = ctrl_port;
	}
	uf_self.adopted = adopted();
	/* Re-apply the configuration this device last had from the controller,
	 * and report its cfgversion: the controller does not re-provision a
	 * device that rebooted (it answers "?" with noop). */
	if (adopted() && (uf_state.flags & UF_SF_PLAN)) {
		if (uh_apply() == UF_AE_NONE) {
			uf_memcpy((UF_X uint8_t *)&uf_live, (UF_X uint8_t *)&uf_plan, sizeof(uf_plan));
			uf_memcpy((UF_X uint8_t *)uf_self.cfgversion, (UF_X uint8_t *)uf_state.cfgversion, UF_CFGVER_LEN);
			uf_agent.applied = 1;
			uh_log_x("unifi: stored config applied, cfgversion ", uf_self.cfgversion);
		} else {
			uh_log("unifi: stored config could not be applied", 0);
		}
	}
	uf_agent.interval = 10;
	uf_agent.enabled = 1;
	uf_agent.phase = UF_AP_IDLE;
	a_now = uh_uptime();
	uf_agent.next_inform = a_now + 2;
	uf_agent.next_disc = a_now;
}

uint8_t uf_agent_expired(void) UF_BANKED
{
	return uf_agent.phase == UF_AP_BUSY && uh_uptime() - uf_agent.busy_since > UF_XFER_TIMEOUT;
}

static void start_inform(void)
{
	UF_X uint16_t len, start;

	if (!(uf_state.ctrl_ip[0] | uf_state.ctrl_ip[1] | uf_state.ctrl_ip[2] | uf_state.ctrl_ip[3])) {
		schedule(60);
		return;
	}
	a_mode = mode_now();
	if (a_mode == MODE_GCM && !uf_agent.epoch_ok && !new_epoch()) {
		schedule(30);
		return;
	}
	uf_self.uptime = a_now;
	uf_self.time = uf_agent.time_base ? uf_agent.time_base + a_now : 0;
	uf_self.uplink = uh_uplink();
	uf_memcpy(uf_self.inform_ip, uf_state.ctrl_ip, 4);
	uf_self.inform_port = uf_state.ctrl_port;
	uf_self.adopted = adopted();
	uh_telemetry();
	a_n = uf_inform_build(uf_tx + TX_PLAIN, UF_TX_SIZE - TX_PLAIN - 16);
	if (!a_n) {
		uh_log("unifi: inform document too large", 0);
		schedule(uf_agent.interval);
		return;
	}
	if (a_mode == MODE_GCM) {
		uf_memcpy(a_iv, uf_state.epoch, 8);
		uf_memcpy(a_iv + 8, uf_agent.nonce_ctr, 8);
		be_inc(uf_agent.nonce_ctr);
	} else {
		uh_random(a_iv, 16);
	}
	use_key(a_mode);
	len = uf_tnbu_seal(uf_tx + TX_FRAME, uf_self.mac, a_iv, a_n, a_mode == MODE_GCM);
	start = uf_http_request(uf_tx, TX_FRAME, len, uf_state.ctrl_ip, uf_state.ctrl_port);
	if (start == 0xffff) {
		uh_log("unifi: request header too long", 0);
		schedule(uf_agent.interval);
		return;
	}
	uf_xfer_start(uf_tx + start, TX_FRAME + len - start);
	uf_resp_start(uf_rx, UF_RX_SIZE);
	uf_agent.n_inform++;
	uf_agent.phase = UF_AP_BUSY;
	uf_agent.busy_since = a_now;
	if (!uh_tcp_open(uf_state.ctrl_ip, uf_state.ctrl_port))
		uf_agent_done(UF_XR_CONNECT);
}

void uf_agent_poll(void) UF_BANKED
{
	if (!uf_agent.enabled || uf_agent.phase != UF_AP_IDLE)
		return;
	if (!uh_ip(uf_self.ip))
		return;
	a_now = uh_uptime();
	if (uf_agent.discovery && !adopted() && (int32_t)(a_now - uf_agent.next_disc) >= 0) {
		uf_self.uptime = a_now;
		uf_self.adopted = 0;
		uh_udp_discovery();
		uf_agent.next_disc = a_now + DISC_INTERVAL;
	}
	if ((int32_t)(a_now - uf_agent.next_inform) >= 0)
		start_inform();
}

uint8_t uf_agent_forget(void) UF_BANKED
{
	UF_X uint8_t ok;

	uf_state.flags = 0;
	uf_memset(uf_state.authkey, 0, 16);
	uf_state.cfgversion[0] = 0;
	uf_state.fwver[0] = 0;
	ok = uf_store_commit();	/* the epoch is kept: it must never go back */
	set_version();
	/* Back to plain switching on the five ports */
	uf_plan_default();
	if (uh_apply() == UF_AE_NONE)
		uf_memcpy((UF_X uint8_t *)&uf_live, (UF_X uint8_t *)&uf_plan, sizeof(uf_plan));
	uf_self.adopted = 0;
	uf_self.cfgversion[0] = 0;
	uf_agent.applied = 0;
	uf_agent.interval = 10;
	uf_agent.next_disc = uh_uptime();
	uf_agent.next_inform = uf_agent.next_disc + 1;
	uh_log("unifi: adoption cleared, stored: ", ok);
	return ok;
}

static uint8_t type_is(UF_C const char * UF_X s)
{
	UF_X const char * UF_X t = (UF_X const char *)a_p + uf_jv[UF_JK_TYPE].off;

	if (uf_jv[UF_JK_TYPE].type != UF_JT_STRING)
		return 0;
	while (*s && *s == *t) {
		s++;
		t++;
	}
	return !*s && !*t;
}

static UF_X const char *jstr(UF_X uint8_t k)
{
	return uf_jv[k].type == UF_JT_STRING ? (UF_X const char *)a_p + uf_jv[k].off : 0;
}

static void adopt(void)
{
	UF_X uint8_t was = adopted();

	uf_memcpy(uf_state.authkey, uf_mgmt.authkey, 16);
	uf_state.flags = UF_SF_ADOPTED | (uf_mgmt.use_gcm ? UF_SF_GCM : 0) | (was ? uf_state.flags & UF_SF_PLAN : 0);
	/* A new key gets a new epoch; commit stores key and epoch together */
	be_inc(uf_state.epoch);
	if (!uf_store_commit()) {
		uh_log("unifi: could not store adoption, staying as before", 0);
		uf_store_load();
		uf_agent.epoch_ok = 0;
		return;
	}
	uf_memset(uf_agent.nonce_ctr, 0, 8);
	uf_agent.epoch_ok = 1;
	uf_self.adopted = 1;
	if (!was)
		uf_self.cfgversion[0] = 0;
	uh_log("unifi: adopted, GCM: ", uf_mgmt.use_gcm);
	schedule(1);
}

static void apply_system(void)
{
	UF_X uint8_t err, i;
	UF_X const char * UF_X ver;

	err = uf_sys_parse(jstr(UF_JK_SYSTEM_CFG));
	for (i = 0; i < uf_sys.n_unknown; i++)
		uh_log_x("unifi: unhandled key ", uf_sys.unknown[i]);
	if (!err)
		err = uf_plan_build();
	if (!err)
		err = uf_plan_protect(uh_uplink());
	uf_agent.last_cfg_err = err;
	if (err) {
		uh_log("unifi: config rejected, error ", err);
		uh_log("unifi: at system_cfg line ", uf_sys.err_line);
		return;
	}
	err = uh_apply();
	uf_agent.last_apply_err = err;
	if (err) {
		uh_log("unifi: applying config failed, error ", err);
		return;
	}
	uf_memcpy((UF_X uint8_t *)&uf_live, (UF_X uint8_t *)&uf_plan, sizeof(uf_plan));
	uf_agent.applied = 1;
	/* mgmt_cfg carries the real version; the top-level field can be "?"
	 * (seen when the device reported a stale cfgversion) */
	ver = uf_mgmt.has_cfgversion ? uf_mgmt.cfgversion : jstr(UF_JK_CFGVERSION);
	if (ver && ver[0] == '?' && !ver[1])
		ver = 0;
	uf_self.cfgversion[0] = 0;
	if (ver) {
		for (i = 0; i < UF_CFGVER_LEN - 1 && ver[i]; i++)
			uf_self.cfgversion[i] = ver[i];
		uf_self.cfgversion[i] = 0;
	}
	uh_log_x("unifi: config applied, cfgversion ", uf_self.cfgversion);
	/* Keep it for the next boot */
	uf_memcpy((UF_X uint8_t *)uf_state.cfgversion, (UF_X uint8_t *)uf_self.cfgversion, UF_CFGVER_LEN);
	uf_state.flags |= UF_SF_PLAN;
	if (!uf_store_commit())
		uh_log("unifi: could not store the applied config", 0);
	schedule(1);
}

static void setparam(void)
{
	if (jstr(UF_JK_MGMT_CFG)) {
		uf_mgmt_parse(jstr(UF_JK_MGMT_CFG));
		if (uf_mgmt.bad)
			uh_log("unifi: malformed mgmt_cfg value ignored", 0);
		/*
		 * Unadopted, the default key is all the protection UniFi offers for
		 * adoption. Once adopted, only a response under our own key may
		 * change the key or turn GCM on.
		 */
		if (!adopted()) {
			if (uf_mgmt.has_authkey)
				adopt();
		} else if (a_mode != MODE_CBC_DEFAULT) {
			if (!uf_mgmt.has_authkey && uf_mgmt.use_gcm && !(uf_state.flags & UF_SF_GCM)) {
				uf_memcpy(uf_mgmt.authkey, uf_state.authkey, 16);
				uf_mgmt.has_authkey = 1;
			}
			if (uf_mgmt.has_authkey && (!uf_memeq_ct(uf_mgmt.authkey, uf_state.authkey, 16)
						    || (uf_mgmt.use_gcm && !(uf_state.flags & UF_SF_GCM))))
				adopt();
		}
		if (uf_mgmt.has_inform && adopted() && a_mode != MODE_CBC_DEFAULT
		    && (uf_memeq_ct(uf_mgmt.inform_ip, uf_state.ctrl_ip, 4) == 0 || uf_mgmt.inform_port != uf_state.ctrl_port)) {
			uf_memcpy(uf_state.ctrl_ip, uf_mgmt.inform_ip, 4);
			uf_state.ctrl_port = uf_mgmt.inform_port;
			if (!uf_store_commit())
				uh_log("unifi: could not store new inform address", 0);
		}
	} else {
		uf_memset((UF_X uint8_t *)&uf_mgmt, 0, sizeof(uf_mgmt));
	}
	if (jstr(UF_JK_SYSTEM_CFG)) {
		if (a_auth)
			apply_system();
		else
			uh_log("unifi: system_cfg ignored, response not GCM-authenticated", 0);
	}
}

/*
 * The controller asks the device to install a USWED35 firmware. Nothing is
 * downloaded or installed: that firmware is for different hardware. The
 * requested version is stored and reported, so the controller considers the
 * upgrade done instead of retrying it (with automatic updates, every night).
 */
static void upgrade(void)
{
	UF_X const char * UF_X v = jstr(UF_JK_VERSION);
	UF_X uint8_t i;

	if (!a_auth || !adopted() || !version_ok(v)) {
		uh_log("unifi: firmware upgrade request ignored", 0);
		return;
	}
	for (i = 0; v[i]; i++)
		uf_state.fwver[i] = v[i];
	uf_state.fwver[i] = 0;
	if (!uf_store_commit())
		uh_log("unifi: could not store the upgraded version", 0);
	set_version();
	uh_log_x("unifi: upgrade request accepted, nothing installed, now reporting ", uf_self.version);
	schedule(1);
}

static uint8_t is_reset_type(void)
{
	return type_is("setdefault") || type_is("reset") || type_is("forget") || type_is("delete")
		|| type_is("remove") || type_is("restore-default");
}

static void handle_response(void)
{
	UF_X uint32_t v;

	use_key(a_mode);
	a_n = uf_tnbu_open(uf_rx, uf_resp.body, uf_self.mac, a_mode == MODE_GCM);
	if (a_n == UF_TNBU_BAD) {
		uf_agent.n_auth_fail++;
		uf_agent.last_terr = uf_tnbu_err;
		uh_log("unifi: response rejected, reason ", uf_tnbu_err);
		return;
	}
	a_auth = (uf_tnbu_flags & UF_TNBU_GCM) != 0;
	a_p = uf_rx + UF_TNBU_HDR;
	if (!uf_json_scan(a_p, a_n)) {
		uh_log("unifi: malformed response JSON, bytes ", a_n);
		return;
	}
	v = uf_json_uint(a_p, UF_JK_SERVER_TIME, 3);
	if (v != 0xffffffffUL && v > a_now)
		uf_agent.time_base = v - a_now;

	if (type_is("noop")) {
		v = uf_json_uint(a_p, UF_JK_INTERVAL, 0);
		if (v != 0xffffffffUL)
			uf_agent.interval = v < UF_INTERVAL_MIN ? UF_INTERVAL_MIN : (v > UF_INTERVAL_MAX ? UF_INTERVAL_MAX : (uint16_t)v);
		schedule(uf_agent.interval);
	} else if (type_is("setparam")) {
		setparam();
	} else if (is_reset_type()) {
		if (a_auth && adopted())
			uf_agent_forget();
		else
			uh_log("unifi: reset ignored, response not GCM-authenticated", 0);
	} else if (type_is("upgrade")) {
		upgrade();
	} else if (type_is("cmd")) {
		if (jstr(UF_JK_CMD))
			uh_log_x("unifi: controller command ignored: ", jstr(UF_JK_CMD));
		else
			uh_log("unifi: controller command ignored", 0);
	} else if (jstr(UF_JK_TYPE)) {
		uh_log_x("unifi: unknown response type ignored: ", jstr(UF_JK_TYPE));
	} else {
		uh_log("unifi: response without _type ignored", 0);
	}
}

void uf_agent_done(UF_X uint8_t result) UF_BANKED
{
	if (uf_agent.phase != UF_AP_BUSY)
		return;
	uf_agent.phase = UF_AP_IDLE;
	a_now = uh_uptime();
	if (result != UF_XR_OK || uf_resp.state != UF_RS_DONE) {
		failed();
		return;
	}
	uf_agent.last_status = uf_resp.status;
	if (uf_resp.status != 200) {
		/* An unadopted device gets 404 until someone adopts it */
		if (uf_resp.status == 404 && !adopted())
			succeeded();
		else
			failed();
		return;
	}
	succeeded();
	if (uf_resp.seen > uf_agent.max_resp)
		uf_agent.max_resp = uf_resp.seen > 0xffff ? 0xffff : (uint16_t)uf_resp.seen;
	if (uf_resp.oversize) {
		uf_agent.n_oversize++;
		uf_agent.last_oversize = uf_resp.seen;
		uh_log("unifi: response larger than buffer, bytes needed ", uf_resp.seen > 0xffff ? 0xffff : (uint16_t)uf_resp.seen);
		return;
	}
	if (uf_resp.body)
		handle_response();
}

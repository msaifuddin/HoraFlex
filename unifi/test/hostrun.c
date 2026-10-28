/*
 * Runs the agent on a host against a real controller, with POSIX sockets and
 * a simulated switch. For protocol checks before flashing; not a unit test.
 *
 *   hostrun <controller-ip> <seconds> [state-file]
 *
 * Uses a locally administered MAC so it never collides with real hardware.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include "ut.h"
#include "../uf_agent.h"
#include "../uf_hw.h"
#include "../uf_http.h"
#include "../uf_msg.h"
#include "../uf_cfg.h"
#include "../uf_store.h"
#include "../uf_json.h"
#include "../uf_tnbu.h"

int ut_fail, ut_pass;
size_t ut_unhex(const char *s, uint8_t *out, size_t max) { (void)s; (void)out; (void)max; return 0; }

static uint8_t MAC[6] = { 0x02, 0x83, 0x72, 0x25, 0x05, 0x02 };
static uint8_t flash[0x200000];
static const char *state_file;
static time_t t0;
static uint8_t my_ip[4];
static int pending_tcp, pending_disc;
static uint8_t tcp_ip[4];
static uint16_t tcp_port;

static void save_flash(void)
{
	FILE *f;
	if (!state_file || !(f = fopen(state_file, "wb")))
		return;
	fwrite(flash + UF_STORE_SLOT_A, 1, 0x2000, f);
	fclose(f);
}

uint8_t uh_flash_read(uint32_t a, uint8_t *b, uint16_t n) { memcpy(b, flash + a, n); return 1; }
uint8_t uh_flash_erase(uint32_t a) { memset(flash + a, 0xff, 0x1000); save_flash(); return 1; }
uint8_t uh_flash_write(uint32_t a, const uint8_t *b, uint16_t n)
{
	for (uint16_t i = 0; i < n; i++)
		flash[a + i] &= b[i];
	save_flash();
	return 1;
}
uint32_t uh_uptime(void) { return (uint32_t)(time(NULL) - t0) + 1; }
void uh_random(uint8_t *b, uint8_t n) { arc4random_buf(b, n); }
void uh_log(const char *m, uint16_t v) { printf("[%4u] %s%u\n", uh_uptime(), m, v); }
void uh_log_x(const char *m, const char *s) { printf("[%4u] %s%s\n", uh_uptime(), m, s); }
uint8_t uh_ip(uint8_t *ip) { memcpy(ip, my_ip, 4); return 1; }
void uh_telemetry(void)
{
	for (int i = 0; i < UF_NPORTS; i++) {
		memset(&uf_tele.p[i], 0, sizeof(uf_tele.p[i]));
		uf_tele.p[i].enabled = 1;
		uf_tele.p[i].up = i == 0;
		uf_tele.p[i].speed = i == 0 ? 1000 : 0;
		uf_tele.p[i].full_duplex = 1;
	}
}
uint8_t uh_uplink(void) { return 1; }
uint8_t uh_apply(void)
{
	static int n;
	char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/syscfg-%ld-%02d.txt", getenv("CAPDIR") ? getenv("CAPDIR") : ".", (long)t0, ++n);
	if ((f = fopen(path, "w"))) {
		fputs((char *)uf_rx + UF_TNBU_HDR + uf_jv[UF_JK_SYSTEM_CFG].off, f);
		fclose(f);
	}
	printf("[%4u] apply: %u VLANs, enabled ports %02x\n", uh_uptime(), uf_plan.nv, uf_plan.enable);
	for (int i = 0; i < uf_plan.nv; i++)
		printf("        vid %u member %02x tagged %02x\n", uf_plan.vid[i], uf_plan.member[i], uf_plan.tagged[i]);
	for (int i = 0; i < UF_NPORTS; i++)
		printf("        port %d pvid %u\n", i + 1, uf_plan.pvid[i]);
	return 0;
}
uint8_t uh_tcp_open(const uint8_t *ip, uint16_t port)
{
	memcpy(tcp_ip, ip, 4);
	tcp_port = port;
	pending_tcp = 1;
	return 1;
}
void uh_udp_discovery(void) { pending_disc = 1; }

static void do_tcp(void)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(tcp_port) };
	struct timeval tv = { 10, 0 };
	uint8_t buf[2048];
	int s = socket(AF_INET, SOCK_STREAM, 0);
	ssize_t n;

	memcpy(&sa.sin_addr, tcp_ip, 4);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(s);
		uf_agent_done(UF_XR_CONNECT);
		return;
	}
	/* drive the send window as uIP would, MSS 1460 */
	while (!uf_xfer_done()) {
		uint16_t k = uf_xfer_next(1460);
		if (send(s, uf_xfer.buf + uf_xfer.acked, k, 0) != k)
			break;
		uf_xfer_acked();
	}
	while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
		uint8_t st = uf_resp_feed(buf, (uint16_t)n);
		if (st >= UF_RS_DONE)
			break;
	}
	if (n == 0)
		uf_resp_closed();
	close(s);
	printf("[%4u] HTTP %u, body %u bytes\n", uh_uptime(), uf_resp.status, uf_resp.body);
	if (uf_resp.status != 200 && uf_resp.body)
		printf("%.*s\n", uf_resp.body, (char *)uf_rx);
	uf_agent_done(uf_resp.state == UF_RS_DONE ? UF_XR_OK : UF_XR_ABORT);
	if (getenv("SHOWRESP") && uf_resp.status == 200 && uf_resp.body > 56)
		printf("        _type=%s cfgversion=%s interval=%u mgmt=%d sys=%d\n",
		       uf_jv[UF_JK_TYPE].type == UF_JT_STRING ? (char *)uf_rx + 40 + uf_jv[UF_JK_TYPE].off : "-",
		       uf_jv[UF_JK_CFGVERSION].type == UF_JT_STRING ? (char *)uf_rx + 40 + uf_jv[UF_JK_CFGVERSION].off : "-",
		       uf_json_uint(uf_rx + 40, UF_JK_INTERVAL, 0), uf_jv[UF_JK_MGMT_CFG].type, uf_jv[UF_JK_SYSTEM_CFG].type);
	if (getenv("SHOWRESP") && uf_resp.status == 200 && uf_jv[UF_JK_MGMT_CFG].type == UF_JT_STRING) {
		char *m = (char *)uf_rx + 40 + uf_jv[UF_JK_MGMT_CFG].off, *k = strstr(m, "authkey=");
		if (k)
			memset(k + 8, 'x', 32);
		printf("        mgmt_cfg: %s\n", m);
	}
}

static void do_disc(void)
{
	uint8_t pkt[256];
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(10001) };
	int s = socket(AF_INET, SOCK_DGRAM, 0), on = 1;
	uint16_t n;

	setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	uf_self.disc_seq++;
	n = uf_disc_build(pkt);
	sa.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	sendto(s, pkt, n, 0, (struct sockaddr *)&sa, sizeof(sa));
	close(s);
}

int main(int argc, char **argv)
{
	uint8_t ctrl[4];
	int secs;
	struct sockaddr_in me;
	socklen_t ml = sizeof(me);

	if (argc < 3 || inet_pton(AF_INET, argv[1], ctrl) != 1)
		return fprintf(stderr, "usage: hostrun <controller-ip> <seconds> [state-file]\n"), 2;
	secs = atoi(argv[2]);
	state_file = argc > 3 ? argv[3] : NULL;
	memset(flash, 0xff, sizeof(flash));
	if (state_file) {
		FILE *f = fopen(state_file, "rb");
		if (f) {
			fread(flash + UF_STORE_SLOT_A, 1, 0x2000, f);
			fclose(f);
		}
	}
	{	/* our own address on the route to the controller */
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		struct sockaddr_in c = { .sin_family = AF_INET, .sin_port = htons(8080) };
		memcpy(&c.sin_addr, ctrl, 4);
		connect(s, (struct sockaddr *)&c, sizeof(c));
		getsockname(s, (struct sockaddr *)&me, &ml);
		memcpy(my_ip, &me.sin_addr, 4);
		close(s);
	}
	t0 = time(NULL);
	if (getenv("MAC_LAST"))
		MAC[5] = (uint8_t)strtol(getenv("MAC_LAST"), NULL, 16);
	setvbuf(stdout, NULL, _IOLBF, 0);
	uf_agent_init(MAC, ctrl, 8080);
	uf_agent.discovery = getenv("DISCOVERY") != NULL;
	if (getenv("FAKE_CFGVER"))
		strncpy(uf_self.cfgversion, getenv("FAKE_CFGVER"), UF_CFGVER_LEN - 1);
	strcpy(uf_self.hostname, "uf-hosttest");
	while (uh_uptime() <= (uint32_t)secs) {
		uf_agent_poll();
		if (pending_disc) {
			pending_disc = 0;
			do_disc();
		}
		if (pending_tcp) {
			pending_tcp = 0;
			do_tcp();
		}
		usleep(100000);
	}
	printf("informs %u ok %u fail %u authfail %u adopted %u cfgversion %s\n", uf_agent.n_inform, uf_agent.n_ok,
	       uf_agent.n_fail, uf_agent.n_auth_fail, uf_self.adopted, uf_self.cfgversion);
	return 0;
}

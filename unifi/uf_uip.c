/*
 * uIP application dispatcher for UniFi builds. Lives in BANK1 next to uIP and
 * httpd: connections on port 80 belong to httpd, everything else is the
 * inform client. httpd keeps its request state in globals, so it is limited
 * to one live connection; the second slot stays free for the inform client.
 */
#include "uip/uip.h"
#include "httpd/httpd.h"
#include "uf_glue.h"

#pragma codeseg BANK1

void uf_uip_appcall(void)
{
	__xdata uint8_t i;
	__xdata struct uip_conn *c;

	if (uip_conn->lport != HTONS(80)) {
		uf_tcp_appcall();
		return;
	}
	if (uip_connected()) {
		for (i = 0; i < UIP_CONNS; i++) {
			c = &uip_conns[i];
			if (c != uip_conn && c->lport == HTONS(80)
			    && (c->tcpstateflags & UIP_TS_MASK) != UIP_CLOSED
			    && (c->tcpstateflags & UIP_TS_MASK) != UIP_TIME_WAIT) {
				uip_abort();
				return;
			}
		}
	}
	httpd_appcall();
}

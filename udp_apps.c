
#include "uip/uip.h"
#include "udp_apps.h"
#ifdef UNIFI
#include "unifi/uf_glue.h"
#endif

void udp_callbacks(void)
{
	dhcp_callback(uip_udp_conn->lport); 	// let the application decide if this is for it or not
	syslog_callback(uip_udp_conn->lport);	// let the application decide if this is for it or not
	dns_callback(uip_udp_conn->lport);
	ntp_callback(uip_udp_conn->lport);
#ifdef UNIFI
	uf_udp_appcall();
#endif
}
/*
 * Entry points of the UniFi agent for the rest of the firmware (built with -DUNIFI)
 */
#ifndef _UF_GLUE_H_
#define _UF_GLUE_H_

#include <stdint.h>

extern __xdata uint8_t uf_rx_port;

void uf_tcp_appcall(void) __banked;
void uf_udp_appcall(void) __banked;
void uf_glue_poll(void) __banked;
void uf_cli(void) __banked;
/* Starts the agent after the startup configuration ran, unless it said "unifi off" */
void uf_glue_autostart(void) __banked;
/* Forgets the UniFi adoption in flash (reset button); the nonce epoch is kept */
void uf_factory_reset(void) __banked;

#endif

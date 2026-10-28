/*
 * UniFi personality, set at build time (make UNIFI_MODEL=... UNIFI_VERSION=...).
 * This is what the controller is told; the hardware definition is separate.
 */
#ifndef _UF_IDENT_H_
#define _UF_IDENT_H_

#ifndef UNIFI_MODEL
#define UNIFI_MODEL		"USWED35"
#endif
#ifndef UNIFI_VERSION
#define UNIFI_VERSION		"2.1.8.971"
#endif
#ifndef UNIFI_MODEL_DISPLAY
#define UNIFI_MODEL_DISPLAY	"Switch Flex Mini 2.5G 5"
#endif
/*
 * Reported as required_version. The controller offers a firmware upgrade only
 * when the new firmware is at least this version ("Upgrade unavailable ...
 * bundled(x) < required(y)"), so a version Ubiquiti will not reach keeps it
 * from pushing USWED35 firmware to this switch at all.
 */
#ifndef UNIFI_REQUIRED_VERSION
#define UNIFI_REQUIRED_VERSION	"99.0.0"
#endif
#ifndef UNIFI_PORT_COUNT
#define UNIFI_PORT_COUNT	5
#endif

#if UNIFI_PORT_COUNT != UF_NPORTS
#error "UNIFI_PORT_COUNT must match the number of mapped copper ports (UF_NPORTS)"
#endif

#endif

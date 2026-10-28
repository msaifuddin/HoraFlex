/*
 * Top-level scanner for controller responses. Finds a fixed set of keys in the
 * outer JSON object without building a tree: string values are unescaped and
 * NUL-terminated in place, nested objects and arrays are skipped iteratively.
 */
#ifndef _UF_JSON_H_
#define _UF_JSON_H_

#include "uf_port.h"

#define UF_JK_TYPE		0
#define UF_JK_CFGVERSION	1
#define UF_JK_MGMT_CFG		2
#define UF_JK_SYSTEM_CFG	3
#define UF_JK_INTERVAL		4
#define UF_JK_SERVER_TIME	5
#define UF_JK_CMD		6
#define UF_JK_N			7

#define UF_JT_ABSENT	0
#define UF_JT_STRING	1
#define UF_JT_LITERAL	2	/* number, true, false, null */
#define UF_JT_NESTED	3	/* object or array, left as raw text */

struct uf_jval {
	uint16_t off;
	uint16_t len;
	uint8_t type;
};

extern UF_X struct uf_jval uf_jv[UF_JK_N];

/* Returns 1 when p[0..n) is one well-formed object, 0 otherwise. Values of
 * the keys above are in uf_jv; unknown keys are skipped. Modifies p. */
uint8_t uf_json_scan(UF_X uint8_t * UF_X p, UF_X uint16_t n) UF_BANKED;

/* Unsigned decimal of key k, bare or quoted (the controller sends server_time_in_utc as a string), ignoring the last drop digits
 * (3 turns milliseconds into seconds). Returns 0xffffffff when absent or bad. */
uint32_t uf_json_uint(UF_X const uint8_t * UF_X p, UF_X uint8_t k, UF_X uint8_t drop) UF_BANKED;

#endif

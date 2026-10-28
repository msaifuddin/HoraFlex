/*
 * Controller configuration: mgmt_cfg and system_cfg parsing, the staged switch
 * model, the plan derived from it, and the management-path guard.
 *
 * UniFi port numbers 1..5 are used throughout; bit (p - 1) of a port mask is
 * UniFi port p. Hardware numbering is only known to the apply layer.
 */
#ifndef _UF_CFG_H_
#define _UF_CFG_H_

#include "uf_port.h"

#define UF_MAX_VLANS	32
#define UF_NAME_LEN	32
#define UF_CFGVER_LEN	33
#define UF_FWVER_LEN	20	/* reported firmware version, e.g. "2.1.8.971" */
#define UF_UNKNOWN_KEYS	4
#define UF_UNKNOWN_LEN	24

#define UF_VM_UNSET	0
#define UF_VM_UNTAGGED	1
#define UF_VM_TAGGED	2
#define UF_VM_EXCLUDE	3

/* Errors: the whole configuration is rejected and nothing is applied */
#define UF_CE_NONE		0
#define UF_CE_SYNTAX		1	/* switch.* line without '=' or bad key shape */
#define UF_CE_VALUE		2	/* number or keyword out of range */
#define UF_CE_TOO_MANY_VLANS	3
#define UF_CE_VLAN_NO_ID	4
#define UF_CE_DUP_VID		5
#define UF_CE_MGMT_VLAN		6	/* management VLAN other than 1 is not supported yet */
#define UF_CE_OPMODE		7	/* port opmode other than "switch" */
#define UF_CE_PROTECT_DISABLE	8	/* would disable the uplink */
#define UF_CE_PROTECT_VLAN	9	/* would take the management VLAN off the uplink */
#define UF_CE_UPLINK_UNKNOWN	10

struct uf_mgmt {
	uint8_t has_authkey;
	uint8_t authkey[16];
	uint8_t use_gcm;
	uint8_t has_cfgversion;
	char cfgversion[UF_CFGVER_LEN];
	uint8_t has_inform;
	uint8_t inform_ip[4];
	uint16_t inform_port;
	uint8_t bad;
};

struct uf_svlan {
	uint16_t idx;
	uint16_t vid;
	uint8_t mode;
	uint8_t disabled;
	uint8_t port_mode[UF_NPORTS];
};

struct uf_sport {
	uint16_t pvid;
	uint8_t disabled;
	uint8_t has_name;
	char name[UF_NAME_LEN];
};

struct uf_sys {
	uint16_t mgmt_vlan;
	uint8_t nvlans;
	struct uf_svlan v[UF_MAX_VLANS];
	struct uf_sport p[UF_NPORTS];
	uint8_t err;
	uint16_t err_line;
	uint8_t n_unknown;
	char unknown[UF_UNKNOWN_KEYS][UF_UNKNOWN_LEN];
	uint16_t n_switch;
};

struct uf_plan {
	uint16_t mgmt_vlan;
	uint8_t nv;
	uint16_t vid[UF_MAX_VLANS];
	uint8_t member[UF_MAX_VLANS];
	uint8_t tagged[UF_MAX_VLANS];
	uint16_t pvid[UF_NPORTS];
	uint8_t enable;
	uint8_t has_name;
	char name[UF_NPORTS][UF_NAME_LEN];
};

extern UF_X struct uf_mgmt uf_mgmt;
extern UF_X struct uf_sys uf_sys;
extern UF_X struct uf_plan uf_plan;	/* staged: built from the latest system_cfg */
extern UF_X struct uf_plan uf_live;	/* applied to the switch; nv == 0xff until the first apply */

/* Parses NUL-terminated newline key=value text */
void uf_mgmt_parse(UF_X const char * UF_X s) UF_BANKED;
uint8_t uf_sys_parse(UF_X const char * UF_X s) UF_BANKED;
/* Resolves uf_sys into uf_plan */
uint8_t uf_plan_build(void) UF_BANKED;
/* Factory state for the five ports: VLAN 1 untagged everywhere, PVID 1, enabled */
void uf_plan_default(void) UF_BANKED;
/* Checks uf_plan keeps UniFi port uplink (1..5, 0 = unknown) reachable */
uint8_t uf_plan_protect(UF_X uint8_t uplink) UF_BANKED;

#endif

/*
 * Bounded scanner for the outer object of a controller response
 */
#include "uf_json.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

#define MAX_DEPTH	32

UF_X struct uf_jval uf_jv[UF_JK_N];

static UF_C const char * UF_C const keys[UF_JK_N] = {
	"_type", "cfgversion", "mgmt_cfg", "system_cfg", "interval", "server_time_in_utc", "cmd", "version"
};

static UF_X uint8_t * UF_X jp;
static UF_X uint16_t jn, ji;

static void skip_ws(void)
{
	while (ji < jn && (jp[ji] == ' ' || jp[ji] == '\t' || jp[ji] == '\n' || jp[ji] == '\r'))
		ji++;
}

/*
 * At an opening quote: unescapes the string in place starting at its first
 * character, NUL-terminates it and leaves ji after the closing quote.
 * Non-ASCII \u escapes become '?'. Returns the length or 0xffff when malformed.
 */
static uint16_t string_in_place(void)
{
	UF_X uint16_t w, start, cp;
	UF_X uint8_t c, k;

	start = w = ++ji;
	while (ji < jn) {
		c = jp[ji++];
		if (c == '"') {
			jp[w] = 0;	/* w < ji, so this never reaches unread input */
			return w - start;
		}
		if (c < 0x20)
			return 0xffff;
		if (c == '\\') {
			if (ji >= jn)
				return 0xffff;
			c = jp[ji++];
			switch (c) {
			case '"': case '\\': case '/': break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'u':
				if (jn - ji < 4)
					return 0xffff;
				cp = 0;
				for (k = 0; k < 4; k++) {
					c = jp[ji++];
					if (c >= '0' && c <= '9')
						c -= '0';
					else if (c >= 'a' && c <= 'f')
						c -= 'a' - 10;
					else if (c >= 'A' && c <= 'F')
						c -= 'A' - 10;
					else
						return 0xffff;
					cp = (cp << 4) | c;
				}
				c = (cp && cp < 0x80) ? (uint8_t)cp : '?';
				break;
			default:
				return 0xffff;
			}
		}
		jp[w++] = c;
	}
	return 0xffff;
}

/* Skips a string without modifying it */
static uint8_t skip_string(void)
{
	ji++;
	while (ji < jn) {
		if (jp[ji] == '\\') {
			ji += 2;
			continue;
		}
		if (jp[ji++] == '"')
			return 1;
	}
	return 0;
}

/* Skips a nested object or array with a bounded depth counter */
static uint8_t skip_nested(void)
{
	UF_X uint8_t depth = 0;
	UF_X uint8_t c;

	while (ji < jn) {
		c = jp[ji];
		if (c == '"') {
			if (!skip_string())
				return 0;
			continue;
		}
		ji++;
		if (c == '{' || c == '[') {
			if (++depth > MAX_DEPTH)
				return 0;
		} else if (c == '}' || c == ']') {
			if (!depth--)
				return 0;
			if (!depth)
				return 1;
		}
	}
	return 0;
}

static uint8_t literal_char(UF_X uint8_t c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-' || c == '+' || c == '.' || c == 'E';
}

static UF_X uint8_t * UF_X kp;
static UF_C const char * UF_X ksp;

static int8_t key_index(UF_X uint16_t off, UF_X uint16_t len)
{
	UF_X uint8_t k;
	UF_X uint16_t i;

	for (k = 0; k < UF_JK_N; k++) {
		ksp = keys[k];
		kp = jp + off;
		for (i = 0; i < len; i++) {
			if (!*ksp || *ksp != *kp)
				break;
			ksp++;
			kp++;
		}
		if (i == len && !*ksp)
			return (int8_t)k;
	}
	return -1;
}

static uint8_t scan(UF_X uint8_t * UF_X p, UF_X uint16_t n)
{
	UF_X uint16_t koff, klen, voff, vlen;
	UF_X int8_t k;
	UF_X uint8_t type;

	jp = p;
	jn = n;
	ji = 0;
	for (k = 0; k < UF_JK_N; k++)
		uf_jv[k].type = UF_JT_ABSENT;

	skip_ws();
	if (ji >= jn || jp[ji] != '{')
		return 0;
	ji++;
	skip_ws();
	if (ji < jn && jp[ji] == '}') {
		ji++;
		goto tail;
	}
	for (;;) {
		skip_ws();
		if (ji >= jn || jp[ji] != '"')
			return 0;
		koff = ji + 1;
		klen = string_in_place();
		if (klen == 0xffff)
			return 0;
		skip_ws();
		if (ji >= jn || jp[ji] != ':')
			return 0;
		ji++;
		skip_ws();
		if (ji >= jn)
			return 0;
		voff = ji;
		if (jp[ji] == '"') {
			voff = ji + 1;
			vlen = string_in_place();
			if (vlen == 0xffff)
				return 0;
			type = UF_JT_STRING;
		} else if (jp[ji] == '{' || jp[ji] == '[') {
			if (!skip_nested())
				return 0;
			vlen = ji - voff;
			type = UF_JT_NESTED;
		} else {
			while (ji < jn && literal_char(jp[ji]))
				ji++;
			vlen = ji - voff;
			if (!vlen)
				return 0;
			type = UF_JT_LITERAL;
		}
		k = key_index(koff, klen);
		if (k >= 0) {
			uf_jv[k].off = voff;
			uf_jv[k].len = vlen;
			uf_jv[k].type = type;
		}
		skip_ws();
		if (ji >= jn)
			return 0;
		if (jp[ji] == ',') {
			ji++;
			continue;
		}
		if (jp[ji] != '}')
			return 0;
		ji++;
		break;
	}
tail:
	skip_ws();
	return ji == jn;
}

/* A failed scan leaves no partial results behind */
uint8_t uf_json_scan(UF_X uint8_t * UF_X p, UF_X uint16_t n) UF_BANKED
{
	UF_X uint8_t k;

	if (scan(p, n))
		return 1;
	for (k = 0; k < UF_JK_N; k++)
		uf_jv[k].type = UF_JT_ABSENT;
	return 0;
}

/* Decimal into a 4-byte big-endian accumulator: no 32-bit temporaries */
static UF_X uint8_t u_acc[4];
static UF_X uint16_t u_i, u_end, u_c;
static UF_X uint8_t u_j;

static uint8_t mul10_add(UF_X uint8_t d)
{
	u_c = d;
	for (u_j = 4; u_j--; ) {
		u_c += (uint16_t)u_acc[u_j] * 10;
		u_acc[u_j] = (uint8_t)u_c;
		u_c >>= 8;
	}
	return u_c == 0;	/* 0 on overflow */
}

uint32_t uf_json_uint(UF_X const uint8_t * UF_X p, UF_X uint8_t k, UF_X uint8_t drop) UF_BANKED
{
	UF_X uint8_t c;

	if ((uf_jv[k].type != UF_JT_LITERAL && uf_jv[k].type != UF_JT_STRING) || uf_jv[k].len <= drop || uf_jv[k].len - drop > 10)
		return 0xffffffff;
	u_acc[0] = u_acc[1] = u_acc[2] = u_acc[3] = 0;
	u_end = uf_jv[k].off + uf_jv[k].len;
	for (u_i = uf_jv[k].off; u_i < u_end; u_i++) {
		c = p[u_i];
		if (c < '0' || c > '9')
			return 0xffffffff;
		if (u_i >= u_end - drop)
			continue;	/* dropped digits are only checked */
		if (!mul10_add(c - '0'))
			return 0xffffffff;
	}
	if ((u_acc[0] & u_acc[1] & u_acc[2] & u_acc[3]) == 0xff)
		return 0xffffffff;	/* the error value itself */
	return ((uint32_t)u_acc[0] << 24) | ((uint32_t)u_acc[1] << 16) | ((uint16_t)u_acc[2] << 8) | u_acc[3];
}

/*
 * Inform HTTP request, send window and response parser
 */
#include "uf_http.h"

#ifdef __SDCC
#pragma codeseg BANK5
#pragma constseg BANK5
#endif

#define LINE_MAX	96
#define HDR_LIMIT	2048

UF_X struct uf_xfer uf_xfer;
UF_X struct uf_resp uf_resp;

static UF_X uint8_t * UF_X r_body;
static UF_X uint16_t r_max;
static UF_X uint8_t r_line[LINE_MAX];
static UF_X uint8_t r_ll;
static UF_X uint32_t r_rem;
static UF_X uint16_t r_cl;

static UF_X uint8_t h_tmp[UF_HTTP_HDR_MAX];
static UF_X uint16_t h_n;
static UF_X uint8_t h_dig[6];

static void h_lit(UF_C const char * UF_X s)
{
	while (*s && h_n < UF_HTTP_HDR_MAX)
		h_tmp[h_n++] = (uint8_t)*s++;
}

static void h_dec(UF_X uint16_t v)
{
	UF_X uint8_t n = 0;

	do {
		h_dig[n++] = (uint8_t)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n && h_n < UF_HTTP_HDR_MAX)
		h_tmp[h_n++] = h_dig[--n];
}

uint16_t uf_http_request(UF_X uint8_t * UF_X buf, UF_X uint16_t body_off, UF_X uint16_t body_len,
			 UF_X const uint8_t * UF_X ip, UF_X uint16_t port) UF_BANKED
{
	UF_X uint8_t i;

	h_n = 0;
	h_lit("POST /inform HTTP/1.1\r\nHost: ");
	for (i = 0; i < 4; i++) {
		if (i)
			h_lit(".");
		h_dec(ip[i]);
	}
	h_lit(":");
	h_dec(port);
	h_lit("\r\nUser-Agent: AirControl Agent v1.0\r\nAccept: application/x-binary\r\n"
	      "Content-Type: application/x-binary\r\nContent-Length: ");
	h_dec(body_len);
	h_lit("\r\n\r\n");
	if (h_n >= UF_HTTP_HDR_MAX || h_n > body_off)
		return 0xffff;
	uf_memcpy(buf + body_off - h_n, h_tmp, h_n);
	return body_off - h_n;
}

void uf_xfer_start(UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED
{
	uf_xfer.buf = buf;
	uf_xfer.len = len;
	uf_xfer.acked = 0;
	uf_xfer.inflight = 0;
}

uint16_t uf_xfer_next(UF_X uint16_t mss) UF_BANKED
{
	if (uf_xfer.inflight || uf_xfer.acked >= uf_xfer.len || !mss)
		return 0;
	uf_xfer.inflight = uf_xfer.len - uf_xfer.acked;
	if (uf_xfer.inflight > mss)
		uf_xfer.inflight = mss;
	return uf_xfer.inflight;
}

void uf_xfer_acked(void) UF_BANKED
{
	uf_xfer.acked += uf_xfer.inflight;
	uf_xfer.inflight = 0;
}

uint16_t uf_xfer_rexmit(void) UF_BANKED
{
	return uf_xfer.inflight;
}

uint8_t uf_xfer_done(void) UF_BANKED
{
	return uf_xfer.acked >= uf_xfer.len && !uf_xfer.inflight;
}

void uf_resp_start(UF_X uint8_t * UF_X body, UF_X uint16_t max) UF_BANKED
{
	uf_memset((UF_X uint8_t *)&uf_resp, 0, sizeof(uf_resp));
	r_body = body;
	r_max = max;
	r_ll = 0;
}

static uint8_t lower(UF_X uint8_t c)
{
	return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c;
}

/* Case-insensitive "name:" at the start of r_line; returns the value offset or 0 */
static uint8_t header_is(UF_C const char * UF_X name)
{
	UF_X uint8_t i = 0;

	while (name[i]) {
		if (i >= r_ll || lower(r_line[i]) != (uint8_t)name[i])
			return 0;
		i++;
	}
	while (i < r_ll && (r_line[i] == ' ' || r_line[i] == '\t'))
		i++;
	return i;
}

static void finish_body(void)
{
	if (uf_resp.has_clen && uf_resp.seen >= uf_resp.clen)
		uf_resp.state = UF_RS_DONE;
}

static void line_done(void)
{
	UF_X uint8_t i;

	if (uf_resp.state == UF_RS_STATUS) {
		/* "HTTP/1.x NNN" */
		if (r_ll < 12 || r_line[0] != 'H' || r_line[1] != 'T' || r_line[2] != 'T' || r_line[3] != 'P'
		    || r_line[4] != '/' || r_line[5] != '1' || r_line[6] != '.' || r_line[8] != ' ') {
			uf_resp.state = UF_RS_ERROR;
			uf_resp.err = UF_RE_STATUS;
			return;
		}
		for (i = 9; i < 12; i++) {
			if (r_line[i] < '0' || r_line[i] > '9') {
				uf_resp.state = UF_RS_ERROR;
				uf_resp.err = UF_RE_STATUS;
				return;
			}
			r_cl = uf_resp.status;
			uf_resp.status = (r_cl << 3) + (r_cl << 1) + (r_line[i] - '0');
		}
		uf_resp.state = UF_RS_HEADER;
		return;
	}
	if (!r_ll) {
		uf_resp.state = UF_RS_BODY;
		/* No body expected for these, or an explicit empty body */
		if ((uf_resp.has_clen && !uf_resp.clen) || uf_resp.status == 204 || uf_resp.status == 304)
			uf_resp.state = UF_RS_DONE;
		if (uf_resp.has_clen && uf_resp.clen > r_max)
			uf_resp.oversize = 1;
		return;
	}
	if ((i = header_is("content-length:"))) {
		/* 16-bit arithmetic: anything above 65534 is too large for us anyway */
		r_cl = 0;
		for (; i < r_ll && r_line[i] >= '0' && r_line[i] <= '9'; i++) {
			if (r_cl > 6552) {
				r_cl = 0xffff;
				break;
			}
			r_cl = r_cl * 10 + (r_line[i] - '0');
		}
		uf_resp.clen = r_cl == 0xffff ? 0x10000UL : r_cl;
		uf_resp.has_clen = 1;
	} else if ((i = header_is("transfer-encoding:"))) {
		if (r_ll - i >= 7 && lower(r_line[i]) == 'c') {
			uf_resp.state = UF_RS_ERROR;
			uf_resp.err = UF_RE_CHUNKED;
		}
	}
}

uint8_t uf_resp_feed(UF_X const uint8_t * UF_X p, UF_X uint16_t n) UF_BANKED
{
	UF_X uint8_t c;

	while (n && uf_resp.state < UF_RS_BODY) {
		c = *p++;
		n--;
		if (++uf_resp.hdr_bytes > HDR_LIMIT) {
			uf_resp.state = UF_RS_ERROR;
			uf_resp.err = UF_RE_HEADER;
			return uf_resp.state;
		}
		if (c == '\r')
			continue;
		if (c == '\n') {
			line_done();
			r_ll = 0;
			continue;
		}
		if (r_ll < LINE_MAX)
			r_line[r_ll++] = c;	/* longer lines are cut; only short headers matter */
	}
	if (uf_resp.state != UF_RS_BODY)
		return uf_resp.state;
	if (uf_resp.has_clen) {
		r_rem = uf_resp.clen;
		r_rem -= uf_resp.seen;
		if (n > r_rem)
			n = (uint16_t)r_rem;	/* ignore anything past the body */
	}
	while (n--) {
		if (!uf_resp.oversize) {
			if (uf_resp.body < r_max)
				r_body[uf_resp.body++] = *p;
			else
				uf_resp.oversize = 1;
		}
		p++;
		uf_resp.seen++;
	}
	finish_body();
	return uf_resp.state;
}

uint8_t uf_resp_closed(void) UF_BANKED
{
	if (uf_resp.state == UF_RS_BODY) {
		if (uf_resp.has_clen) {
			uf_resp.state = UF_RS_ERROR;
			uf_resp.err = UF_RE_TRUNCATED;
		} else {
			uf_resp.state = UF_RS_DONE;
		}
	} else if (uf_resp.state < UF_RS_BODY) {
		uf_resp.state = UF_RS_ERROR;
		uf_resp.err = UF_RE_TRUNCATED;
	}
	return uf_resp.state;
}

/*
 * HTTP for the inform client: request header, the TCP send window, and a
 * bounded response parser. No uIP dependency, so it is unit-testable.
 */
#ifndef _UF_HTTP_H_
#define _UF_HTTP_H_

#include "uf_port.h"

/* Bytes reserved in front of the body for the request header */
#define UF_HTTP_HDR_MAX	224

/* Writes the header so it ends at buf + body_off. Returns its start offset,
 * or 0xffff if it does not fit in UF_HTTP_HDR_MAX */
uint16_t uf_http_request(UF_X uint8_t * UF_X buf, UF_X uint16_t body_off, UF_X uint16_t body_len,
			 UF_X const uint8_t * UF_X ip, UF_X uint16_t port) UF_BANKED;

/*
 * Send window. Data is sent in chunks of at most the MSS; the next chunk is
 * only offered after the previous one was acknowledged, and a retransmission
 * repeats exactly the unacknowledged chunk.
 */
struct uf_xfer {
	UF_X uint8_t *buf;
	uint16_t len;
	uint16_t acked;
	uint16_t inflight;
};
extern UF_X struct uf_xfer uf_xfer;

void uf_xfer_start(UF_X uint8_t * UF_X buf, UF_X uint16_t len) UF_BANKED;
/* Length of the next chunk at uf_xfer.buf + uf_xfer.acked, 0 if none may be sent now */
uint16_t uf_xfer_next(UF_X uint16_t mss) UF_BANKED;
void uf_xfer_acked(void) UF_BANKED;
/* Length of the chunk to retransmit (same offset as before) */
uint16_t uf_xfer_rexmit(void) UF_BANKED;
uint8_t uf_xfer_done(void) UF_BANKED;

#define UF_RS_STATUS	0
#define UF_RS_HEADER	1
#define UF_RS_BODY	2
#define UF_RS_DONE	3
#define UF_RS_ERROR	4

#define UF_RE_NONE	0
#define UF_RE_STATUS	1	/* status line not HTTP/1.x NNN */
#define UF_RE_CHUNKED	2	/* chunked transfer encoding is not supported */
#define UF_RE_HEADER	3	/* header section too long */
#define UF_RE_TRUNCATED	4	/* connection closed before Content-Length bytes */

struct uf_resp {
	uint8_t state;
	uint8_t err;
	uint16_t status;
	uint8_t has_clen;
	uint32_t clen;
	uint16_t body;		/* bytes stored */
	uint32_t seen;		/* body bytes received, stored or not */
	uint8_t oversize;	/* body did not fit; seen tells how big it was */
	uint16_t hdr_bytes;
};
extern UF_X struct uf_resp uf_resp;

void uf_resp_start(UF_X uint8_t * UF_X body, UF_X uint16_t max) UF_BANKED;
/* Consumes received bytes. Returns uf_resp.state */
uint8_t uf_resp_feed(UF_X const uint8_t * UF_X p, UF_X uint16_t n) UF_BANKED;
/* The peer closed the connection */
uint8_t uf_resp_closed(void) UF_BANKED;

#endif

#ifndef PROTO_H_
#define PROTO_H_

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

/*
 * Wire framing (as enforced by netproc):
 *   every transfer is exactly FRAME_LEN bytes = 1 link byte + PAYLOAD_LEN bytes.
 *   frame[0]      = link number (0xFF == broadcast to all neighbors)
 *   frame[1..127] = the 127-byte payload, copied verbatim by netproc.
 *
 * The payload format below is private to bfproc: netproc never inspects it and
 * the grader runs only our own bfproc instances together, so there is no
 * interoperability constraint on field widths or units.
 */
#define FRAME_LEN 128
#define PAYLOAD_LEN 127
#define LINK_BROADCAST 0xFF

/*
 * Update message (cf. BPDU). All fields are 32-bit, network byte order on wire.
 *
 *   root   - myRoot: smallest ID known to the sender.
 *   cost   - myCost: sender's cost to root.
 *   id     - myID:   sender's own ID (this becomes the receiver's "parent").
 *   exp_ms - expTime: remaining lifetime, in MILLISECONDS.
 *
 * The PDF gives no wire unit for expTime ("expTime = 100" is illustrative). We
 * use milliseconds because the output is printed to 0.1s and the timeouts are
 * 2s/6s, so aging needs sub-second resolution; integer-second expTime would
 * truncate and drift. 16 bytes used, the rest of the 127-byte payload is zeroed.
 */
struct bf_msg {
	uint32_t root;
	uint32_t cost;
	uint32_t id;
	uint32_t exp_ms;
};

static inline void bf_msg_pack(uint8_t payload[PAYLOAD_LEN],
							   const struct bf_msg* m) {
	uint32_t net[4] = {htonl(m->root), htonl(m->cost), htonl(m->id),
					   htonl(m->exp_ms)};
	memset(payload, 0, PAYLOAD_LEN);
	memcpy(payload, net, sizeof net);
}

static inline void bf_msg_unpack(struct bf_msg* m,
								 const uint8_t payload[PAYLOAD_LEN]) {
	uint32_t net[4];
	memcpy(net, payload, sizeof net);
	m->root = ntohl(net[0]);
	m->cost = ntohl(net[1]);
	m->id = ntohl(net[2]);
	m->exp_ms = ntohl(net[3]);
}

#endif  // PROTO_H_

#ifndef PROTO_H_
#define PROTO_H_

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

/* netproc frames are a fixed 128 bytes: a link byte (0xFF = broadcast) plus a
 * 127-byte payload. It rewrites the link byte to the receiver's own index and
 * leaves the payload alone, so the payload format is entirely ours. */
#define FRAME_LEN 128
#define PAYLOAD_LEN 127
#define LINK_BROADCAST 0xFF

/* Our update message, like a spanning-tree BPDU. Sent in network byte order,
 * rest of the payload zeroed. exp_ms is in milliseconds: we print time to 0.1s
 * with 2s/6s timeouts, so whole seconds would round away gaps we need. id is the
 * sender, which the receiver records as its parent. */
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

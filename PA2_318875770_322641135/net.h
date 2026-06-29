#ifndef NET_H_
#define NET_H_

#include <stdint.h>

#include "proto.h"

/*
 * Socket helpers. Per the course FAQ, the node must set TCP_NODELAY, recv with
 * MSG_WAITALL, and send with MSG_NOSIGNAL (so a dead netproc yields EPIPE
 * instead of a SIGPIPE that would kill us).
 */

/* Connect to netproc at addr:port, set TCP_NODELAY. Returns fd, or -1 on error. */
int net_connect(const char* addr, uint16_t port);

/* Handshake: send our ID as a 4-byte int via htonl(). Returns 0 / -1. */
int net_send_id(int sock, uint32_t id);

/* Send one 128-byte frame (link byte + payload). Returns 0, or -1 if peer gone. */
int net_send_frame(int sock, uint8_t link, const uint8_t payload[PAYLOAD_LEN]);

/*
 * Receive one 128-byte frame. *link gets frame[0], payload gets frame[1..127].
 * Returns 1 on success, 0 if netproc closed the connection (EOF), -1 on error.
 */
int net_recv_frame(int sock, uint8_t* link, uint8_t payload[PAYLOAD_LEN]);

#endif  // NET_H_

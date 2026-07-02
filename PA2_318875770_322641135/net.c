


#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Send the whole buffer. MSG_NOSIGNAL so a dead netproc gives us an error to
 * handle instead of killing us with SIGPIPE. */
static int writen(int fd, const void* buf, size_t n) {
	const char* p = buf;
	size_t left = n;
	while (left) {
		ssize_t w = send(fd, p, left, MSG_NOSIGNAL);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (w == 0) return -1;
		left -= (size_t) w;
		p += w;
	}
	return 0;
}

/* Backstop for a short recv; returns whatever it managed to read. */
static ssize_t readn(int fd, void* buf, size_t n) {
	char* p = buf;
	size_t left = n;
	while (left) {
		ssize_t r = recv(fd, p, left, 0);
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (r == 0) return (ssize_t) (n - left);
		left -= (size_t) r;
		p += r;
	}
	return (ssize_t) n;
}

int net_connect(const char* addr, uint16_t port) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	int one = 1;
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
		perror("setsockopt(TCP_NODELAY)");
		close(sock);
		return -1;
	}

	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
		fprintf(stderr, "Invalid netproc address: %s\n", addr);
		close(sock);
		return -1;
	}

	if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
		perror("connect");
		close(sock);
		return -1;
	}
	return sock;
}

int net_send_id(int sock, uint32_t id) {
	uint32_t net = htonl(id);
	return writen(sock, &net, sizeof net);
}

int net_send_frame(int sock, uint8_t link, const uint8_t payload[PAYLOAD_LEN]) {
	uint8_t frame[FRAME_LEN];
	frame[0] = link;
	memcpy(frame + 1, payload, PAYLOAD_LEN);
	return writen(sock, frame, FRAME_LEN);
}

int net_recv_frame(int sock, uint8_t* link, uint8_t payload[PAYLOAD_LEN]) {
	uint8_t frame[FRAME_LEN];
	ssize_t r;
	do {
		r = recv(sock, frame, FRAME_LEN, MSG_WAITALL);
	} while (r < 0 && errno == EINTR);

	if (r == 0) return 0;  /* netproc hung up */
	if (r < 0) return -1;
	/* MSG_WAITALL normally gives the whole frame; read any remainder by hand. */
	if (r < FRAME_LEN) {
		ssize_t r2 = readn(sock, frame + r, (size_t) (FRAME_LEN - r));
		if (r2 < 0) return -1;
		if (r2 < (ssize_t) (FRAME_LEN - r)) return 0;  /* hung up mid-frame */
	}

	*link = frame[0];
	memcpy(payload, frame + 1, PAYLOAD_LEN);
	return 1;
}

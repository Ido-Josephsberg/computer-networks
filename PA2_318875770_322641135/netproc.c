// Netproc: A network simulator
// Course: Introduction to Computer Networks
// University: TAU
// Author: GMD <gm@mail.tau.ac.il>

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include "bf.h"

#define realloc_dynarray(ptr, newsize) \
	realloc_dynarray_(ptr, (newsize) * sizeof *ptr)
void* realloc_dynarray_(void* ptr, size_t newsize) {
	struct dynarray {
		size_t size;
		max_align_t array[];
	};
	struct dynarray* dyn;
	if (ptr == NULL) {
		if (newsize == 0) return NULL;
		dyn = malloc(sizeof(struct dynarray) + newsize);
		if (dyn == NULL) return NULL;
		dyn->size = newsize;
		memset((char*) dyn->array, 0, newsize);
		return dyn->array;
	}
	dyn = (struct dynarray*) ((char*) ptr - offsetof(struct dynarray, array));
	size_t oldsize = dyn->size;
	if (newsize == 0) {
		free(dyn);
		return NULL;
	} else if (newsize > oldsize) {
		size_t overalloc = newsize * 2;
		assert(overalloc > oldsize);
		dyn = realloc(dyn, sizeof(struct dynarray) + overalloc);
		if (dyn == NULL) return NULL;
		dyn->size = overalloc;
		memset((char*) dyn->array + oldsize, 0, overalloc - oldsize);
		return dyn->array;
	}
	return ptr;
}

struct network_edge {
	size_t index;
	size_t edge;
	uint32_t dst;
	uint32_t cost;
};
struct network_node {
	uint32_t src;
	struct network_edge* edgelist;
	size_t edges;
};
struct network {
	struct network_node* nodelist;
	size_t nodes;
};

ssize_t network_find(struct network* network, uint32_t src) {
	for (size_t i = 0; i != network->nodes; ++i) {
		struct network_node* node = &network->nodelist[i];
		if (node->src == src) return i;
	}
	return -1;
}

ssize_t network_add_node(struct network* network, uint32_t src) {
	ssize_t index = network_find(network, src);
	if (index == -1) {
		network->nodelist =
			realloc_dynarray(network->nodelist, network->nodes + 1);
		if (network->nodelist == NULL) return -1;
		index = network->nodes++;
		network->nodelist[index].src = src;
	}
	return index;
}

ssize_t network_add_edge(struct network* network, size_t index) {
	struct network_node* node = &network->nodelist[index];
	node->edgelist = realloc_dynarray(node->edgelist, node->edges + 1);
	if (node->edgelist == NULL) return -1;
	return node->edges++;
}

struct network_link {
	uint32_t u, v, cost;
};

ssize_t network_add_link(struct network* network, struct network_link link) {
	ssize_t node_u = network_add_node(network, link.u);
	if (node_u == -1) {
		return -1;
	}
	ssize_t node_v = network_add_node(network, link.v);
	if (node_v == -1) {
		return -1;
	}

	size_t edge_u = network_add_edge(network, node_u);
	if (edge_u == -1) return -1;
	size_t edge_v = network_add_edge(network, node_v);
	if (edge_v == -1) return -1;
	network->nodelist[node_u].edgelist[edge_u] = (struct network_edge) {
		.index = node_v, .edge = edge_v, .dst = link.v, .cost = link.cost};
	network->nodelist[node_v].edgelist[edge_v] = (struct network_edge) {
		.index = node_u, .edge = edge_u, .dst = link.u, .cost = link.cost};
	return 0;
}

void network_deinit(struct network* network) {
	if (!network->nodelist) return;
	for (size_t i = 0; i != network->nodes; ++i) {
		struct network_node* node = &network->nodelist[i];
		node->edgelist = realloc_dynarray(node->edgelist, 0);
	}
	network->nodelist = realloc_dynarray(network->nodelist, 0);
}

struct network network_init(FILE* fd) {
	struct network network = {0};
	struct network_link link = {0};
	for (size_t i = 0; !feof(fd); ++i) {
		int n = fscanf(fd, "%d,%d,%d\n", &link.u, &link.v, &link.cost);
		if (n != 3) {
			fprintf(stderr, "Error parsing line %zd\n", i);
			network_deinit(&network);
			break;
		}
		if (network_add_link(&network, link) < 0) {
			fprintf(stderr, "Could not add link at line %zd\n", i);
			network_deinit(&network);
			break;
		}
	}
	return network;
}

struct writer {
	ssize_t dest;
	uint8_t buf[128];
};

struct connection {
	thrd_t thrd;
	mtx_t mtx;
	struct network network;
	int* sockets;
	int* rfds;
	struct writer* wfds;
	int nfds;
};

int server_forward(struct connection* conn, size_t index, uint8_t* buff) {
	struct network_node* node = &conn->network.nodelist[index];
	size_t edges = node->edges;
	uint8_t i = buff[0];
	if (i == 0xFF) {
		printf("Received message from #%d to send to all neighbors\n",
			   node->src),
			fflush(stdout);
		conn->wfds = realloc_dynarray(conn->wfds, conn->nfds + edges);
		if (conn->wfds == NULL) {
			perror("malloc");
			return -1;
		}
		struct writer* w = conn->wfds + conn->nfds;
		conn->nfds += edges;
		for (size_t i = 0; i != edges; ++i) {
			w[i].dest = node->edgelist[i].index;
			assert(w[i].dest < conn->network.nodes);
			size_t edge = node->edgelist[i].edge;
			assert(edge <= 0xFF);
			w[i].buf[0] = edge;
			memcpy(w[i].buf + 1, buff + 1, 127);
		}
	} else {
		if (i >= node->edges) {
			fprintf(stderr, "Error: Index out of bounds %d\n", i);
			return -1;
		}
		conn->wfds = realloc_dynarray(conn->wfds, conn->nfds + 1);
		if (conn->wfds == NULL) {
			perror("malloc");
			return -1;
		}
		struct writer* w = conn->wfds + conn->nfds;
		conn->nfds += 1;
		{
			w->dest = node->edgelist[i].index;
			assert(w->dest < conn->network.nodes);
			size_t edge = node->edgelist[i].edge;
			assert(edge <= 0xFF);
			w->buf[0] = edge;
			memcpy(w->buf + 1, buff + 1, 127);
		}
		printf("Received message from #%d to send to #%d (neighbor %d)\n",
			   node->src, node->edgelist[i].dst, i),
			fflush(stdout);
	}
	return 0;
}

int server(void* arg) {
	struct connection* conn = (struct connection*) arg;
	struct network network = conn->network;
	size_t nodes = network.nodes;
	uint8_t buff[128 + 1] = {0};
	fd_set rfds, wfds;
	struct timeval timeout = {0};
	for (;;) {
		timeout.tv_usec = HELLO_TIMEOUT * 10;
		mtx_lock(&conn->mtx);
		memcpy(conn->rfds, conn->sockets, nodes * sizeof(int));
		mtx_unlock(&conn->mtx);
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		int maxfd = 0;
		for (size_t i = 0; i != nodes; ++i) {
			int fd = conn->rfds[i];
			if (fd <= 0) continue;
			FD_SET(fd, &rfds);
			if (fd > maxfd) maxfd = fd;
		}
		size_t nfds = conn->nfds;
		conn->nfds = 0;
		for (size_t i = 0; i != nfds; ++i) {
			struct writer* w = &conn->wfds[i];
			if (w->dest < 0) continue;
			conn->wfds[conn->nfds++] = *w;
			int fd = conn->rfds[w->dest];
			if (fd <= 0) continue;
			FD_SET(fd, &wfds);
			if (fd > maxfd) maxfd = fd;
		}
		if (maxfd == 0) {
			continue;
		}
		ssize_t n = select(1 + maxfd, &rfds, &wfds, NULL, &timeout);
		if (n == -1) {
			perror("select");
			thrd_exit(1);
		}
		if (n == 0) continue;
		for (size_t i = 0; i != conn->nfds; ++i) {
			struct writer* w = &conn->wfds[i];
			if (w->dest < 0) continue;
			int fd = conn->rfds[w->dest];
			assert(w->dest < conn->network.nodes);
			if (fd <= 0) continue;
			if (!FD_ISSET(fd, &wfds)) continue;
			uint32_t src = network.nodelist[w->dest].src;
			printf("Sending to #%d\n", src), fflush(stdout);
			int n = send(fd, w->buf, 128, MSG_NOSIGNAL);
			if (n < 0) {
				perror("send");
				fprintf(stderr, "Removing socket of #%d\n", src);
				close(fd);
				conn->rfds[w->dest] = -1;
				mtx_lock(&conn->mtx);
				conn->sockets[w->dest] = -1;
				mtx_unlock(&conn->mtx);
			}
			w->dest = -1;
		}
		for (size_t i = 0; i != nodes; ++i) {
			int fd = conn->rfds[i];
			if (fd <= 0) continue;
			if (!FD_ISSET(fd, &rfds)) continue;
			memset(buff, 0, sizeof buff);
			uint32_t src = network.nodelist[i].src;
			printf("Receiving from #%d\n", src), fflush(stdout);
			ssize_t n = recv(fd, buff, 128, MSG_WAITALL);
			if (n < 0) {
				perror("recv");
			} else if (n == 0) {
				fprintf(stderr, "Empty buffer of #%d\n", src);
			}
			if (n <= 0 || server_forward(conn, i, buff) < 0) {
				fprintf(stderr, "Removing socket of #%d\n", src);
				close(fd);
				conn->rfds[i] = -1;
				mtx_lock(&conn->mtx);
				conn->sockets[i] = -1;
				mtx_unlock(&conn->mtx);
			}
		}
	}
	thrd_exit(1);
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage : %s <file>\n", argv[0]);
		return 1;
	}
	uint16_t port = PORT;
	FILE* fd = fopen(argv[1], "r");
	if (fd == NULL) {
		perror("file");
		return 1;
	}
	struct network network = network_init(fd);
	fclose(fd);
	if (network.nodelist == NULL) {
		return 1;
	}

	size_t nodes = network.nodes;
	int* sockets = malloc(2 * nodes * sizeof(int));
	if (sockets == NULL) {
		perror("malloc");
		network_deinit(&network);
		return 1;
	}
	memset(sockets, 0, 2 * nodes * sizeof(int));

	struct connection conn = {
		.network = network,
		.sockets = sockets,
		.rfds = sockets + nodes,
		.wfds = NULL,
		.nfds = 0,
	};
	mtx_init(&conn.mtx, mtx_plain);

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		perror("socket");
		free(sockets);
		network_deinit(&network);
		return 1;
	}

	int one = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t) port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sfd);
		free(sockets);
		network_deinit(&network);
		return 1;
	}

	if (listen(sfd, 128) < 0) {
		perror("listen");
		close(sfd);
		free(sockets);
		network_deinit(&network);
		return 1;
	}

	thrd_create(&conn.thrd, server, &conn);
	uint32_t buff = {0};
	for (;;) {
		struct sockaddr_in client;
		socklen_t client_len = sizeof(client);

		printf("Waiting for connection\n"), fflush(stdout);
		int cfd = accept(sfd, (struct sockaddr*) &client, &client_len);
		if (cfd < 0) {
			perror("accept");
			break;
		}
		printf("Accepted connection at: %04x:%d [%d]\n",
			   ntohl(client.sin_addr.s_addr), ntohs(client.sin_port), cfd),
			fflush(stdout);
		int one = 1;
		setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		ssize_t n = recv(cfd, &buff, sizeof buff, MSG_WAITALL);
		if (n < 0) {
			perror("recv");
			close(cfd);
			break;
		}
		uint32_t id = ntohl(buff);
		printf("Received connection from ID: %d [%d]\n", id, cfd),
			fflush(stdout);
		ssize_t node = network_find(&network, id);
		if (node == -1) {
			fprintf(stderr, "Error: Do not know ID: %x\n", id);
			close(cfd);
			continue;
		}
		mtx_lock(&conn.mtx);
		if (sockets[node] > 0) close(sockets[node]);
		sockets[node] = cfd;
		mtx_unlock(&conn.mtx);
	}
	thrd_join(conn.thrd, NULL);
	mtx_destroy(&conn.mtx);
	close(sfd);
	free(sockets);
	network_deinit(&network);
	return 0;
}

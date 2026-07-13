/*
 * nodeproc: one node in the distributed Bellman-Ford network. It talks only to its
 * direct neighbours (via netproc) and works out the root, its distance to it,
 * and the next hop toward it.
 *
 * Single-threaded, select()-driven. We wait on three deadlines - the hello
 * beacon, our root info going stale, and the process lifetime - each an absolute
 * time in ms, and sleep until the nearest. bf.h's timeouts are in seconds, so
 * they get a x1000. Printed times are measured from startup.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "bf.h"
#include "bfnode.h"
#include "net.h"
#include "proto.h"
#include "time_util.h"

/* Set at startup; printed times are relative to this. */
static double start_ms;

static float event_time(double now) {
	return (float) (now - start_ms) / 1e3f;
}

/* Print where we sit in the tree; self-root has no parent, so NULL / distance 0. */
static void print_state(const struct node_state* s, double now) {
	if (node_is_self_root(s)) {
		printf("time=%.01f\tRoot=%d\tparent=NULL\tdistance=%d\n",
			   event_time(now), (int) s->my_root, (int) s->my_cost);
	} else {
		printf("time=%.01f\tRoot=%d\tparent=%d\tdistance=%d\n", event_time(now),
			   (int) s->my_root, (int) s->parent_id, (int) s->my_cost);
	}
	fflush(stdout);
}

/* Broadcast our view to all neighbours. Counts as a beacon, so it also resets
 * the hello timer. Returns -1 if netproc is gone. */
static int do_send(int sock, const struct node_state* s, double* hello_deadline) {
	double now = now_ms();
	struct bf_msg m;
	uint8_t payload[PAYLOAD_LEN];
	node_build_msg(s, &m, now);
	bf_msg_pack(payload, &m);

	if (net_send_frame(sock, LINK_BROADCAST, payload) < 0) return -1;

	printf("time=%.01f\tMessage sent to all neighbors\n", event_time(now));
	fflush(stdout);
	*hello_deadline = now + HELLO_TIMEOUT * 1000;  /* seconds to ms */
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc < 5) {
		fprintf(stderr,
				"Usage: %s <netproc_address> <node_id> <lifetime> <cost1> "
				"[cost2 ...]\n",
				argv[0]);
		return 1;
	}

	const char* address = argv[1];
	uint32_t node_id = (uint32_t) strtoul(argv[2], NULL, 10);
	long lifetime = strtol(argv[3], NULL, 10);
	int num_links = argc - 4;

	uint32_t* costs = malloc((size_t) num_links * sizeof *costs);
	if (costs == NULL) {
		perror("malloc");
		return 1;
	}
	for (int i = 0; i < num_links; ++i)
		costs[i] = (uint32_t) strtoul(argv[4 + i], NULL, 10);

	int sock = net_connect(address, PORT);
	if (sock < 0) {
		free(costs);
		return 1;
	}
	if (net_send_id(sock, node_id) < 0) {
		fprintf(stderr, "Failed to send ID handshake\n");
		close(sock);
		free(costs);
		return 1;
	}

	start_ms = now_ms();
	double lifetime_deadline = start_ms + (double) lifetime * 1000.0;
	double hello_deadline = 0;

	struct node_state s;
	node_init(&s, node_id, costs, num_links);

	/* Announce ourselves right away: coming online is a change worth sharing. */
	print_state(&s, now_ms());
	if (do_send(sock, &s, &hello_deadline) < 0) goto cleanup;

	for (;;) {
		double now = now_ms();

		/* Sleep until the first of our three deadlines comes due. */
		double next = lifetime_deadline;
		if (hello_deadline < next) next = hello_deadline;
		if (!node_is_self_root(&s) && s.exp_deadline_ms < next)
			next = s.exp_deadline_ms;

		double wait = next - now;
		if (wait < 0) wait = 0;
		struct timeval tv;
		tv.tv_sec = (long) (wait / 1000.0);
		tv.tv_usec = (long) ((wait - (double) tv.tv_sec * 1000.0) * 1000.0);

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		int r = select(sock + 1, &rfds, NULL, NULL, &tv);
		if (r < 0) {
			if (errno == EINTR) continue;
			perror("select");
			break;
		}

		now = now_ms();

		/* Time's up before anything else we might do. */
		if (now >= lifetime_deadline) {
			printf("time=%.01f\tLifetime expired. Shutting down.\n",
				   event_time(now));
			fflush(stdout);
			break;
		}

		if (r > 0 && FD_ISSET(sock, &rfds)) {
			uint8_t link;
			uint8_t payload[PAYLOAD_LEN];
			int rc = net_recv_frame(sock, &link, payload);
			if (rc <= 0) {  /* netproc closed on us or errored out */
				fprintf(stderr, "netproc connection lost; shutting down\n");
				break;
			}
			/* Skip anything tagged with a link we don't actually have. */
			if (link < (uint8_t) s.num_links) {
				struct bf_msg m;
				bf_msg_unpack(&m, payload);
				if (node_handle_msg(&s, link, &m, now)) {
					print_state(&s, now_ms());
					if (do_send(sock, &s, &hello_deadline) < 0) break;
				}
			}
		}

		/* Our root has been quiet too long -- assume it's gone and take over. */
		if (!node_is_self_root(&s) && now_ms() >= s.exp_deadline_ms) {
			node_expire(&s);
			print_state(&s, now_ms());
			if (do_send(sock, &s, &hello_deadline) < 0) break;
		}

		/* Nothing changed but the beacon is due, so send one anyway. */
		if (now_ms() >= hello_deadline) {
			if (do_send(sock, &s, &hello_deadline) < 0) break;
		}
	}

cleanup:
	close(sock);
	free(costs);
	return 0;
}

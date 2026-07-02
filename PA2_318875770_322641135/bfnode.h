#ifndef BFNODE_H_
#define BFNODE_H_

#include <stdint.h>

#include "proto.h"

#define LINK_NONE (-1)

/* One node's Bellman-Ford state - just the algorithm, no sockets. Being our own
 * root means my_root == my_id, cost 0, no parent, infinite deadline.
 *
 * parent_link is the link an update came in on; parent_id is the neighbour's ID,
 * and that's what we print as the parent (the spec prints parent=217, which
 * can't be a link number). */
struct node_state {
	uint32_t my_id;
	uint32_t my_root;
	uint32_t my_cost;
	int parent_link;
	uint32_t parent_id;
	double exp_deadline_ms;    /* when current root info goes stale; inf if we're root */
	const uint32_t* costs;     /* costs[i] = cost of link i */
	int num_links;
};

void node_init(struct node_state* s, uint32_t id, const uint32_t* costs,
			   int num_links);

int node_is_self_root(const struct node_state* s);

/* Fold in an update from the given link. Returns 1 if our root or cost changed
 * (time to reprint and rebroadcast); a bare deadline refresh doesn't count. */
int node_handle_msg(struct node_state* s, int link, const struct bf_msg* m,
					double now_ms);

/* Root went silent too long; give up and become our own root. */
int node_expire(struct node_state* s);

/* Fill in the update we're about to send, with the life left on our root info. */
void node_build_msg(const struct node_state* s, struct bf_msg* m, double now_ms);

#endif  // BFNODE_H_

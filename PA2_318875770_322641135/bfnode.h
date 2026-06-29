#ifndef BFNODE_H_
#define BFNODE_H_

#include <stdint.h>

#include "proto.h"

#define LINK_NONE (-1)

/*
 * Per-node Bellman-Ford state. Pure logic: this module never touches sockets.
 *
 * Self-root condition  <=>  my_root == my_id
 *                      <=>  my_cost == 0, parent_link == LINK_NONE,
 *                           exp_deadline_ms == +infinity.
 */
struct node_state {
	uint32_t my_id;            /* constant: our own ID */
	uint32_t my_root;          /* smallest ID known */
	uint32_t my_cost;          /* cost to my_root */
	int parent_link;           /* local link index of next hop, or LINK_NONE */
	uint32_t parent_id;        /* node ID of next hop (this is what we print) */
	double exp_deadline_ms;    /* absolute time current root info expires (+inf if self) */
	const uint32_t* costs;     /* costs[i] = cost of link i */
	int num_links;             /* number of incident links */
};

/* Initialise to self-root (root=self, cost=0, no parent, no expiry). */
void node_init(struct node_state* s, uint32_t id, const uint32_t* costs,
			   int num_links);

/* True iff we are currently our own root. */
int node_is_self_root(const struct node_state* s);

/*
 * Process an update received on local link `link`. Returns 1 if my_root or
 * my_cost changed (caller must then print state + broadcast), else 0.
 * Note: a same-root deadline refresh alone does NOT count as a change.
 */
int node_handle_msg(struct node_state* s, int link, const struct bf_msg* m,
					double now_ms);

/* Root info expired: reset to self-root. Returns 1 (state changed). */
int node_expire(struct node_state* s);

/* Build the update we currently advertise. exp_ms is ROOT_TIMEOUT*1000 when we
 * are the root, else the remaining time to our deadline (>= 0). */
void node_build_msg(const struct node_state* s, struct bf_msg* m, double now_ms);

#endif  // BFNODE_H_

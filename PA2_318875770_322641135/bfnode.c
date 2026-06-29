#include "bfnode.h"

#include <math.h>

#include "bf.h"

void node_init(struct node_state* s, uint32_t id, const uint32_t* costs,
			   int num_links) {
	s->my_id = id;
	s->my_root = id;
	s->my_cost = 0;
	s->parent_link = LINK_NONE;
	s->parent_id = id;
	s->exp_deadline_ms = INFINITY;
	s->costs = costs;
	s->num_links = num_links;
}

int node_is_self_root(const struct node_state* s) {
	return s->my_root == s->my_id;
}

int node_handle_msg(struct node_state* s, int link, const struct bf_msg* m,
					double now_ms) {
	uint32_t link_cost = s->costs[link];
	uint32_t cand = m->cost + link_cost;  /* cost to m->root via this neighbor */
	int changed = 0;

	if (m->root < s->my_root) {
		/* Strictly smaller root: always adopt; deadline starts fresh. */
		s->my_root = m->root;
		s->my_cost = cand;
		s->parent_link = link;
		s->parent_id = m->id;
		s->exp_deadline_ms = now_ms + (double) m->exp_ms;
		changed = 1;
	} else if (m->root == s->my_root && !node_is_self_root(s)) {
		/* Same (non-self) root: this proves the root is alive, so refresh the
		 * deadline -- but expTime may only INCREASE (ignore stale duplicates). */
		double new_deadline = now_ms + (double) m->exp_ms;
		if (new_deadline > s->exp_deadline_ms) s->exp_deadline_ms = new_deadline;

		if (m->id == s->parent_id) {
			/* Update straight from our next hop: must follow it even if the
			 * cost INCREASED (distance-vector correctness, e.g. on a loop). */
			s->parent_link = link;
			if (cand != s->my_cost) {
				s->my_cost = cand;
				changed = 1;
			}
		} else if (cand < s->my_cost) {
			/* A strictly cheaper path via a different neighbor: switch. */
			s->my_cost = cand;
			s->parent_link = link;
			s->parent_id = m->id;
			changed = 1;
		}
		/* Equal cost via a non-parent: keep current parent (no flapping). */
	}
	/* m->root > my_root, or m->root == my_id while we are root: ignore. */

	return changed;
}

int node_expire(struct node_state* s) {
	s->my_root = s->my_id;
	s->my_cost = 0;
	s->parent_link = LINK_NONE;
	s->parent_id = s->my_id;
	s->exp_deadline_ms = INFINITY;
	return 1;
}

void node_build_msg(const struct node_state* s, struct bf_msg* m,
					double now_ms) {
	m->root = s->my_root;
	m->cost = s->my_cost;
	m->id = s->my_id;
	if (node_is_self_root(s)) {
		/* A root's message has "infinite" life; originate at ROOT_TIMEOUT.
		 * (seconds -> ms: the mandatory x1000 conversion.) */
		m->exp_ms = (uint32_t) (ROOT_TIMEOUT * 1000);
	} else {
		double rem = s->exp_deadline_ms - now_ms;  /* aging happens here */
		if (rem < 0) rem = 0;
		m->exp_ms = (uint32_t) rem;
	}
}

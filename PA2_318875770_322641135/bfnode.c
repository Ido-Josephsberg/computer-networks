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
	uint32_t cand = m->cost + link_cost;  /* cost to this root via this neighbour */
	int changed = 0;

	if (m->root < s->my_root) {
		/* Smaller root wins; take it and restart its clock. */
		s->my_root = m->root;
		s->my_cost = cand;
		s->parent_link = link;
		s->parent_id = m->id;
		s->exp_deadline_ms = now_ms + (double) m->exp_ms;
		changed = 1;
	} else if (m->root == s->my_root && !node_is_self_root(s)) {
		/* Same root, still alive: push the deadline out, but never backwards. */
		double new_deadline = now_ms + (double) m->exp_ms;
		if (new_deadline > s->exp_deadline_ms) s->exp_deadline_ms = new_deadline;

		if (m->id == s->parent_id) {
			/* From our own parent, so follow it even if the cost rose - ignoring
			 * that is how loops count to infinity. */
			s->parent_link = link;
			if (cand != s->my_cost) {
				s->my_cost = cand;
				changed = 1;
			}
		} else if (cand < s->my_cost) {
			/* A cheaper path via someone else. */
			s->my_cost = cand;
			s->parent_link = link;
			s->parent_id = m->id;
			changed = 1;
		}
		/* A tie from a non-parent isn't worth flapping the tree for. */
	}
	/* Bigger root, or our own ID while we're root: ignore. */

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
		/* A root's messages don't expire; stamp ROOT_TIMEOUT (seconds to ms). */
		m->exp_ms = (uint32_t) (ROOT_TIMEOUT * 1000);
	} else {
		/* Time left on our root info; the absolute deadline makes this count down. */
		double rem = s->exp_deadline_ms - now_ms;
		if (rem < 0) rem = 0;
		m->exp_ms = (uint32_t) rem;
	}
}

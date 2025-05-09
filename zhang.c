#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

/* Pointer tags to represent 4 states:
 * INV: Invalid data
 * DAT: Valid data
 * INS: Data in the insert stage
 * REM: Data being removed
 */
#define S_INV (0)
#define S_DAT (1)
#define S_INS (2)
#define S_REM (3)

#include "bench.h"
#include "lf.h"

#define LFLIST_END(head_ptr, curr_ptr) (head_ptr == curr_ptr)

#define S_SET(uptr, s) ((uptr & ~(uintptr_t)3) | (uintptr_t)s)

inline static int lfhead_state_get(lfhead_t *head)
{
	lfhead_t *next_ret = ck_pr_load_ptr(&head->next_ret);
	int s = (int)((uintptr_t)next_ret & (uintptr_t)3);
	return s;
}

inline static void lfhead_state_set(lfhead_t *head, int new)
{
	lfhead_t *next_ret = ck_pr_load_ptr(&head->next_ret);
	uintptr_t uptr_new = (uintptr_t)next_ret;
	uptr_new = S_SET(uptr_new, new);
	ck_pr_store_ptr(&head->next_ret, (void *)uptr_new);
}

inline static void lfhead_state_fas(lfhead_t *head, int new)
{
	lfhead_t *next_ret = ck_pr_load_ptr(&head->next_ret);
	uintptr_t uptr_new = (uintptr_t)next_ret;
	uptr_new = S_SET(uptr_new, new);
	ck_pr_fas_ptr(&head->next_ret, (void *)uptr_new);
}

inline static bool lfhead_state_cas(lfhead_t *head, int expected, int new)
{
	lfhead_t *next_ret = ck_pr_load_ptr(&head->next_ret);
	uintptr_t uptr_expected = (uintptr_t)next_ret;
	uintptr_t uptr_new = (uintptr_t)next_ret;
	uptr_expected = S_SET(uptr_expected, expected);
	uptr_new = S_SET(uptr_new, new);
	return ck_pr_cas_ptr(&head->next_ret, (void *)uptr_expected,
			     (void *)uptr_new);
}

inline static void enlist(lfhead_t *restrict head, lfhead_t *restrict new)
{
	lfhead_t *old;
	old = ck_pr_load_ptr(&head->next);
	while (1) {
		new->next = old;
		if (ck_pr_cas_ptr_value(&head->next, old, new, &old)) {
			break;
		}
	}
}

inline static bool insert_help(lfhead_t *restrict head, lfhead_t *restrict new,
			       hp_tls_t *restrict hp)
{
	lfhead_t *prev, *curr, *next;
	int s;
	prev = new;
	curr = hp_post(hp, &new->next, HP_CURR);

	while (curr != head) {
		s = lfhead_state_get(curr);

		if (s == S_INV) {
			next = hp_post(hp, &curr->next, HP_NEXT);
			ck_pr_fas_ptr(&prev->next, next);
			curr = next;
			hp_inherit(hp, HP_NEXT, HP_CURR);
		} else if (curr != new) {
			prev = curr;
			hp_inherit(hp, HP_CURR, HP_PREV);
			next = hp_post(hp, &curr->next, HP_NEXT);
			curr = next;
			hp_inherit(hp, HP_NEXT, HP_CURR);
		} else if (s == S_REM) {
			return true;
		} else if (s == S_INS || s == S_DAT) {
			return false;
		}
	}

	return true;
}

inline static bool del_help(lfhead_t *restrict head, lfhead_t *restrict target,
			    lfhead_t *restrict dummy,
			    hp_tls_t *restrict hp)
{
	lfhead_t *prev, *curr, *next;
	int s;
	prev = dummy;
	curr = hp_post(hp, &dummy->next, HP_CURR);

	while (curr != head) {
		s = lfhead_state_get(curr);

		if (s == S_INV) {
			next = hp_post(hp, &curr->next, HP_NEXT);
			ck_pr_fas_ptr(&prev->next, next);
			curr = next;
			hp_inherit(hp, HP_NEXT, HP_CURR);
		} else if (curr != target) {
			prev = curr;
			hp_inherit(hp, HP_CURR, HP_PREV);
			next = hp_post(hp, &curr->next, HP_NEXT);
			curr = next;
			hp_inherit(hp, HP_NEXT, HP_CURR);
		} else if (s == S_REM) {
			return false;
		} else if (s == S_INS) {
			return lfhead_state_cas(curr, S_INS, S_REM);
		} else if (s == S_DAT) {
			lfhead_state_fas(curr, S_INV);
			return true;
		}
	}

	return true;
}

inline static bool insert(lfhead_t *restrict head, lfhead_t *restrict new,
			  hp_tls_t *restrict hp)
{
	bool b = true;
	lfhead_state_set(new, S_INS);
	/* I don't think we need to set new as a hazard pointer due to the
	 * similar reasons as del with dummy. If we are the only thread that
	 * can actually mark it S_INV (if the cas fails) then it will never
	 * be moved to the retire list.
	 */
	enlist(head, new);

	b = insert_help(head, new, hp);

	if (!lfhead_state_cas(new, S_INS, b ? S_DAT : S_INV)) {
		del_help(head, new, new, hp);
		lfhead_state_fas(new, S_INV);
	}
	hp_clear(hp);
	return b;
}

inline static bool del(lfhead_t *restrict head, lfhead_t *restrict target,
		       lfhead_t *restrict dummy, lfhead_t *restrict head_ret,
		       hp_tls_t *restrict hp)
{
	bool b;

	lfhead_state_set(dummy, S_REM);
	/* Dummy will be visible in the list after this operation, but I don't think
	 * we need a hazard pointer for it. The only thread that is allowed to switch
	 * a node from S_REM to S_INV is the "owner" of that node. If the node cannot
	 * be logically deleted during the lifetime of this call then it will never
	 * go on the retire list.
	 */
	enlist(head, dummy);

	b = del_help(head, target, dummy, hp);
	hp_clear(hp);
	lfhead_state_fas(dummy, S_INV);

	/* THIS IS NOT CORRECT!!!! ONLY DOING THIS FOR BENCHMARK TO SEE
	 * WHAT IF
	 */
	if (b) {
		retire_push(head_ret, target);
		retire_push(head_ret, dummy);
	}

	return b;
}

inline static bool find(lfhead_t *restrict head, lfhead_t *restrict target,
			hp_tls_t *restrict hp)
{
	bool result = false;
	lfhead_t *next, *curr, *prev;
	int s;

	prev = head;
	curr = hp_post(hp, &head->next, HP_CURR);

	while (curr != head) {
		s = lfhead_state_get(curr);
		if (curr == target) {
			result = s != S_INV && s != S_REM;
			break;
		}
		prev = curr;
		hp_inherit(hp, HP_CURR, HP_PREV);
		next = hp_post(hp, &curr->next, HP_NEXT);
		curr = next;
		hp_inherit(hp, HP_NEXT, HP_CURR);
	}
	hp_clear(hp);
	return result;
}

void *zhang_trfunc(void *varg)
{
	BENCH_DECOMPOSE_ARGS(varg);
	insert_phase_foreach(i)
	{
		insert(head, &nodes[i], hp_tls);
	}
	find_phase_foreach(i)
	{
		find(head, &nodes[i], hp_tls);
	}
	delete_phase_foreach(i)
	{
		del(head, &nodes[i], &dummies[i], head_ret, hp_tls);
	}

	all_phase_foreach(i)
	{
		insert(head, &nodes[i], hp_tls);
		find(head, &nodes[i], hp_tls);
		del(head, &nodes[i], &dummies[i], head_ret, hp_tls);
	}
	finish_find_phase_foreach()
	{
		find(head, &nodes[rops], hp_tls);
	}
	finish_insdel_phase_foreach(i)
	{
		insert(head, &nodes[i], hp_tls);
		del(head, &nodes[i], &dummies[i], head_ret, hp_tls);
	}
	pthread_exit(NULL);
}

void zhang_cleanup(thr_arg_t *arg)
{
	lfhead_t *prev, *curr, *next;
	int s;
	prev = arg->head;
	curr = ck_pr_load_ptr(&prev->next);

	while (curr != arg->head) {
		s = lfhead_state_get(curr);
		if (s == S_INV) {
			next = ck_pr_load_ptr(&curr->next);
			ck_pr_fas_ptr(&prev->next, next);
			curr = next;
			continue;
		}
		prev = curr;
		curr = ck_pr_load_ptr(&curr->next);
	}
}

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

#define S_DAT (0)
#define S_INV (1)
#define S_INS (2)
#define S_REM (3)

/* 8 threads 16,000 inserts, 16,000 deletions per thread
 * V1: Directly from paper ~19-25ms
 * V2: State kept in 2 lower pointer bits ~17-23ms
 */

struct lflist_head {
	struct lflist_head *next;
};
typedef struct lflist_head lflist_head_t;

struct integer_entry {
	lflist_head_t integers;
	int x;
};

/* Pointer tags to represent 4 states:
 * DAT: Valid data
 * INV: Invalid data
 * INS: Data in the insert stage
 * REM: Data being removed
 */
#define STATE_DAT ((uintptr_t)0)
#define STATE_INV ((uintptr_t)1)
#define STATE_INS ((uintptr_t)2)
#define STATE_REM ((uintptr_t)3)

#define PTR_IN_STATE(ptr, state) ((void *)(((uintptr_t)(ptr)) & state))
#define PTR_SET_STATE(ptr, state) \
	((void *)((((uintptr_t)(ptr)) & ~((uintptr_t)3)) | state))
#define PTR_GET_STATE(ptr) ((uintptr_t)ptr & (uintptr_t)3)

#define PTR_DAT(ptr) PTR_IN_STATE(ptr, STATE_DAT)
#define PTR_INV(ptr) PTR_IN_STATE(ptr, STATE_INV)
#define PTR_INS(ptr) PTR_IN_STATE(ptr, STATE_INS)
#define PTR_REM(ptr) PTR_IN_STATE(ptr, STATE_REM)

#define PTR_SET_DAT(ptr) PTR_SET_STATE(ptr, STATE_DAT)
#define PTR_SET_INV(ptr) PTR_SET_STATE(ptr, STATE_INV)
#define PTR_SET_INS(ptr) PTR_SET_STATE(ptr, STATE_INS)
#define PTR_SET_REM(ptr) PTR_SET_STATE(ptr, STATE_REM)

#define LFLIST_END(head_ptr, curr_ptr) (head_ptr == curr_ptr)

#define lflist_entry(lflist_head_ptr, entry_type, entry_lflist_head_member) \
	((entry_type *)((uintptr_t)(lflist_head_ptr) -                      \
			offsetof(entry_type, entry_lflist_head_member)))

inline static void enlist_ins(struct lflist_head *head, struct lflist_head *new)
{
	struct lflist_head *old;
	old = ck_pr_load_ptr(&head->next);
	while (1) {
		new->next = PTR_SET_INS(old);
		if (ck_pr_cas_ptr_value(&head->next, old, new, &old)) {
			break;
		}
	}
}

inline static void enlist_del(struct lflist_head *head, struct lflist_head *new)
{
	struct lflist_head *old;
	old = ck_pr_load_ptr(&head->next);
	while (1) {
		new->next = PTR_SET_REM(old);
		if (ck_pr_cas_ptr_value(&head->next, old, new, &old)) {
			break;
		}
	}
}

inline static bool insert_help(struct lflist_head *head,
			       struct lflist_head *new)
{
	struct lflist_head *prev, *curr, *next;
	void *curr_raw;
	uintptr_t s;
	prev = PTR_SET_DAT(new);
	curr_raw = ck_pr_load_ptr(&prev->next);
	curr = PTR_SET_DAT(curr_raw);

	while (curr != head) {
		next = ck_pr_load_ptr(&curr->next);
		s = PTR_GET_STATE(next);

		if (s == S_INV) {
			ck_pr_fas_ptr(&prev->next, next);
			curr_raw = next;
			curr = PTR_SET_DAT(curr_raw);
		} else if (curr != new) {
			prev = curr;
			curr_raw = ck_pr_load_ptr(&curr->next);
			curr = PTR_SET_DAT(curr_raw);
		} else if (s == S_REM) {
			return true;
		} else if (s == S_INS || s == S_DAT) {
			return false;
		}
	}

	return true;
}

inline static bool del_help(struct lflist_head *head,
			    struct lflist_head *target,
			    struct lflist_head *dummy)
{
	struct lflist_head *prev, *curr, *next;
	void *curr_raw;
	uintptr_t s;
	prev = PTR_SET_DAT(dummy);
	curr_raw = ck_pr_load_ptr(&prev->next);
	curr = PTR_SET_DAT(curr_raw);

	while (curr != head) {
		next = ck_pr_load_ptr(&curr->next);
		s = PTR_GET_STATE(next);

		if (s == S_INV) {
			ck_pr_fas_ptr(&prev->next, next);
			curr_raw = next;
			curr = PTR_SET_DAT(curr_raw);
		} else if (curr != target) {
			prev = curr;
			curr_raw = ck_pr_load_ptr(&curr->next);
			curr = PTR_SET_DAT(curr_raw);
		} else if (s == S_REM) {
			return false;
		} else if (s == S_INS) {
			return ck_pr_cas_ptr(&curr->next, next, PTR_SET_REM(next));
		} else if (s == S_DAT) {
			ck_pr_fas_ptr(&curr->next, PTR_SET_INV(next));
			return true;
		}
	}

	return true;
}

inline static bool insert(struct lflist_head *restrict head,
			  struct lflist_head *restrict new)
{
	bool b = true;
	struct lflist_head *new_orig = PTR_SET_DAT(new);
	void *ins_state_ptr;
	void *new_state_ptr;

	enlist_ins(head, new);
	ins_state_ptr = PTR_SET_INS(new_orig->next);

	b = insert_help(head, new);

	new_state_ptr = b ? PTR_SET_DAT(new_orig->next) : PTR_SET_INV(new_orig->next);
	if (!ck_pr_cas_ptr(&new_orig->next, ins_state_ptr, new_state_ptr)) {
		del_help(head, new, new);
		ck_pr_fas_ptr(&new_orig->next, PTR_SET_INV(new_orig->next));
	}
	return b;
}

inline static bool del(struct lflist_head *restrict head,
		       struct lflist_head *restrict target,
		       struct lflist_head *restrict dummy)
{
	bool b;
	struct lflist_head *dummy_orig = PTR_SET_DAT(dummy);

	enlist_del(head, dummy);

	b = del_help(head, target, dummy);
	ck_pr_fas_ptr(&dummy_orig->next, PTR_SET_INV(dummy_orig->next));

	return b;
}

static void _integer_list_print(struct lflist_head *head)
{
	void *curr_raw = ck_pr_load_ptr(&head->next);
	struct lflist_head *curr = PTR_SET_DAT(curr_raw);
	struct lflist_head *next;
	int i = 1;

	printf("[0]: %p\n", head);
	while (curr != head) {
		struct integer_entry *e;
		next = ck_pr_load_ptr(&curr->next);
		int s = PTR_GET_STATE(next);

		if (s == S_INV || s == S_REM) {
			curr_raw = ck_pr_load_ptr(&curr->next);
			curr = PTR_SET_DAT(curr_raw);
			continue;
		}

		e = lflist_entry(curr, struct integer_entry, integers);

		printf("[%d]: %p -> %d\n", i, curr, e->x);

		curr_raw = ck_pr_load_ptr(&curr->next);
		curr = PTR_SET_DAT(curr_raw);
		i += 1;
	}
}

static void *pthread_runner(void *arg)
{
	struct lflist_head *head = (struct lflist_head *)arg;
	int i;

#define LEN (16000)
	struct integer_entry *es = malloc(sizeof(*es) * LEN);
	struct lflist_head *dummies = malloc(sizeof(*dummies) * LEN);

	for (i = 0; i < LEN; ++i) {
		es[i].x = i;
		insert(head, &es[i].integers);
		del(head, &es[i].integers, &dummies[i]);
	}
	pthread_exit(NULL);
#undef LEN
}

int main(void)
{
	lflist_head_t head;
	head.next = &head;
	char buf[128];
#define NTS (8)
	pthread_t tids[NTS];

	struct pf_hw_timer timer;

	pf_hw_timer_start(&timer);
	for (int i = 0; i < NTS; ++i) {
		pthread_create(&tids[i], NULL, pthread_runner, &head);
	}

	for (int i = 0; i < NTS; ++i) {
		pthread_join(tids[i], NULL);
	}
	pf_hw_timer_end(&timer, PF_TSC_FREQ_HZ_INTEL_12700K);

	_integer_list_print(&head);

	pf_timer_pretty_time(&timer.duration, PF_HW_TIMER_US, 2, buf, 128);

	printf("%s\n", buf);

	return 0;
}

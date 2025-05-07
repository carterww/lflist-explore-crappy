#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

#define NUM_THREADS (8)
#define OPS_PER_THREAD (16000)
#define HP_PER_THREAD (3)

struct lflist_head {
	struct lflist_head *next;
	struct lflist_head *retired_next;
};
typedef struct lflist_head lflist_head_t;
typedef void lflist_head_unsafe_t;

struct integer_entry {
	lflist_head_t integers;
	int x;
};

struct hp_t {
	struct lflist_head *hps[HP_PER_THREAD];
} __attribute__((aligned(64)));

#define PTR_MARK ((uintptr_t)1)

inline static bool is_marked(void *ptr)
{
	uintptr_t uptr = (uintptr_t)ptr;
	return uptr & PTR_MARK;
}

inline static bool is_unmarked(void *ptr)
{
	return !is_marked(ptr);
}

inline static lflist_head_unsafe_t *mark(void *ptr)
{
	uintptr_t uptr = (uintptr_t)ptr;
	return (lflist_head_unsafe_t *)(uptr | PTR_MARK);
}

inline static lflist_head_t *unmark(void *ptr)
{
	uintptr_t uptr = (uintptr_t)ptr;
	return (lflist_head_t *)(uptr & ~PTR_MARK);
}

#define LFLIST_END(head_ptr, curr_ptr) (head_ptr == unmark(curr_ptr))

#define lflist_entry(lflist_head_ptr, entry_type, entry_lflist_head_member) \
	((entry_type *)((uintptr_t)(lflist_head_ptr) -                      \
			offsetof(entry_type, entry_lflist_head_member)))

static lflist_head_t head;
static lflist_head_t retired_head;
static pthread_t tids[NUM_THREADS];
static struct hp_t hps[NUM_THREADS];

inline static void hp_reader_barrier(void)
{
	// ck_pr_fence_memory();
	ck_pr_barrier();
}

inline static void hp_clear(uint64_t tidx)
{
	hps[tidx].hps[0] = NULL;
	hps[tidx].hps[1] = NULL;
	hps[tidx].hps[2] = NULL;
}

inline static void hp_inherit(uint64_t tidx, uint64_t from, uint64_t to)
{
	hps[tidx].hps[to] = hps[tidx].hps[from];
	hp_reader_barrier();
}

inline static struct lflist_head *hp_post(struct lflist_head **t, uint64_t tidx,
					  uint64_t hp_num)
{
	struct lflist_head *val;

	(void)tidx;
	(void)hp_num;

	while (1) {
		/* Relaxed load target and relaxed store hp */
		val = *t;
		hps[tidx].hps[hp_num] = val;
		/* Make sure hp is visible */
		hp_reader_barrier();
		/* Check if target changed between load and store
		 * This retries if so.
		 */
		if (*t == val) {
			return val;
		} else {
			continue;
		}
	}
}

inline static void retire_push(struct lflist_head *t)
{
	struct lflist_head *old = retired_head.retired_next;
	do {
		t->retired_next = old;
	} while (
		!ck_pr_cas_ptr_value(&retired_head.retired_next, old, t, &old));
}

inline static struct lflist_head *retire_pop(void)
{
	struct lflist_head *old = retired_head.retired_next;
	while (!ck_pr_cas_ptr_value(&retired_head.retired_next, old,
				    old->retired_next, &old))
		;
	return old;
}

inline static bool find(struct lflist_head *t, uint64_t tidx,
			struct lflist_head **pnext, struct lflist_head **pcurr,
			struct lflist_head **pprev)
{
	/* HP requires inheriting pointers to have a greater index than the
	 * value they are inheriting from. curr inherits from next and prev
	 * inherits from curr.
	 */
	struct lflist_head *next, *curr, *prev;
	struct lflist_head *nexts, *currs, *prevs;
try_again:
	prev = &head;
	curr = hp_post(&head.next, tidx, 1); /* Mark curr as hp1 */
	while (1) {
		prevs = unmark(prev);
		currs = unmark(curr);
		next = hp_post(&currs->next, tidx, 0); /* Mark next as hp0 */
		if (currs == &head) {
			*pprev = prev;
			*pcurr = curr;
			*pnext = next;
			return false;
		}
		if (currs->next != next)
			goto try_again;
		if (prevs->next != curr)
			goto try_again;
		if (is_unmarked(next)) {
			if (t == currs) {
				*pprev = prev;
				*pcurr = curr;
				*pnext = next;
				return true;
			}
			prev = curr;
			hp_inherit(tidx, 1, 2);
		} else {
			if (ck_pr_cas_ptr(&prevs->next, unmark(curr),
					  unmark(next))) {
				retire_push(currs);
			} else {
				goto try_again;
			}
		}
		curr = next;
		hp_inherit(tidx, 0, 1);
	}
}

inline static bool insert(struct lflist_head *new, uint64_t tidx)
{
	bool result;

	(void)tidx;

	while (1) {
		new->next = head.next;
		if (ck_pr_cas_ptr(&head.next, new->next, new)) {
			result = true;
			break;
		}
	}
	hp_clear(tidx);
	return result;
}

inline static bool delete(struct lflist_head *target, uint64_t tidx)
{
	bool result;
	struct lflist_head *next, *curr, *prev;
	struct lflist_head *nexts, *currs, *prevs;

	while (1) {
		if (!find(target, tidx, &next, &curr, &prev)) {
			result = false;
			break;
		}
		nexts = unmark(next);
		currs = unmark(curr);
		prevs = unmark(prev);
		if (!ck_pr_cas_ptr(&currs->next, next, mark(next))) {
			continue;
		}
		if (ck_pr_cas_ptr(&prevs->next, unmark(curr), next)) {
			retire_push(target);
		} else {
			find(target, tidx, &next, &curr, &prev);
		}
		result = true;
		break;
	}

	hp_clear(tidx);
	return result;
}

static void *pthread_runner(void *arg)
{
	uint64_t tidx = (uint64_t)arg;
	int i;

	struct integer_entry *es = malloc(sizeof(*es) * OPS_PER_THREAD);
	for (i = 0; i < OPS_PER_THREAD; ++i) {
		es[i].x = i;
		insert(&es[i].integers, tidx);
		delete (&es[i].integers, tidx);
	}
	pthread_exit(NULL);
}

static void _integer_list_print(void)
{
	void *curr_raw = ck_pr_load_ptr(&head.next);
	struct lflist_head *curr = unmark(curr_raw);
	int i = 1;

	printf("[0]: %p\n", &head);
	while (!LFLIST_END(&head, curr_raw)) {
		struct integer_entry *e;

		curr = unmark(curr_raw);

		if (is_marked(curr_raw)) {
			curr_raw = curr->next;
			continue;
		}
		e = lflist_entry(curr, struct integer_entry, integers);

		printf("[%d]: %p -> %d\n", i, curr, e->x);

		curr_raw = ck_pr_load_ptr(&curr->next);
		i += 1;
	}
}

static void _retire_list_print(void)
{
	struct lflist_head *curr = retire_pop();
	int i = 1;

	while (curr != &retired_head) {
		struct integer_entry *e;
		e = lflist_entry(curr, struct integer_entry, integers);
		printf("[%d]: %p -> %d\n", i, curr, e->x);

		curr = retire_pop();
		i += 1;
	}
}

int main(void)
{
	char buf[128];
	struct pf_hw_timer timer;

	head.next = &head;
	retired_head.retired_next = &retired_head;
	pf_hw_timer_start(&timer);
	for (uint64_t i = 0; i < NUM_THREADS; ++i) {
		pthread_create(&tids[i], NULL, pthread_runner, (void *)i);
	}
	for (int i = 0; i < NUM_THREADS; ++i) {
		pthread_join(tids[i], NULL);
	}
	pf_hw_timer_end(&timer, PF_TSC_FREQ_HZ_INTEL_12700K);

	_integer_list_print();
	// _retire_list_print();

	pf_timer_pretty_time(&timer.duration, PF_HW_TIMER_US, 2, buf, 128);
	printf("%s\n", buf);

	return 0;
}

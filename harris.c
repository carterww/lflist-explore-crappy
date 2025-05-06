#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

struct lflist_head {
	struct lflist_head *next;
};
typedef struct lflist_head lflist_head_t;

struct integer_entry {
	lflist_head_t integers;
	int x;
};

#define MARK_PTR(ptr) ((void *)(((uintptr_t)(ptr)) | (uintptr_t)0x1))
#define UNMARK_PTR(ptr) ((void *)(((uintptr_t)(ptr)) & ~(uintptr_t)0x1))
#define IS_MARKED_PTR(ptr) ((void *)(((uintptr_t)(ptr)) & (uintptr_t)0x1))

#define LFLIST_END(head_ptr, curr_ptr) (head_ptr == curr_ptr)

#define lflist_entry(lflist_head_ptr, entry_type, entry_lflist_head_member) \
	((entry_type *)((uintptr_t)(lflist_head_ptr) -                      \
			offsetof(entry_type, entry_lflist_head_member)))

inline static void lflist_init(struct lflist_head *head)
{
	head->next = head;
}

inline static void lflist_add(struct lflist_head *restrict head,
			      struct lflist_head *restrict new)
{
	struct lflist_head *next;

	next = ck_pr_load_ptr(&head->next);
	do {
		new->next = next;
	} while (!ck_pr_cas_ptr_value(&head->next, next, (void *)new, &next));
}

inline static bool lflist_del_harris(struct lflist_head *restrict head,
				     struct lflist_head *restrict target)
{
	struct lflist_head *prev;
	struct lflist_head *curr;

	prev = head;
	curr = ck_pr_load_ptr(&head->next);

	while (!LFLIST_END(head, curr)) {
		if (IS_MARKED_PTR(curr)) {
			prev = head;
			curr = ck_pr_load_ptr(&head->next);
			continue;
		}
		if (curr == target) {
			struct lflist_head *next = ck_pr_load_ptr(&curr->next);
			while (!IS_MARKED_PTR(next)) {
				if (ck_pr_cas_ptr(&curr->next, next,
						  MARK_PTR(next)))
					break;
				next = ck_pr_load_ptr(&curr->next);
			}

			if (ck_pr_cas_ptr(&prev->next, curr,
					  UNMARK_PTR(next))) {
				return true;
			} else {
				prev = head;
				curr = ck_pr_load_ptr(&head->next);
				continue;
			}
		}

		prev = curr;
		curr = ck_pr_load_ptr(&curr->next);
	}

	return false;
}

inline static bool lflist_empty(struct lflist_head *head)
{
	struct lflist_head *next = ck_pr_load_ptr(&head->next);

	return LFLIST_END(head, next);
}

static void _integer_list_print(struct lflist_head *head)
{
	void *curr_raw = ck_pr_load_ptr(&head->next);
	struct lflist_head *curr = UNMARK_PTR(curr_raw);
	int i = 1;

	printf("[0]: %p\n", head);
	while (!LFLIST_END(head, UNMARK_PTR(curr_raw))) {
		struct integer_entry *e;

		curr = UNMARK_PTR(curr_raw);
		e = lflist_entry(curr, struct integer_entry, integers);

		printf("[%d]: %p -> %d\n", i, curr, e->x);

		curr_raw = ck_pr_load_ptr(&curr->next);
		i += 1;
	}
}

struct thread_arg {
	lflist_head_t *head;
	int idx;
};

static void *pthread_runner(void *arg)
{
	struct thread_arg *a = (struct thread_arg *)arg;
	lflist_head_t *head = a->head;
	int idx = a->idx;
	int i;

#define LEN (16000)
	struct integer_entry *es = malloc(sizeof(*es) * LEN);
	for (i = 0; i < LEN; ++i) {
		es[i].x = i;
		lflist_add(head, &es[i].integers);
		lflist_del_harris(head, &es[i].integers);
	}
	pthread_exit(NULL);
#undef LEN
}

int main(void)
{
	lflist_head_t head;
	lflist_init(&head);
	char buf[128];
#define NTS (8)
	pthread_t tids[NTS];
	struct thread_arg args[NTS];

	struct pf_hw_timer timer;

	pf_hw_timer_start(&timer);
	for (int i = 0; i < NTS; ++i) {
		args[i].head = &head;
		args[i].idx = i;
		pthread_create(&tids[i], NULL, pthread_runner, &args[i]);
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

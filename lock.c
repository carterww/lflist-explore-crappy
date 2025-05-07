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

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define lflist_entry(lflist_head_ptr, entry_type, entry_lflist_head_member) \
	((entry_type *)((uintptr_t)(lflist_head_ptr) -                      \
			offsetof(entry_type, entry_lflist_head_member)))

inline static void insert(struct lflist_head *restrict head,
			  struct lflist_head *restrict new)
{
	pthread_mutex_lock(&lock);
	struct lflist_head *next = head->next;
	new->next = next;
	head->next = new;
	pthread_mutex_unlock(&lock);
}

inline static bool del(struct lflist_head *restrict head,
		       struct lflist_head *restrict target)
{
	pthread_mutex_lock(&lock);
	struct lflist_head *prev = head;
	struct lflist_head *curr = head->next;
	while (curr != head) {
		if (curr != target) {
			prev = curr;
			curr = curr->next;
			continue;
		}
		prev->next = curr->next;
		pthread_mutex_unlock(&lock);
		return true;
	}
	pthread_mutex_unlock(&lock);
	return false;
}

static void _integer_list_print(struct lflist_head *head)
{
	struct lflist_head *prev = head;
	struct lflist_head *curr = ck_pr_load_ptr(&head->next);
	int i = 1;

	printf("[0]: %p\n", head);
	while (curr != head) {
		struct integer_entry *e;

		e = lflist_entry(curr, struct integer_entry, integers);

		printf("[%d]: %p -> %d\n", i, curr, e->x);

		curr = ck_pr_load_ptr(&curr->next);
		i += 1;
	}
}

static void *pthread_runner(void *arg)
{
	struct lflist_head *head = (struct lflist_head *)arg;
	int i;

#define LEN (16000)
	struct integer_entry *es = malloc(sizeof(*es) * LEN);

	for (i = 0; i < LEN; ++i) {
		es[i].x = i;
		insert(head, &es[i].integers);
		del(head, &es[i].integers);
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

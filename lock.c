#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

#include "bench.h"
#include "lf.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

inline static void insert(lfhead_t *restrict head, lfhead_t *restrict new)
{
	pthread_mutex_lock(&lock);
	lfhead_t *next = head->next;
	new->next = next;
	head->next = new;
	pthread_mutex_unlock(&lock);
}

inline static bool del(lfhead_t *restrict head, lfhead_t *restrict target)
{
	pthread_mutex_lock(&lock);
	lfhead_t *prev = head;
	lfhead_t *curr = head->next;
	while (curr != head) {
		if (curr == target) {
			prev->next = curr->next;
			pthread_mutex_unlock(&lock);
			return true;
		}
		prev = curr;
		curr = curr->next;
	}
	pthread_mutex_unlock(&lock);
	return false;
}

inline static bool find(lfhead_t *restrict head, lfhead_t *restrict target)
{
	pthread_mutex_lock(&lock);
	lfhead_t *prev = head;
	lfhead_t *curr = head->next;
	while (curr != head) {
		if (curr == target) {
			pthread_mutex_unlock(&lock);
			return true;
		}
		prev = curr;
		curr = curr->next;
	}
	pthread_mutex_unlock(&lock);
	return false;
}

void *lock_trfunc(void *varg)
{
	BENCH_DECOMPOSE_ARGS(varg);
	insert_phase_foreach(i)
	{
		insert(head, &nodes[i]);
	}
	find_phase_foreach(i)
	{
		find(head, &nodes[i]);
	}
	delete_phase_foreach(i)
	{
		if (del(head, &nodes[i])) {
			/* We don't really have to do this because we could just free() it
			 * here, but the benchmark checks for correctness by popping from
			 * this.
			 */
			retire_push(head_ret, &nodes[i]);
		}
	}

	all_phase_foreach(i)
	{
		insert(head, &nodes[i]);
		find(head, &nodes[i]);
		if (del(head, &nodes[i])) {
			retire_push(head_ret, &nodes[i]);
		}
	}
	finish_find_phase_foreach()
	{
		find(head, &nodes[rops]);
	}
	finish_insdel_phase_foreach(i)
	{
		insert(head, &nodes[i]);
		if (del(head, &nodes[i])) {
			retire_push(head_ret, &nodes[i]);
		}
	}
	pthread_exit(NULL);
}

void lock_cleanup(thr_arg_t *arg)
{
	(void)arg;
	return;
}

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ck_pr.h>

#include <pf_hw_timer.h>

#include "bench.h"
#include "lf.h"

#define ARR_LEN(a) (sizeof(a) / sizeof(*(a)))

#define TMAX (16)
#define OPS_MAX (100000)
static const uint64_t thr_nums[] = { 1, 2, 4, 8, TMAX };
static const int64_t thr_ops_num[] = { OPS_MAX };
static const double read_percents[] = { 0.9, 0.8, 0.5, 0.2, 0.0 };

// #define TMAX (2)
// #define OPS_MAX (10000)
// static const uint64_t thr_nums[] = { TMAX };
// static const int64_t thr_ops_num[] = { OPS_MAX };
// static const double read_percents[] = { 0.0 };

static lfhead_t head;
static lfhead_t head_ret;

static pthread_t tids[TMAX];
static thr_arg_t targs[TMAX];
static hp_tls_t hps[TMAX];

static lfhead_t nodes[TMAX][OPS_MAX];
static lfhead_t dummies[TMAX][OPS_MAX];

static void reset_args(void)
{
	for (int i = 0; i < TMAX; ++i) {
		head.next = &head;
		head_ret.next_ret = &head_ret;
		targs[i].hp_tls = &hps[i];
		targs[i].read_ops = 0;

		targs[i].nodes = NULL;
		targs[i].node_num = 0;

		hp_clear(&hps[i]);
	}
}

static bool verify_list_state(uint64_t thrn, uint64_t ins, uint64_t del, bool is_zhang)
{
	uint64_t exist = 0;
	uint64_t retired = 0;

	uint64_t exist_expect = del > ins ? 0 : ins - del;
	uint64_t retired_expect = del > ins ? ins : del;
	if (is_zhang) {
		retired_expect *= 2;
	}

	exist_expect *= thrn;
	retired_expect *= thrn;

	lfhead_t *curr = ck_pr_load_ptr(&head.next);
	lfhead_t *curr_ret = ck_pr_load_ptr(&head_ret.next_ret);

	while (curr != &head) {
		curr = ck_pr_load_ptr(&curr->next);
		++exist;
	}
	while (curr_ret != &head_ret) {
		curr_ret = ck_pr_load_ptr(&curr_ret->next_ret);
		++retired;
	}

	return exist_expect == exist && retired_expect == retired;
}

static void fill_args(uint64_t thrn, int64_t opn, int64_t ropn)
{
	for (uint64_t t = 0; t < thrn; ++t) {
		thr_arg_t *a = &targs[t];
		a->tidx = t;
		a->head = &head;
		a->head_ret = &head_ret;
		a->dummies = dummies[t];
		a->hp_tls = &hps[t];
		a->randseed = (unsigned int)time(NULL) + ((unsigned int)t * 30);
		a->read_ops = ropn;
		a->nodes = nodes[t];
		a->node_num = (uint64_t)opn;
	}
}

static void execute(uint64_t thrn, int64_t opn, int64_t ropn,
		    void *(*func)(void *), void (*cleanup_func)(thr_arg_t *))
{
	struct pf_hw_timer timer;
	char buff[128];
	int64_t total_ops = opn;

	opn = opn - ropn;
	while (opn + ropn > total_ops) {
		--ropn;
	}
	while (opn + ropn < total_ops) {
		++opn;
	}
	reset_args();
	fill_args(thrn, opn, ropn);

	pf_hw_timer_start(&timer);
	for (uint64_t t = 0; t < thrn; ++t) {
		pthread_create(&tids[t], NULL, func, &targs[t]);
	}
	for (uint64_t t = 0; t < thrn; ++t) {
		pthread_join(tids[t], NULL);
	}
	pf_hw_timer_end(&timer, PF_TSC_FREQ_HZ_INTEL_12700K);
	cleanup_func(&targs[0]);
	if (!verify_list_state(thrn, (uint64_t)opn, (uint64_t)opn, func == zhang_trfunc)) {
		printf("fail!\n");
	}
	enum pf_hw_timer_units unit = PF_HW_TIMER_MS;
	pf_timer_pretty_time(&timer.duration, unit, 2, buff, 128);

	double totops = (double)total_ops;
	double idops = (double)opn;
	double rops = (double)ropn;
	double perins = (idops * 50 / totops);
	double perdel = perins;
	double perread = (rops * 100 / totops);
	double sec = (double)timer.duration.tv_sec;
	double ns = (double)timer.duration.tv_nsec;
	double us = (sec * 1000000) + (ns / 1000);
	double ops = totops * (double)thrn;
	printf("Threads:  %2lu; ", thrn);
	printf("Insert:  %3.0f%%; ", perins);
	printf("Delete:  %3.0f%%; ", perdel);
	printf("Read:  %3.0f%%; ", perread);
	printf("Ops/µs:  %6.3f; ", ops / us);
	printf("Elapsed Time:  %s\n", buff);
}

int main(int argc, char *argv[])
{
	size_t opidx = 0, opidx_end = ARR_LEN(thr_ops_num);
	size_t ridx = 0, ridx_end = ARR_LEN(read_percents);
	size_t tidx = 0, tidx_end = ARR_LEN(thr_nums);
	void *(*func)(void *);
	void (*cleanup_func)(thr_arg_t *);

	if (argc > 1) {
		if (strcmp(argv[1], "lock") == 0) {
			func = lock_trfunc;
			cleanup_func = lock_cleanup;
		} else if (strcmp(argv[1], "zhang") == 0) {
			func = zhang_trfunc;
			cleanup_func = zhang_cleanup;
		}else {
			printf("Please specify a valid implementation: { lock }\n");
			return 1;
		}
	} else {
		func = lock_trfunc;
		cleanup_func = lock_cleanup;
	}

	for (opidx = 0; opidx < opidx_end; ++opidx) {
		for (ridx = 0; ridx < ridx_end; ++ridx) {
			for (tidx = 0; tidx < tidx_end; ++tidx) {
				int64_t opn = thr_ops_num[opidx];
				double rper = read_percents[ridx];
				uint64_t thrn = thr_nums[tidx];

				int64_t ropn = (int64_t)(rper * (double)opn);
				execute(thrn, opn, ropn, func, cleanup_func);
			}
		}
	}

	return 0;
}

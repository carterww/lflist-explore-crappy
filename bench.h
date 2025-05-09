#ifndef BENCH_H
#define BENCH_H

#include <stdlib.h>

#include "lf.h"

struct thr_arg {
	uint64_t tidx;
	lfhead_t *head;
	lfhead_t *head_ret;
	lfhead_t *dummies;
	hp_tls_t *hp_tls;
	unsigned int randseed;

	int64_t read_ops;

	lfhead_t *nodes;
	size_t node_num;
};
typedef struct thr_arg thr_arg_t;

inline static size_t rand_insert_n(unsigned int *seed, size_t max_ins)
{
	unsigned int randn;
	unsigned int max;

	randn = (unsigned int)rand_r(seed);
	max = (unsigned int)(max_ins / 16) + 1;
	max = max > 500 ? 500 : max;
	return randn % max;
}

#define BENCH_DECOMPOSE_ARGS(varg)               \
	thr_arg_t *arg = (thr_arg_t *)(varg);    \
	lfhead_t *head = (arg)->head;            \
	lfhead_t *head_ret = (arg)->head_ret;    \
	lfhead_t *dummies = (arg)->dummies;      \
	hp_tls_t *hp_tls = (arg)->hp_tls;        \
	unsigned int *seed = &((arg)->randseed); \
	int64_t rops = (arg)->read_ops;          \
	lfhead_t *nodes = (arg)->nodes;          \
	size_t node_num = (arg)->node_num;       \
	size_t rand_ins = rand_insert_n(seed, node_num);

#define insert_phase_foreach(idx_name) \
	for (unsigned int idx_name = 0; idx_name < rand_ins; ++idx_name)

#define find_phase_foreach(idx_name)                                     \
	for (unsigned int idx_name = 0; idx_name < rand_ins && rops > 0; \
	     ++idx_name, --rops)

#define delete_phase_foreach(idx_name) insert_phase_foreach(idx_name)

#define all_phase_foreach(idx_name)                                \
	size_t idx_name;                                           \
	for (idx_name = rand_ins; idx_name < node_num && rops > 0; \
	     ++idx_name, --rops)

#define finish_find_phase_foreach() for (; rops > 0; --rops)

#define finish_insdel_phase_foreach(idx_name) \
	for (; idx_name < node_num; ++idx_name)

void *lock_trfunc(void *arg);
void *harris_trfunc(void *arg);
void *michael_trfunc(void *arg);
void *zhang_trfunc(void *arg);

void lock_cleanup(thr_arg_t *arg);
void harris_cleanup(thr_arg_t *arg);
void michael_cleanup(thr_arg_t *arg);
void zhang_cleanup(thr_arg_t *arg);

#endif /* BENCH_H */

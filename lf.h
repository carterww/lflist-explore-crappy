#ifndef LF_H
#define LF_H

#include <stddef.h>

#include <ck_pr.h>

#define CACHELINE_BYTES (64)
#define HPS_MAX (CACHELINE_BYTES / sizeof(void *))

struct lfhead {
	struct lfhead *next;
	struct lfhead *next_ret;
};
typedef struct lfhead lfhead_t;
typedef void lfhead_unsafe_t;

struct hp_tls {
	struct lfhead *hps[HPS_MAX];
} __attribute__((aligned(CACHELINE_BYTES)));
typedef struct hp_tls hp_tls_t;

#define HP_PREV (2)
#define HP_CURR (1)
#define HP_NEXT (0)

inline static void hp_local_fence(void)
{
	// Full fence not required if using membarier() on consumer
	// ck_pr_fence_memory();
	ck_pr_barrier();
}

inline static void hp_clear(hp_tls_t *h)
{
	/* Only use 3 entries for now */
	h->hps[0] = NULL;
	h->hps[1] = NULL;
	h->hps[2] = NULL;
}

/* from <= to */
inline static void hp_inherit(hp_tls_t *h, uint64_t from, uint64_t to)
{
	h->hps[to] = h->hps[from];
	hp_local_fence();
}

inline static lfhead_t *hp_post(hp_tls_t *h, lfhead_t **src_ptr, uint64_t n)
{
	lfhead_t *val;

	while (1) {
		/* Relaxed load target and relaxed store hp */
		val = ck_pr_load_ptr(src_ptr);
		h->hps[n] = val;
		/* Make sure hp is visible */
		hp_local_fence();
		/* Check if target changed between load and store
		 * This retries if so.
		 */
		if (ck_pr_load_ptr(src_ptr) == val) {
			return val;
		} else {
			continue;
		}
	}
}

/* HP retire list will be a Treiber stack. MPSC */
inline static void retire_push(lfhead_t *rhead, lfhead_t *tar)
{
	lfhead_t *old = ck_pr_load_ptr(&rhead->next_ret);
	do {
		tar->next_ret = old;
	} while (!ck_pr_cas_ptr_value(&rhead->next_ret, old, tar, &old));
}
inline static lfhead_t *retire_pop(lfhead_t *rhead)
{
	lfhead_t *old = ck_pr_load_ptr(&rhead->next_ret);
	while (!ck_pr_cas_ptr_value(&rhead->next_ret, old, old->next_ret, &old))
		;
	return old;
}

#endif /* LF_H */

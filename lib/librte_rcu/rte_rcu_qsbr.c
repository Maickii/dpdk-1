/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Arm Limited
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_malloc.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_atomic.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_errno.h>

#include "rte_rcu_qsbr.h"

/* Get the memory size of QSBR variable */
size_t __rte_experimental
rte_rcu_qsbr_get_memsize(uint32_t max_threads)
{
	size_t sz;

	if (max_threads == 0) {
		rte_log(RTE_LOG_ERR, rcu_log_type,
			"%s(): Invalid max_threads %u\n",
			__func__, max_threads);
		rte_errno = EINVAL;

		return 1;
	}

	sz = sizeof(struct rte_rcu_qsbr);

	/* Add the size of quiescent state counter array */
	sz += sizeof(struct rte_rcu_qsbr_cnt) * max_threads;

	/* Add the size of the registered thread ID bitmap array */
	sz += RTE_QSBR_THRID_ARRAY_SIZE(max_threads);

	return RTE_ALIGN(sz, RTE_CACHE_LINE_SIZE);
}

/* Initialize a quiescent state variable */
int __rte_experimental
rte_rcu_qsbr_init(struct rte_rcu_qsbr *v, uint32_t max_threads)
{
	size_t sz;

	if (v == NULL) {
		rte_log(RTE_LOG_ERR, rcu_log_type,
			"%s(): Invalid input parameter\n", __func__);
		rte_errno = EINVAL;

		return 1;
	}

	sz = rte_rcu_qsbr_get_memsize(max_threads);
	if (sz == 1)
		return 1;

	/* Set all the threads to offline */
	memset(v, 0, sz);
	v->max_threads = max_threads;
	v->num_elems = RTE_ALIGN_MUL_CEIL(max_threads,
			RTE_QSBR_THRID_ARRAY_ELM_SIZE) /
			RTE_QSBR_THRID_ARRAY_ELM_SIZE;
	v->token = RTE_QSBR_CNT_INIT;

	return 0;
}

/* Register a reader thread to report its quiescent state
 * on a QS variable.
 */
int __rte_experimental
rte_rcu_qsbr_thread_register(struct rte_rcu_qsbr *v, unsigned int thread_id)
{
	unsigned int i, id, success;
	uint64_t old_bmap, new_bmap;

	if (v == NULL || thread_id >= v->max_threads) {
		rte_log(RTE_LOG_ERR, rcu_log_type,
			"%s(): Invalid input parameter\n", __func__);
		rte_errno = EINVAL;

		return 1;
	}

	id = thread_id & RTE_QSBR_THRID_MASK;
	i = thread_id >> RTE_QSBR_THRID_INDEX_SHIFT;

	/* Make sure that the counter for registered threads does not
	 * go out of sync. Hence, additional checks are required.
	 */
	/* Check if the thread is already registered */
	old_bmap = __atomic_load_n(RTE_QSBR_THRID_ARRAY_ELM(v, i),
					__ATOMIC_RELAXED);
	if (old_bmap & 1UL << id)
		return 0;

	do {
		new_bmap = old_bmap | (1UL << id);
		success = __atomic_compare_exchange(
					RTE_QSBR_THRID_ARRAY_ELM(v, i),
					&old_bmap, &new_bmap, 0,
					__ATOMIC_RELEASE, __ATOMIC_RELAXED);

		if (success)
			__atomic_fetch_add(&v->num_threads,
						1, __ATOMIC_RELAXED);
		else if (old_bmap & (1UL << id))
			/* Someone else registered this thread.
			 * Counter should not be incremented.
			 */
			return 0;
	} while (success == 0);

	return 0;
}

/* Remove a reader thread, from the list of threads reporting their
 * quiescent state on a QS variable.
 */
int __rte_experimental
rte_rcu_qsbr_thread_unregister(struct rte_rcu_qsbr *v, unsigned int thread_id)
{
	unsigned int i, id, success;
	uint64_t old_bmap, new_bmap;

	if (v == NULL || thread_id >= v->max_threads) {
		rte_log(RTE_LOG_ERR, rcu_log_type,
			"%s(): Invalid input parameter\n", __func__);
		rte_errno = EINVAL;

		return 1;
	}

	id = thread_id & RTE_QSBR_THRID_MASK;
	i = thread_id >> RTE_QSBR_THRID_INDEX_SHIFT;

	/* Make sure that the counter for registered threads does not
	 * go out of sync. Hence, additional checks are required.
	 */
	/* Check if the thread is already unregistered */
	old_bmap = __atomic_load_n(RTE_QSBR_THRID_ARRAY_ELM(v, i),
					__ATOMIC_RELAXED);
	if (old_bmap & ~(1UL << id))
		return 0;

	do {
		new_bmap = old_bmap & ~(1UL << id);
		/* Make sure any loads of the shared data structure are
		 * completed before removal of the thread from the list of
		 * reporting threads.
		 */
		success = __atomic_compare_exchange(
					RTE_QSBR_THRID_ARRAY_ELM(v, i),
					&old_bmap, &new_bmap, 0,
					__ATOMIC_RELEASE, __ATOMIC_RELAXED);

		if (success)
			__atomic_fetch_sub(&v->num_threads,
						1, __ATOMIC_RELAXED);
		else if (old_bmap & ~(1UL << id))
			/* Someone else unregistered this thread.
			 * Counter should not be incremented.
			 */
			return 0;
	} while (success == 0);

	return 0;
}

/* Dump the details of a single quiescent state variable to a file. */
int __rte_experimental
rte_rcu_qsbr_dump(FILE *f, struct rte_rcu_qsbr *v)
{
	uint64_t bmap;
	uint32_t i, t;

	if (v == NULL || f == NULL) {
		rte_log(RTE_LOG_ERR, rcu_log_type,
			"%s(): Invalid input parameter\n", __func__);
		rte_errno = EINVAL;

		return 1;
	}

	fprintf(f, "\nQuiescent State Variable @%p\n", v);

	fprintf(f, "  QS variable memory size = %lu\n",
				rte_rcu_qsbr_get_memsize(v->max_threads));
	fprintf(f, "  Given # max threads = %u\n", v->max_threads);
	fprintf(f, "  Current # threads = %u\n", v->num_threads);

	fprintf(f, "  Registered thread ID mask = 0x");
	for (i = 0; i < v->num_elems; i++)
		fprintf(f, "%lx", __atomic_load_n(
					RTE_QSBR_THRID_ARRAY_ELM(v, i),
					__ATOMIC_ACQUIRE));
	fprintf(f, "\n");

	fprintf(f, "  Token = %lu\n",
			__atomic_load_n(&v->token, __ATOMIC_ACQUIRE));

	fprintf(f, "Quiescent State Counts for readers:\n");
	for (i = 0; i < v->num_elems; i++) {
		bmap = __atomic_load_n(RTE_QSBR_THRID_ARRAY_ELM(v, i),
					__ATOMIC_ACQUIRE);
		while (bmap) {
			t = __builtin_ctzl(bmap);
			fprintf(f, "thread ID = %d, count = %lu\n", t,
				__atomic_load_n(
					&v->qsbr_cnt[i].cnt,
					__ATOMIC_RELAXED));
			bmap &= ~(1UL << t);
		}
	}

	return 0;
}

int rcu_log_type;

RTE_INIT(rte_rcu_register)
{
	rcu_log_type = rte_log_register("lib.rcu");
	if (rcu_log_type >= 0)
		rte_log_set_level(rcu_log_type, RTE_LOG_ERR);
}

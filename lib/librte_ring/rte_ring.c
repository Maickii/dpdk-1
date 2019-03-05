/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010-2015 Intel Corporation
 * Copyright (c) 2007,2008 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 * Derived from FreeBSD's bufring.h
 * Used as BSD-3 Licensed with permission from Kip Macy.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_atomic.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_spinlock.h>

#include "rte_ring.h"

TAILQ_HEAD(rte_ring_list, rte_tailq_entry);

static struct rte_tailq_elem rte_ring_tailq = {
	.name = RTE_TAILQ_RING_NAME,
};
EAL_REGISTER_TAILQ(rte_ring_tailq)

/* true if x is a power of 2 */
#define POWEROF2(x) ((((x)-1) & (x)) == 0)

/* return the size of memory occupied by a ring */
ssize_t
rte_ring_get_memsize_v1905(unsigned int count, unsigned int flags)
{
	ssize_t sz, elt_sz;

	/* count must be a power of 2 */
	if ((!POWEROF2(count)) || (count > RTE_RING_SZ_MASK )) {
		RTE_LOG(ERR, RING,
			"Requested size is invalid, must be power of 2, and "
			"do not exceed the size limit %u\n", RTE_RING_SZ_MASK);
		return -EINVAL;
	}

	elt_sz = (flags & RING_F_LF) ? 2 * sizeof(void *) : sizeof(void *);

	sz = sizeof(struct rte_ring) + count * elt_sz;
	sz = RTE_ALIGN(sz, RTE_CACHE_LINE_SIZE);
	return sz;
}
BIND_DEFAULT_SYMBOL(rte_ring_get_memsize, _v1905, 19.05);
MAP_STATIC_SYMBOL(ssize_t rte_ring_get_memsize(unsigned int count,
					       unsigned int flags),
		  rte_ring_get_memsize_v1905);

ssize_t
rte_ring_get_memsize_v20(unsigned int count)
{
	return rte_ring_get_memsize_v1905(count, 0);
}
VERSION_SYMBOL(rte_ring_get_memsize, _v20, 2.0);

int
rte_ring_init(struct rte_ring *r, const char *name, unsigned count,
	unsigned flags)
{
	int ret;

	/* compilation-time checks */
	RTE_BUILD_BUG_ON((sizeof(struct rte_ring) &
			  RTE_CACHE_LINE_MASK) != 0);
	RTE_BUILD_BUG_ON((offsetof(struct rte_ring, cons) &
			  RTE_CACHE_LINE_MASK) != 0);
	RTE_BUILD_BUG_ON((offsetof(struct rte_ring, prod) &
			  RTE_CACHE_LINE_MASK) != 0);
	RTE_BUILD_BUG_ON(sizeof(struct rte_ring_lf_entry) !=
			 2 * sizeof(void *));

	/* init the ring structure */
	memset(r, 0, sizeof(*r));
	ret = snprintf(r->name, sizeof(r->name), "%s", name);
	if (ret < 0 || ret >= (int)sizeof(r->name))
		return -ENAMETOOLONG;
	r->flags = flags;

	if (flags & RING_F_EXACT_SZ) {
		r->size = rte_align32pow2(count + 1);
		r->mask = r->size - 1;
		r->capacity = count;
	} else {
		if ((!POWEROF2(count)) || (count > RTE_RING_SZ_MASK)) {
			RTE_LOG(ERR, RING,
				"Requested size is invalid, must be power of 2, and not exceed the size limit %u\n",
				RTE_RING_SZ_MASK);
			return -EINVAL;
		}
		r->size = count;
		r->mask = count - 1;
		r->capacity = r->mask;
	}

	r->log2_size = rte_log2_u64(r->size);

	if (flags & RING_F_LF) {
		uint32_t i;

		r->prod_ptr.single =
			(flags & RING_F_SP_ENQ) ? __IS_SP : __IS_MP;
		r->cons_ptr.single =
			(flags & RING_F_SC_DEQ) ? __IS_SC : __IS_MC;
		r->prod_ptr.head = r->cons_ptr.head = 0;
		r->prod_ptr.tail = r->cons_ptr.tail = 0;

		for (i = 0; i < r->size; i++) {
			struct rte_ring_lf_entry *ring_ptr, *base;

			base = (struct rte_ring_lf_entry *)&r->ring;

			ring_ptr = &base[i & r->mask];

			ring_ptr->cnt = 0;
		}
	} else {
		r->prod.single = (flags & RING_F_SP_ENQ) ? __IS_SP : __IS_MP;
		r->cons.single = (flags & RING_F_SC_DEQ) ? __IS_SC : __IS_MC;
		r->prod.head = r->cons.head = 0;
		r->prod.tail = r->cons.tail = 0;
	}

	return 0;
}

/* If a ring entry is written on average every M cycles, then a ring entry is
 * reused every M*count cycles, and a ring entry's counter repeats every
 * M*count*2^32 cycles. If M=100 on a 2GHz system, then a 1024-entry ring's
 * counters would repeat every 2.37 days. The likelihood of ABA occurring is
 * considered sufficiently low for 1024-entry and larger rings.
 */
#define MIN_32_BIT_LF_RING_SIZE 1024

/* create the ring */
struct rte_ring *
rte_ring_create(const char *name, unsigned count, int socket_id,
		unsigned flags)
{
	char mz_name[RTE_MEMZONE_NAMESIZE];
	struct rte_ring *r;
	struct rte_tailq_entry *te;
	const struct rte_memzone *mz;
	ssize_t ring_size;
	int mz_flags = 0;
	struct rte_ring_list* ring_list = NULL;
	const unsigned int requested_count = count;
	int ret;

	ring_list = RTE_TAILQ_CAST(rte_ring_tailq.head, rte_ring_list);

#ifdef RTE_ARCH_64
#if !defined(RTE_ARCH_X86_64)
	printf("This platform does not support the atomic operation required for RING_F_LF\n");
	rte_errno = EINVAL;
	return NULL;
#endif
#else
	if ((flags & RING_F_LF) && count < MIN_32_BIT_LF_RING_SIZE) {
		printf("RING_F_LF is only supported on 32-bit platforms for rings with at least 1024 entries.\n");
		rte_errno = EINVAL;
		return NULL;
	}
#endif

	/* for an exact size ring, round up from count to a power of two */
	if (flags & RING_F_EXACT_SZ)
		count = rte_align32pow2(count + 1);

	ring_size = rte_ring_get_memsize(count, flags);
	if (ring_size < 0) {
		rte_errno = ring_size;
		return NULL;
	}

	ret = snprintf(mz_name, sizeof(mz_name), "%s%s",
		RTE_RING_MZ_PREFIX, name);
	if (ret < 0 || ret >= (int)sizeof(mz_name)) {
		rte_errno = ENAMETOOLONG;
		return NULL;
	}

	te = rte_zmalloc("RING_TAILQ_ENTRY", sizeof(*te), 0);
	if (te == NULL) {
		RTE_LOG(ERR, RING, "Cannot reserve memory for tailq\n");
		rte_errno = ENOMEM;
		return NULL;
	}

	rte_rwlock_write_lock(RTE_EAL_TAILQ_RWLOCK);

	/* reserve a memory zone for this ring. If we can't get rte_config or
	 * we are secondary process, the memzone_reserve function will set
	 * rte_errno for us appropriately - hence no check in this this function */
	mz = rte_memzone_reserve_aligned(mz_name, ring_size, socket_id,
					 mz_flags, __alignof__(*r));
	if (mz != NULL) {
		r = mz->addr;
		/* no need to check return value here, we already checked the
		 * arguments above */
		rte_ring_init(r, name, requested_count, flags);

		te->data = (void *) r;
		r->memzone = mz;

		TAILQ_INSERT_TAIL(ring_list, te, next);
	} else {
		r = NULL;
		RTE_LOG(ERR, RING, "Cannot reserve memory\n");
		rte_free(te);
	}
	rte_rwlock_write_unlock(RTE_EAL_TAILQ_RWLOCK);

	return r;
}

/* free the ring */
void
rte_ring_free(struct rte_ring *r)
{
	struct rte_ring_list *ring_list = NULL;
	struct rte_tailq_entry *te;

	if (r == NULL)
		return;

	/*
	 * Ring was not created with rte_ring_create,
	 * therefore, there is no memzone to free.
	 */
	if (r->memzone == NULL) {
		RTE_LOG(ERR, RING, "Cannot free ring (not created with rte_ring_create()");
		return;
	}

	if (rte_memzone_free(r->memzone) != 0) {
		RTE_LOG(ERR, RING, "Cannot free memory\n");
		return;
	}

	ring_list = RTE_TAILQ_CAST(rte_ring_tailq.head, rte_ring_list);
	rte_rwlock_write_lock(RTE_EAL_TAILQ_RWLOCK);

	/* find out tailq entry */
	TAILQ_FOREACH(te, ring_list, next) {
		if (te->data == (void *) r)
			break;
	}

	if (te == NULL) {
		rte_rwlock_write_unlock(RTE_EAL_TAILQ_RWLOCK);
		return;
	}

	TAILQ_REMOVE(ring_list, te, next);

	rte_rwlock_write_unlock(RTE_EAL_TAILQ_RWLOCK);

	rte_free(te);
}

/* dump the status of the ring on the console */
void
rte_ring_dump(FILE *f, const struct rte_ring *r)
{
	fprintf(f, "ring <%s>@%p\n", r->name, r);
	fprintf(f, "  flags=%x\n", r->flags);
	fprintf(f, "  size=%"PRIu32"\n", r->size);
	fprintf(f, "  capacity=%"PRIu32"\n", r->capacity);
	if (r->flags & RING_F_LF) {
		fprintf(f, "  ct=%"PRIuPTR"\n", r->cons_ptr.tail);
		fprintf(f, "  ch=%"PRIuPTR"\n", r->cons_ptr.head);
		fprintf(f, "  pt=%"PRIuPTR"\n", r->prod_ptr.tail);
		fprintf(f, "  ph=%"PRIuPTR"\n", r->prod_ptr.head);
	} else {
		fprintf(f, "  ct=%"PRIu32"\n", r->cons.tail);
		fprintf(f, "  ch=%"PRIu32"\n", r->cons.head);
		fprintf(f, "  pt=%"PRIu32"\n", r->prod.tail);
		fprintf(f, "  ph=%"PRIu32"\n", r->prod.head);
	}
	fprintf(f, "  used=%u\n", rte_ring_count(r));
	fprintf(f, "  avail=%u\n", rte_ring_free_count(r));
}

/* dump the status of all rings on the console */
void
rte_ring_list_dump(FILE *f)
{
	const struct rte_tailq_entry *te;
	struct rte_ring_list *ring_list;

	ring_list = RTE_TAILQ_CAST(rte_ring_tailq.head, rte_ring_list);

	rte_rwlock_read_lock(RTE_EAL_TAILQ_RWLOCK);

	TAILQ_FOREACH(te, ring_list, next) {
		rte_ring_dump(f, (struct rte_ring *) te->data);
	}

	rte_rwlock_read_unlock(RTE_EAL_TAILQ_RWLOCK);
}

/* search a ring from its name */
struct rte_ring *
rte_ring_lookup(const char *name)
{
	struct rte_tailq_entry *te;
	struct rte_ring *r = NULL;
	struct rte_ring_list *ring_list;

	ring_list = RTE_TAILQ_CAST(rte_ring_tailq.head, rte_ring_list);

	rte_rwlock_read_lock(RTE_EAL_TAILQ_RWLOCK);

	TAILQ_FOREACH(te, ring_list, next) {
		r = (struct rte_ring *) te->data;
		if (strncmp(name, r->name, RTE_RING_NAMESIZE) == 0)
			break;
	}

	rte_rwlock_read_unlock(RTE_EAL_TAILQ_RWLOCK);

	if (te == NULL) {
		rte_errno = ENOENT;
		return NULL;
	}

	return r;
}

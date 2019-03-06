/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2017,2018 HXT-semitech Corporation.
 * Copyright (c) 2007-2009 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 * Derived from FreeBSD's bufring.h
 * Used as BSD-3 Licensed with permission from Kip Macy.
 */

#ifndef _RTE_RING_C11_MEM_H_
#define _RTE_RING_C11_MEM_H_

static __rte_always_inline void
update_tail(struct rte_ring_headtail *ht, uint32_t old_val, uint32_t new_val,
		uint32_t single, uint32_t enqueue)
{
	RTE_SET_USED(enqueue);

	/*
	 * If there are other enqueues/dequeues in progress that preceded us,
	 * we need to wait for them to complete
	 */
	if (!single)
		while (unlikely(ht->tail != old_val))
			rte_pause();

	__atomic_store_n(&ht->tail, new_val, __ATOMIC_RELEASE);
}

/**
 * @internal This function updates the producer head for enqueue
 *
 * @param r
 *   A pointer to the ring structure
 * @param is_sp
 *   Indicates whether multi-producer path is needed or not
 * @param n
 *   The number of elements we will want to enqueue, i.e. how far should the
 *   head be moved
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Enqueue a fixed number of items from a ring
 *   RTE_RING_QUEUE_VARIABLE: Enqueue as many items as possible from ring
 * @param old_head
 *   Returns head value as it was before the move, i.e. where enqueue starts
 * @param new_head
 *   Returns the current/new head value i.e. where enqueue finishes
 * @param free_entries
 *   Returns the amount of free space in the ring BEFORE head was moved
 * @return
 *   Actual number of objects enqueued.
 *   If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_move_prod_head(struct rte_ring *r, unsigned int is_sp,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uint32_t *old_head, uint32_t *new_head,
		uint32_t *free_entries)
{
	const uint32_t capacity = r->capacity;
	uint32_t cons_tail;
	unsigned int max = n;
	int success;

	*old_head = __atomic_load_n(&r->prod.head, __ATOMIC_RELAXED);
	do {
		/* Reset n to the initial burst count */
		n = max;

		/* Ensure the head is read before tail */
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		/* load-acquire synchronize with store-release of ht->tail
		 * in update_tail.
		 */
		cons_tail = __atomic_load_n(&r->cons.tail,
					__ATOMIC_ACQUIRE);

		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * *old_head > cons_tail). So 'free_entries' is always between 0
		 * and capacity (which is < size).
		 */
		*free_entries = (capacity + cons_tail - *old_head);

		/* check that we have enough room in ring */
		if (unlikely(n > *free_entries))
			n = (behavior == RTE_RING_QUEUE_FIXED) ?
					0 : *free_entries;

		if (n == 0)
			return 0;

		*new_head = *old_head + n;
		if (is_sp)
			r->prod.head = *new_head, success = 1;
		else
			/* on failure, *old_head is updated */
			success = __atomic_compare_exchange_n(&r->prod.head,
					old_head, *new_head,
					0, __ATOMIC_RELAXED,
					__ATOMIC_RELAXED);
	} while (unlikely(success == 0));
	return n;
}

/**
 * @internal This function updates the consumer head for dequeue
 *
 * @param r
 *   A pointer to the ring structure
 * @param is_sc
 *   Indicates whether multi-consumer path is needed or not
 * @param n
 *   The number of elements we will want to enqueue, i.e. how far should the
 *   head be moved
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Dequeue a fixed number of items from a ring
 *   RTE_RING_QUEUE_VARIABLE: Dequeue as many items as possible from ring
 * @param old_head
 *   Returns head value as it was before the move, i.e. where dequeue starts
 * @param new_head
 *   Returns the current/new head value i.e. where dequeue finishes
 * @param entries
 *   Returns the number of entries in the ring BEFORE head was moved
 * @return
 *   - Actual number of objects dequeued.
 *     If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_move_cons_head(struct rte_ring *r, int is_sc,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uint32_t *old_head, uint32_t *new_head,
		uint32_t *entries)
{
	unsigned int max = n;
	uint32_t prod_tail;
	int success;

	/* move cons.head atomically */
	*old_head = __atomic_load_n(&r->cons.head, __ATOMIC_RELAXED);
	do {
		/* Restore n as it may change every loop */
		n = max;

		/* Ensure the head is read before tail */
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		/* this load-acquire synchronize with store-release of ht->tail
		 * in update_tail.
		 */
		prod_tail = __atomic_load_n(&r->prod.tail,
					__ATOMIC_ACQUIRE);

		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * cons_head > prod_tail). So 'entries' is always between 0
		 * and size(ring)-1.
		 */
		*entries = (prod_tail - *old_head);

		/* Set the actual entries for dequeue */
		if (n > *entries)
			n = (behavior == RTE_RING_QUEUE_FIXED) ? 0 : *entries;

		if (unlikely(n == 0))
			return 0;

		*new_head = *old_head + n;
		if (is_sc)
			r->cons.head = *new_head, success = 1;
		else
			/* on failure, *old_head will be updated */
			success = __atomic_compare_exchange_n(&r->cons.head,
							old_head, *new_head,
							0, __ATOMIC_RELAXED,
							__ATOMIC_RELAXED);
	} while (unlikely(success == 0));
	return n;
}

/**
 * @internal This function updates the producer head for enqueue using
 *	     pointer-sized head/tail values.
 *
 * @param r
 *   A pointer to the ring structure
 * @param is_sp
 *   Indicates whether multi-producer path is needed or not
 * @param n
 *   The number of elements we will want to enqueue, i.e. how far should the
 *   head be moved
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Enqueue a fixed number of items from a ring
 *   RTE_RING_QUEUE_VARIABLE: Enqueue as many items as possible from ring
 * @param old_head
 *   Returns head value as it was before the move, i.e. where enqueue starts
 * @param new_head
 *   Returns the current/new head value i.e. where enqueue finishes
 * @param free_entries
 *   Returns the amount of free space in the ring BEFORE head was moved
 * @return
 *   Actual number of objects enqueued.
 *   If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_move_prod_head_ptr(struct rte_ring *r, unsigned int is_sp,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uintptr_t *old_head, uintptr_t *new_head,
		uint32_t *free_entries)
{
	const uint32_t capacity = r->capacity;
	uintptr_t cons_tail;
	unsigned int max = n;
	int success;

	*old_head = __atomic_load_n(&r->prod_ptr.head, __ATOMIC_RELAXED);
	do {
		/* Reset n to the initial burst count */
		n = max;

		/* Ensure the head is read before tail */
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		/* load-acquire synchronize with store-release of tail in
		 * __rte_ring_do_lf_dequeue_{sc, mc}.
		 */
		cons_tail = __atomic_load_n(&r->cons_ptr.tail,
					__ATOMIC_ACQUIRE);

		*free_entries = (capacity + cons_tail - *old_head);

		/* check that we have enough room in ring */
		if (unlikely(n > *free_entries))
			n = (behavior == RTE_RING_QUEUE_FIXED) ?
					0 : *free_entries;

		if (n == 0)
			return 0;

		*new_head = *old_head + n;
		if (is_sp)
			r->prod_ptr.head = *new_head, success = 1;
		else
			/* on failure, *old_head is updated */
			success = __atomic_compare_exchange_n(&r->prod_ptr.head,
					old_head, *new_head,
					0, __ATOMIC_RELAXED,
					__ATOMIC_RELAXED);
	} while (unlikely(success == 0));

	return n;
}

/**
 * @internal This function updates the consumer head for dequeue using
 *	     pointer-sized head/tail values.
 *
 * @param r
 *   A pointer to the ring structure
 * @param is_sc
 *   Indicates whether multi-consumer path is needed or not
 * @param n
 *   The number of elements we will want to enqueue, i.e. how far should the
 *   head be moved
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Dequeue a fixed number of items from a ring
 *   RTE_RING_QUEUE_VARIABLE: Dequeue as many items as possible from ring
 * @param old_head
 *   Returns head value as it was before the move, i.e. where dequeue starts
 * @param new_head
 *   Returns the current/new head value i.e. where dequeue finishes
 * @param entries
 *   Returns the number of entries in the ring BEFORE head was moved
 * @return
 *   - Actual number of objects dequeued.
 *     If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_move_cons_head_ptr(struct rte_ring *r, unsigned int is_sc,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uintptr_t *old_head, uintptr_t *new_head,
		uint32_t *entries)
{
	unsigned int max = n;
	uintptr_t prod_tail;
	int success;

	/* move cons.head atomically */
	*old_head = __atomic_load_n(&r->cons_ptr.head, __ATOMIC_RELAXED);
	do {
		/* Restore n as it may change every loop */
		n = max;

		/* Ensure the head is read before tail */
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		/* load-acquire synchronize with store-release of tail in
		 * __rte_ring_do_lf_enqueue_{sp, mp}.
		 */
		prod_tail = __atomic_load_n(&r->prod_ptr.tail,
					__ATOMIC_ACQUIRE);

		*entries = (prod_tail - *old_head);

		/* Set the actual entries for dequeue */
		if (n > *entries)
			n = (behavior == RTE_RING_QUEUE_FIXED) ? 0 : *entries;

		if (unlikely(n == 0))
			return 0;

		*new_head = *old_head + n;
		if (is_sc)
			r->cons_ptr.head = *new_head, success = 1;
		else
			/* on failure, *old_head will be updated */
			success = __atomic_compare_exchange_n(&r->cons_ptr.head,
							old_head, *new_head,
							0, __ATOMIC_RELAXED,
							__ATOMIC_RELAXED);
	} while (unlikely(success == 0));

	return n;
}

/**
 * @internal
 *   Enqueue several objects on the lock-free ring (single-producer only)
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Enqueue a fixed number of items to the ring
 *   RTE_RING_QUEUE_VARIABLE: Enqueue as many items as possible to the ring
 * @param free_space
 *   returns the amount of space after the enqueue operation has finished
 * @return
 *   Actual number of objects enqueued.
 *   If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_do_lf_enqueue_sp(struct rte_ring *r, void * const *obj_table,
			    unsigned int n,
			    enum rte_ring_queue_behavior behavior,
			    unsigned int *free_space)
{
	uint32_t free_entries;
	uintptr_t head, next;

	n = __rte_ring_move_prod_head_ptr(r, 1, n, behavior,
					  &head, &next, &free_entries);
	if (n == 0)
		goto end;

	ENQUEUE_PTRS_LF(r, &r->ring, head, obj_table, n);

	__atomic_store_n(&r->prod_ptr.tail,
			 r->prod_ptr.tail + n,
			 __ATOMIC_RELEASE);
end:
	if (free_space != NULL)
		*free_space = free_entries - n;
	return n;
}

/* This macro defines the number of times an enqueueing thread can fail to find
 * a free ring slot before reloading its producer tail index.
 */
#define ENQ_RETRY_LIMIT 32

/**
 * @internal
 *   Get the next producer tail index.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param idx
 *   The local tail index
 * @return
 *   If the ring's tail is ahead of the local tail, return the shared tail.
 *   Else, return tail + 1.
 */
static __rte_always_inline uintptr_t
__rte_ring_reload_tail(struct rte_ring *r, uintptr_t idx)
{
	uintptr_t fresh = __atomic_load_n(&r->prod_ptr.tail, __ATOMIC_RELAXED);

	if ((intptr_t)(idx - fresh) < 0)
		idx = fresh; /* fresh is after idx, use it instead */
	else
		idx++; /* Continue with next slot */

	return idx;
}

/**
 * @internal
 *   Update the ring's producer tail index. If another thread already updated
 *   the index beyond the caller's tail value, do nothing.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param idx
 *   The local tail index
 * @return
 *   If the shared tail is ahead of the local tail, return the shared tail.
 *   Else, return tail + 1.
 */
static __rte_always_inline uintptr_t
__rte_ring_lf_update_tail(struct rte_ring *r, uintptr_t val)
{
	volatile uintptr_t *loc = &r->prod_ptr.tail;
	uintptr_t old = __atomic_load_n(loc, __ATOMIC_RELAXED);

	do {
		/* Check if the tail has already been updated. */
		if ((intptr_t)(val - old) < 0)
			return old;

		/* Else val >= old, need to update *loc */
	} while (!__atomic_compare_exchange_n(loc, &old, val,
					      1, __ATOMIC_RELEASE,
					      __ATOMIC_RELAXED));

	return val;
}

/**
 * @internal
 *   Enqueue several objects on the lock-free ring (multi-producer safe)
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Enqueue a fixed number of items to the ring
 *   RTE_RING_QUEUE_VARIABLE: Enqueue as many items as possible to the ring
 * @param free_space
 *   returns the amount of space after the enqueue operation has finished
 * @return
 *   Actual number of objects enqueued.
 *   If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_do_lf_enqueue_mp(struct rte_ring *r, void * const *obj_table,
			    unsigned int n,
			    enum rte_ring_queue_behavior behavior,
			    unsigned int *free_space)
{
#if !defined(ALLOW_EXPERIMENTAL_API)
	RTE_SET_USED(r);
	RTE_SET_USED(obj_table);
	RTE_SET_USED(n);
	RTE_SET_USED(behavior);
	RTE_SET_USED(free_space);
	printf("[%s()] RING_F_LF requires an experimental API."
	       " Recompile with ALLOW_EXPERIMENTAL_API to use it.\n"
	       , __func__);
	return 0;
#else
	struct rte_ring_lf_entry *base;
	uintptr_t head, next, tail;
	unsigned int i;
	uint32_t avail;

	/* Atomically update the prod head to reserve n slots. The prod tail
	 * is modified at the end of the function.
	 */
	n = __rte_ring_move_prod_head_ptr(r, 0, n, behavior,
					  &head, &next, &avail);

	tail = __atomic_load_n(&r->prod_ptr.tail, __ATOMIC_RELAXED);
	head = __atomic_load_n(&r->cons_ptr.tail, __ATOMIC_ACQUIRE);

	if (unlikely(n == 0))
		goto end;

	base = (struct rte_ring_lf_entry *)&r->ring;

	for (i = 0; i < n; i++) {
		unsigned int retries = 0;
		int success = 0;

		/* Enqueue to the tail entry. If another thread wins the race,
		 * retry with the new tail.
		 */
		do {
			struct rte_ring_lf_entry old_value, new_value;
			struct rte_ring_lf_entry *ring_ptr;

			ring_ptr = &base[tail & r->mask];

			old_value = *ring_ptr;

			if (old_value.cnt != (tail >> r->log2_size)) {
				/* This slot has already been used. Depending
				 * on how far behind this thread is, either go
				 * to the next slot or reload the tail.
				 */
				uintptr_t prev_tail;

				prev_tail = (tail + r->size) >> r->log2_size;

				if (old_value.cnt != prev_tail ||
				    ++retries == ENQ_RETRY_LIMIT) {
					/* This thread either fell 2+ laps
					 * behind or hit the retry limit, so
					 * reload the tail index.
					 */
					tail = __rte_ring_reload_tail(r, tail);
					retries = 0;
				} else {
					/* Slot already used, try the next. */
					tail++;

				}

				continue;
			}

			/* Found a free slot, try to enqueue next element. */
			new_value.ptr = obj_table[i];
			new_value.cnt = (tail + r->size) >> r->log2_size;

#ifdef RTE_ARCH_64
			success = rte_atomic128_cmp_exchange(
					(rte_int128_t *)ring_ptr,
					(rte_int128_t *)&old_value,
					(rte_int128_t *)&new_value,
					1, __ATOMIC_RELEASE,
					__ATOMIC_RELAXED);
#else
			success = __atomic_compare_exchange(
					(uint64_t *)ring_ptr,
					&old_value,
					&new_value,
					1, __ATOMIC_RELEASE,
					__ATOMIC_RELAXED);
#endif
		} while (success == 0);

		/* Only increment tail if the CAS succeeds, since it can
		 * spuriously fail on some architectures.
		 */
		tail++;
	}

end:
	tail = __rte_ring_lf_update_tail(r, tail);

	if (free_space != NULL)
		*free_space = avail - n;
	return n;
#endif
}

/**
 * @internal
 *   Dequeue several objects from the lock-free ring (single-consumer only)
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to pull from the ring.
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Dequeue a fixed number of items from the ring
 *   RTE_RING_QUEUE_VARIABLE: Dequeue as many items as possible from the ring
 * @param available
 *   returns the number of remaining ring entries after the dequeue has finished
 * @return
 *   - Actual number of objects dequeued.
 *     If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_do_lf_dequeue_sc(struct rte_ring *r, void **obj_table,
			    unsigned int n,
			    enum rte_ring_queue_behavior behavior,
			    unsigned int *available)
{
	uintptr_t cons_tail, prod_tail, avail;

	cons_tail = __atomic_load_n(&r->cons_ptr.tail, __ATOMIC_RELAXED);
	prod_tail = __atomic_load_n(&r->prod_ptr.tail, __ATOMIC_ACQUIRE);

	avail = prod_tail - cons_tail;

	/* Set the actual entries for dequeue */
	if (unlikely(avail < n))
		n = (behavior == RTE_RING_QUEUE_FIXED) ? 0 : avail;

	if (unlikely(n == 0))
		goto end;

	DEQUEUE_PTRS_LF(r, &r->ring, cons_tail, obj_table, n);

	/* Use a read barrier and store-relaxed so we don't unnecessarily order
	 * writes.
	 */
	rte_smp_rmb();

	__atomic_store_n(&r->cons_ptr.tail, cons_tail + n, __ATOMIC_RELAXED);
end:
	if (available != NULL)
		*available = avail - n;

	return n;
}

/**
 * @internal
 *   Dequeue several objects from the lock-free ring (multi-consumer safe)
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to pull from the ring.
 * @param behavior
 *   RTE_RING_QUEUE_FIXED:    Dequeue a fixed number of items from the ring
 *   RTE_RING_QUEUE_VARIABLE: Dequeue as many items as possible from the ring
 * @param available
 *   returns the number of remaining ring entries after the dequeue has finished
 * @return
 *   - Actual number of objects dequeued.
 *     If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
 */
static __rte_always_inline unsigned int
__rte_ring_do_lf_dequeue_mc(struct rte_ring *r, void **obj_table,
			    unsigned int n,
			    enum rte_ring_queue_behavior behavior,
			    unsigned int *available)
{
	uintptr_t cons_tail, prod_tail, avail;

	cons_tail = __atomic_load_n(&r->cons_ptr.tail, __ATOMIC_RELAXED);

	do {
		/* Load tail on every iteration to avoid spurious queue empty
		 * situations.
		 */
		prod_tail = __atomic_load_n(&r->prod_ptr.tail,
					    __ATOMIC_ACQUIRE);

		avail = prod_tail - cons_tail;

		/* Set the actual entries for dequeue */
		if (unlikely(avail < n))
			n = (behavior == RTE_RING_QUEUE_FIXED) ? 0 : avail;

		if (unlikely(n == 0))
			goto end;

		DEQUEUE_PTRS_LF(r, &r->ring, cons_tail, obj_table, n);

		/* Use a read barrier and store-relaxed so we don't
		 * unnecessarily order writes.
		 */
		rte_smp_rmb();

	} while (!__atomic_compare_exchange_n(&r->cons_ptr.tail,
					      &cons_tail, cons_tail + n,
					      0, __ATOMIC_RELAXED,
					      __ATOMIC_RELAXED));

end:
	if (available != NULL)
		*available = avail - n;

	return n;
}

#endif /* _RTE_RING_C11_MEM_H_ */

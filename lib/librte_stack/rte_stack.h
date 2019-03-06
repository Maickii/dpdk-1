/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Intel Corporation
 */

/**
 * @file rte_stack.h
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * RTE Stack
 *
 * librte_stack provides an API for configuration and use of a bounded stack of
 * pointers. Push and pop operations are MT-safe, allowing concurrent access,
 * and the interface supports pushing and popping multiple pointers at a time.
 */

#ifndef _RTE_STACK_H_
#define _RTE_STACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <rte_errno.h>
#include <rte_memzone.h>
#include <rte_spinlock.h>

#define RTE_TAILQ_STACK_NAME "RTE_STACK"
#define RTE_STACK_MZ_PREFIX "STK_"
/** The maximum length of a stack name. */
#define RTE_STACK_NAMESIZE (RTE_MEMZONE_NAMESIZE - \
			   sizeof(RTE_STACK_MZ_PREFIX) + 1)

/* Structure containing the LIFO, its current length, and a lock for mutual
 * exclusion.
 */
struct rte_stack_std {
	rte_spinlock_t lock; /**< LIFO lock */
	uint32_t len; /**< LIFO len */
	void *objs[]; /**< LIFO pointer table */
};

/* The RTE stack structure contains the LIFO structure itself, plus metadata
 * such as its name and memzone pointer.
 */
struct rte_stack {
	/** Name of the stack. */
	char name[RTE_STACK_NAMESIZE] __rte_cache_aligned;
	/** Memzone containing the rte_stack structure. */
	const struct rte_memzone *memzone;
	uint32_t capacity; /**< Usable size of the stack. */
	uint32_t flags; /**< Flags supplied at creation. */
	struct rte_stack_std stack_std; /**< LIFO structure. */
} __rte_cache_aligned;

/**
 * @internal Push several objects on the stack (MT-safe).
 *
 * @param s
 *   A pointer to the stack structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to push on the stack from the obj_table.
 * @return
 *   Actual number of objects pushed (either 0 or *n*).
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_std_push(struct rte_stack *s, void * const *obj_table, unsigned int n)
{
	struct rte_stack_std *stack = &s->stack_std;
	unsigned int index;
	void **cache_objs;

	rte_spinlock_lock(&stack->lock);
	cache_objs = &stack->objs[stack->len];

	/* Is there sufficient space in the stack? */
	if ((stack->len + n) > s->capacity) {
		rte_spinlock_unlock(&stack->lock);
		return 0;
	}

	/* Add elements back into the cache */
	for (index = 0; index < n; ++index, obj_table++)
		cache_objs[index] = *obj_table;

	stack->len += n;

	rte_spinlock_unlock(&stack->lock);
	return n;
}

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Push several objects on the stack (MT-safe).
 *
 * @param s
 *   A pointer to the stack structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to push on the stack from the obj_table.
 * @return
 *   Actual number of objects pushed (either 0 or *n*).
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_push(struct rte_stack *s, void * const *obj_table, unsigned int n)
{
	return rte_stack_std_push(s, obj_table, n);
}

/**
 * @internal Pop several objects from the stack (MT-safe).
 *
 * @param s
 *   A pointer to the stack structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to pull from the stack.
 * @return
 *   Actual number of objects popped (either 0 or *n*).
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_std_pop(struct rte_stack *s, void **obj_table, unsigned int n)
{
	struct rte_stack_std *stack = &s->stack_std;
	unsigned int index, len;
	void **cache_objs;

	rte_spinlock_lock(&stack->lock);

	if (unlikely(n > stack->len)) {
		rte_spinlock_unlock(&stack->lock);
		return 0;
	}

	cache_objs = stack->objs;

	for (index = 0, len = stack->len - 1; index < n;
			++index, len--, obj_table++)
		*obj_table = cache_objs[len];

	stack->len -= n;
	rte_spinlock_unlock(&stack->lock);

	return n;
}

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Pop several objects from the stack (MT-safe).
 *
 * @param s
 *   A pointer to the stack structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to pull from the stack.
 * @return
 *   Actual number of objects popped (either 0 or *n*).
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_pop(struct rte_stack *s, void **obj_table, unsigned int n)
{
	if (unlikely(n == 0 || obj_table == NULL))
		return 0;

	return rte_stack_std_pop(s, obj_table, n);
}

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Return the number of used entries in a stack.
 *
 * @param s
 *   A pointer to the stack structure.
 * @return
 *   The number of used entries in the stack.
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_count(struct rte_stack *s)
{
	return (unsigned int)s->stack_std.len;
}

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Return the number of free entries in a stack.
 *
 * @param s
 *   A pointer to the stack structure.
 * @return
 *   The number of free entries in the stack.
 */
static __rte_always_inline unsigned int __rte_experimental
rte_stack_free_count(struct rte_stack *s)
{
	return s->capacity - rte_stack_count(s);
}

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Create a new stack named *name* in memory.
 *
 * This function uses ``memzone_reserve()`` to allocate memory for a stack of
 * size *count*. The behavior of the stack is controlled by the *flags*.
 *
 * @param name
 *   The name of the stack.
 * @param count
 *   The size of the stack.
 * @param socket_id
 *   The *socket_id* argument is the socket identifier in case of
 *   NUMA. The value can be *SOCKET_ID_ANY* if there is no NUMA
 *   constraint for the reserved zone.
 * @param flags
 *   Reserved for future use.
 * @return
 *   On success, the pointer to the new allocated stack. NULL on error with
 *    rte_errno set appropriately. Possible errno values include:
 *    - ENOSPC - the maximum number of memzones has already been allocated
 *    - EEXIST - a stack with the same name already exists
 *    - ENOMEM - insufficient memory to create the stack
 *    - ENAMETOOLONG - name size exceeds RTE_STACK_NAMESIZE
 */
struct rte_stack *__rte_experimental
rte_stack_create(const char *name, unsigned int count, int socket_id,
		 uint32_t flags);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Free all memory used by the stack.
 *
 * @param s
 *   Stack to free
 */
void __rte_experimental
rte_stack_free(struct rte_stack *s);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice
 *
 * Lookup a stack by its name.
 *
 * @param name
 *   The name of the stack.
 * @return
 *   The pointer to the stack matching the name, or NULL if not found,
 *   with rte_errno set appropriately. Possible rte_errno values include:
 *    - ENOENT - Stack with name *name* not found.
 *    - EINVAL - *name* pointer is NULL.
 */
struct rte_stack * __rte_experimental
rte_stack_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_STACK_H_ */

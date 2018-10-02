/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

#include <rte_errno.h>
#include <rte_memcpy.h>
#include <rte_memory.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_branch_prediction.h>
#include <rte_debug.h>
#include <rte_launch.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_common.h>
#include <rte_spinlock.h>

#include <rte_malloc.h>
#include "malloc_elem.h"
#include "malloc_heap.h"


/* Free the memory space back to heap */
void rte_free(void *addr)
{
	if (addr == NULL) return;
	if (malloc_heap_free(malloc_elem_from_data(addr)) < 0)
		RTE_LOG(ERR, EAL, "Error: Invalid memory\n");
}

/*
 * Allocate memory on specified heap.
 */
void *
rte_malloc_socket(const char *type, size_t size, unsigned int align,
		int socket_arg)
{
	/* return NULL if size is 0 or alignment is not power-of-2 */
	if (size == 0 || (align && !rte_is_power_of_2(align)))
		return NULL;

	if (!rte_eal_has_hugepages())
		socket_arg = SOCKET_ID_ANY;

	return malloc_heap_alloc(type, size, socket_arg, 0,
			align == 0 ? 1 : align, 0, false);
}

/*
 * Allocate memory on default heap.
 */
void *
rte_malloc(const char *type, size_t size, unsigned align)
{
	return rte_malloc_socket(type, size, align, SOCKET_ID_ANY);
}

/*
 * Allocate zero'd memory on specified heap.
 */
void *
rte_zmalloc_socket(const char *type, size_t size, unsigned align, int socket)
{
	return rte_malloc_socket(type, size, align, socket);
}

/*
 * Allocate zero'd memory on default heap.
 */
void *
rte_zmalloc(const char *type, size_t size, unsigned align)
{
	return rte_zmalloc_socket(type, size, align, SOCKET_ID_ANY);
}

/*
 * Allocate zero'd memory on specified heap.
 */
void *
rte_calloc_socket(const char *type, size_t num, size_t size, unsigned align, int socket)
{
	return rte_zmalloc_socket(type, num * size, align, socket);
}

/*
 * Allocate zero'd memory on default heap.
 */
void *
rte_calloc(const char *type, size_t num, size_t size, unsigned align)
{
	return rte_zmalloc(type, num * size, align);
}

/*
 * Resize allocated memory.
 */
void *
rte_realloc(void *ptr, size_t size, unsigned align)
{
	if (ptr == NULL)
		return rte_malloc(NULL, size, align);

	struct malloc_elem *elem = malloc_elem_from_data(ptr);
	if (elem == NULL) {
		RTE_LOG(ERR, EAL, "Error: memory corruption detected\n");
		return NULL;
	}

	size = RTE_CACHE_LINE_ROUNDUP(size), align = RTE_CACHE_LINE_ROUNDUP(align);
	/* check alignment matches first, and if ok, see if we can resize block */
	if (RTE_PTR_ALIGN(ptr,align) == ptr &&
			malloc_heap_resize(elem, size) == 0)
		return ptr;

	/* either alignment is off, or we have no room to expand,
	 * so move data. */
	void *new_ptr = rte_malloc(NULL, size, align);
	if (new_ptr == NULL)
		return NULL;
	const unsigned old_size = elem->size - MALLOC_ELEM_OVERHEAD;
	rte_memcpy(new_ptr, ptr, old_size < size ? old_size : size);
	rte_free(ptr);

	return new_ptr;
}

int
rte_malloc_validate(const void *ptr, size_t *size)
{
	const struct malloc_elem *elem = malloc_elem_from_data(ptr);
	if (!malloc_elem_cookies_ok(elem))
		return -1;
	if (size != NULL)
		*size = elem->size - elem->pad - MALLOC_ELEM_OVERHEAD;
	return 0;
}

/*
 * Function to retrieve data for heap on given socket
 */
int
rte_malloc_get_socket_stats(int socket,
		struct rte_malloc_socket_stats *socket_stats)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	int heap_idx, ret = -1;

	rte_rwlock_read_lock(&mcfg->memory_hotplug_lock);

	heap_idx = malloc_socket_to_heap_id(socket);
	if (heap_idx < 0)
		goto unlock;

	ret = malloc_heap_get_stats(&mcfg->malloc_heaps[heap_idx],
			socket_stats);
unlock:
	rte_rwlock_read_unlock(&mcfg->memory_hotplug_lock);

	return ret;
}

/*
 * Function to dump contents of all heaps
 */
void __rte_experimental
rte_malloc_dump_heaps(FILE *f)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	unsigned int idx;

	rte_rwlock_read_lock(&mcfg->memory_hotplug_lock);

	for (idx = 0; idx < RTE_MAX_HEAPS; idx++) {
		fprintf(f, "Heap id: %u\n", idx);
		malloc_heap_dump(&mcfg->malloc_heaps[idx], f);
	}

	rte_rwlock_read_unlock(&mcfg->memory_hotplug_lock);
}

int
rte_malloc_heap_get_socket(const char *name)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	struct malloc_heap *heap = NULL;
	unsigned int idx;
	int ret;

	if (name == NULL ||
			strnlen(name, RTE_HEAP_NAME_MAX_LEN) == 0 ||
			strnlen(name, RTE_HEAP_NAME_MAX_LEN) ==
				RTE_HEAP_NAME_MAX_LEN) {
		rte_errno = EINVAL;
		return -1;
	}
	rte_rwlock_read_lock(&mcfg->memory_hotplug_lock);
	for (idx = 0; idx < RTE_MAX_HEAPS; idx++) {
		struct malloc_heap *tmp = &mcfg->malloc_heaps[idx];

		if (!strncmp(name, tmp->name, RTE_HEAP_NAME_MAX_LEN)) {
			heap = tmp;
			break;
		}
	}

	if (heap != NULL) {
		ret = heap->socket_id;
	} else {
		rte_errno = ENOENT;
		ret = -1;
	}
	rte_rwlock_read_unlock(&mcfg->memory_hotplug_lock);

	return ret;
}

int
rte_malloc_heap_socket_is_external(int socket_id)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	unsigned int idx;
	int ret = -1;

	if (socket_id == SOCKET_ID_ANY)
		return 0;

	rte_rwlock_read_lock(&mcfg->memory_hotplug_lock);
	for (idx = 0; idx < RTE_MAX_HEAPS; idx++) {
		struct malloc_heap *tmp = &mcfg->malloc_heaps[idx];

		if ((int)tmp->socket_id == socket_id) {
			/* external memory always has large socket ID's */
			ret = tmp->socket_id >= RTE_MAX_NUMA_NODES;
			break;
		}
	}
	rte_rwlock_read_unlock(&mcfg->memory_hotplug_lock);

	return ret;
}

/*
 * Print stats on memory type. If type is NULL, info on all types is printed
 */
void
rte_malloc_dump_stats(FILE *f, __rte_unused const char *type)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	unsigned int heap_id;
	struct rte_malloc_socket_stats sock_stats;

	rte_rwlock_read_lock(&mcfg->memory_hotplug_lock);

	/* Iterate through all initialised heaps */
	for (heap_id = 0; heap_id < RTE_MAX_HEAPS; heap_id++) {
		struct malloc_heap *heap = &mcfg->malloc_heaps[heap_id];

		malloc_heap_get_stats(heap, &sock_stats);

		fprintf(f, "Heap id:%u\n", heap_id);
		fprintf(f, "\tHeap name:%s\n", heap->name);
		fprintf(f, "\tHeap_size:%zu,\n", sock_stats.heap_totalsz_bytes);
		fprintf(f, "\tFree_size:%zu,\n", sock_stats.heap_freesz_bytes);
		fprintf(f, "\tAlloc_size:%zu,\n", sock_stats.heap_allocsz_bytes);
		fprintf(f, "\tGreatest_free_size:%zu,\n",
				sock_stats.greatest_free_size);
		fprintf(f, "\tAlloc_count:%u,\n",sock_stats.alloc_count);
		fprintf(f, "\tFree_count:%u,\n", sock_stats.free_count);
	}
	rte_rwlock_read_unlock(&mcfg->memory_hotplug_lock);
	return;
}

/*
 * TODO: Set limit to memory that can be allocated to memory type
 */
int
rte_malloc_set_limit(__rte_unused const char *type,
		__rte_unused size_t max)
{
	return 0;
}

/*
 * Return the IO address of a virtual address obtained through rte_malloc
 */
rte_iova_t
rte_malloc_virt2iova(const void *addr)
{
	const struct rte_memseg *ms;
	struct malloc_elem *elem = malloc_elem_from_data(addr);

	if (elem == NULL)
		return RTE_BAD_IOVA;

	if (!elem->msl->external && rte_eal_iova_mode() == RTE_IOVA_VA)
		return (uintptr_t) addr;

	ms = rte_mem_virt2memseg(addr, elem->msl);
	if (ms == NULL)
		return RTE_BAD_IOVA;

	if (ms->iova == RTE_BAD_IOVA)
		return RTE_BAD_IOVA;

	return ms->iova + RTE_PTR_DIFF(addr, ms->addr);
}

static struct malloc_heap *
find_named_heap(const char *name)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	unsigned int i;

	for (i = 0; i < RTE_MAX_HEAPS; i++) {
		struct malloc_heap *heap = &mcfg->malloc_heaps[i];

		if (!strncmp(name, heap->name, RTE_HEAP_NAME_MAX_LEN))
			return heap;
	}
	return NULL;
}

int
rte_malloc_heap_create(const char *heap_name)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	struct malloc_heap *heap = NULL;
	int i, ret;

	if (heap_name == NULL ||
			strnlen(heap_name, RTE_HEAP_NAME_MAX_LEN) == 0 ||
			strnlen(heap_name, RTE_HEAP_NAME_MAX_LEN) ==
				RTE_HEAP_NAME_MAX_LEN) {
		rte_errno = EINVAL;
		return -1;
	}
	/* check if there is space in the heap list, or if heap with this name
	 * already exists.
	 */
	rte_rwlock_write_lock(&mcfg->memory_hotplug_lock);

	for (i = 0; i < RTE_MAX_HEAPS; i++) {
		struct malloc_heap *tmp = &mcfg->malloc_heaps[i];
		/* existing heap */
		if (strncmp(heap_name, tmp->name,
				RTE_HEAP_NAME_MAX_LEN) == 0) {
			RTE_LOG(ERR, EAL, "Heap %s already exists\n",
				heap_name);
			rte_errno = EEXIST;
			ret = -1;
			goto unlock;
		}
		/* empty heap */
		if (strnlen(tmp->name, RTE_HEAP_NAME_MAX_LEN) == 0) {
			heap = tmp;
			break;
		}
	}
	if (heap == NULL) {
		RTE_LOG(ERR, EAL, "Cannot create new heap: no space\n");
		rte_errno = ENOSPC;
		ret = -1;
		goto unlock;
	}

	/* we're sure that we can create a new heap, so do it */
	ret = malloc_heap_create(heap, heap_name);
unlock:
	rte_rwlock_write_unlock(&mcfg->memory_hotplug_lock);

	return ret;
}

int
rte_malloc_heap_destroy(const char *heap_name)
{
	struct rte_mem_config *mcfg = rte_eal_get_configuration()->mem_config;
	struct malloc_heap *heap = NULL;
	int ret;

	if (heap_name == NULL ||
			strnlen(heap_name, RTE_HEAP_NAME_MAX_LEN) == 0 ||
			strnlen(heap_name, RTE_HEAP_NAME_MAX_LEN) ==
				RTE_HEAP_NAME_MAX_LEN) {
		rte_errno = EINVAL;
		return -1;
	}
	rte_rwlock_write_lock(&mcfg->memory_hotplug_lock);

	/* start from non-socket heaps */
	heap = find_named_heap(heap_name);
	if (heap == NULL) {
		RTE_LOG(ERR, EAL, "Heap %s not found\n", heap_name);
		rte_errno = ENOENT;
		ret = -1;
		goto unlock;
	}
	/* we shouldn't be able to destroy internal heaps */
	if (heap->socket_id < RTE_MAX_NUMA_NODES) {
		rte_errno = EPERM;
		ret = -1;
		goto unlock;
	}
	/* sanity checks done, now we can destroy the heap */
	rte_spinlock_lock(&heap->lock);
	ret = malloc_heap_destroy(heap);

	/* if we failed, lock is still active */
	if (ret < 0)
		rte_spinlock_unlock(&heap->lock);
unlock:
	rte_rwlock_write_unlock(&mcfg->memory_hotplug_lock);

	return ret;
}

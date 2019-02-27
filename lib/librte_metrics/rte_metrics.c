/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 */

#include <string.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_metrics.h>
#include <rte_lcore.h>
#include <rte_memzone.h>
#include <rte_spinlock.h>
#include <rte_bitmap.h>

#define RTE_METRICS_MAX_METRICS 256
#define RTE_METRICS_MEMZONE_NAME "RTE_METRICS"

/**
 * Internal stats metadata and value entry.
 *
 * @internal
 */
struct rte_metrics_meta_s {
	/** Name of metric */
	char name[RTE_METRICS_MAX_NAME_LEN];
	/** Current value for metric */
	uint64_t value[RTE_MAX_ETHPORTS];
	/** Used for global metrics */
	uint64_t global_value;

};

/**
 * Internal stats info structure.
 *
 * @internal
 * Offsets into metadata are used instead of pointers because ASLR
 * means that having the same physical addresses in different
 * processes is not guaranteed.
 */
struct rte_metrics_data_s {
	/**   Number of metrics. */
	uint16_t cnt_stats;
	/** Metric data memory block. */
	struct rte_metrics_meta_s metadata[RTE_METRICS_MAX_METRICS];
	/** Metric data bitmap in use */
	struct rte_bitmap *bits;
	/** Metric data access lock */
	rte_spinlock_t lock;
};

void
rte_metrics_init(int socket_id)
{
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint32_t bmp_size;
	void *bmp_mem;

	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone != NULL)
		return;
	memzone = rte_memzone_reserve(RTE_METRICS_MEMZONE_NAME,
		sizeof(struct rte_metrics_data_s), socket_id, 0);
	if (memzone == NULL)
		rte_exit(EXIT_FAILURE, "Unable to allocate stats memzone\n");
	stats = memzone->addr;
	memset(stats, 0, sizeof(struct rte_metrics_data_s));

	bmp_size =
		rte_bitmap_get_memory_footprint(RTE_METRICS_MAX_METRICS);
	bmp_mem = rte_malloc("metrics_bits", bmp_size,
						RTE_CACHE_LINE_SIZE);
	if (bmp_mem == NULL)
		rte_exit(EXIT_FAILURE, "Failed to allocate metrics bitmap\n");

	stats->bits = rte_bitmap_init(RTE_METRICS_MAX_METRICS,
			bmp_mem, bmp_size);
	if (stats->bits == NULL) {
		rte_exit(EXIT_FAILURE, "Failed to init metrics bitmap\n");
		rte_free(bmp_mem);
	}

	rte_spinlock_init(&stats->lock);
}

int
rte_metrics_reg_name(const char *name)
{
	const char * const list_names[] = {name};

	return rte_metrics_reg_names(list_names, 1);
}

int
rte_metrics_reg_names(const char * const *names, uint16_t cnt_names)
{
	struct rte_metrics_meta_s *entry = NULL;
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint16_t idx_name, idx;
	uint16_t idx_base = RTE_METRICS_MAX_METRICS;

	/* Some sanity checks */
	if (cnt_names < 1 || names == NULL)
		return -EINVAL;
	for (idx_name = 0; idx_name < cnt_names; idx_name++)
		if (names[idx_name] == NULL)
			return -EINVAL;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone == NULL)
		return -EIO;
	stats = memzone->addr;

	if (stats->cnt_stats + cnt_names >= RTE_METRICS_MAX_METRICS)
		return -ENOMEM;

	rte_spinlock_lock(&stats->lock);

	/* search for a continuous array, fail if not enough*/
	for (idx_name = 0; idx_name < RTE_METRICS_MAX_METRICS; idx_name++) {
		if (!rte_bitmap_get(stats->bits, idx_name)) {
			idx_base = idx_name;
			if (idx_base + cnt_names > RTE_METRICS_MAX_METRICS)
				return -ENOMEM;
			for (idx = idx_base;
				idx < idx_base + cnt_names; idx++) {
				if (rte_bitmap_get(stats->bits, idx))
					break;
			}
			if (idx == idx_base + cnt_names)
				break;
			idx_name = idx;
		}
	}
	if (idx_base == RTE_METRICS_MAX_METRICS)
		return -ENOMEM;

	for (idx = idx_base; idx < idx_base + cnt_names; idx++) {
		rte_bitmap_set(stats->bits, idx);
		entry = &stats->metadata[idx];
		strlcpy(entry->name, names[idx-idx_base],
			RTE_METRICS_MAX_NAME_LEN);
		memset(entry->value, 0, sizeof(entry->value));
		entry->global_value = 0;
	}
	stats->cnt_stats += cnt_names;

	rte_spinlock_unlock(&stats->lock);

	return idx_base;
}

int
rte_metrics_update_value(int port_id, uint16_t key, const uint64_t value)
{
	return rte_metrics_update_values(port_id, key, &value, 1);
}

int
rte_metrics_update_values(int port_id,
	uint16_t key,
	const uint64_t *values,
	uint32_t count)
{
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint16_t idx_metric;
	uint16_t idx_value;
	uint16_t cnt_setsize;

	if (port_id != RTE_METRICS_GLOBAL &&
			(port_id < 0 || port_id >= RTE_MAX_ETHPORTS))
		return -EINVAL;

	if (values == NULL)
		return -EINVAL;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone == NULL)
		return -EIO;
	stats = memzone->addr;

	rte_spinlock_lock(&stats->lock);

	idx_metric = key;
	cnt_setsize = 0;
	while (idx_metric < RTE_METRICS_MAX_METRICS) {
		if (rte_bitmap_get(stats->bits, idx_metric)) {
			cnt_setsize++;
			idx_metric++;
		} else
			break;
	}
	/* Check update does not cross set border */
	if (cnt_setsize == 0 || count > cnt_setsize) {
		rte_spinlock_unlock(&stats->lock);
		return -EINVAL;
	}

	for (idx_value = 0; idx_value < count; idx_value++) {
		idx_metric = key + idx_value;
		if (port_id == RTE_METRICS_GLOBAL)
			stats->metadata[idx_metric].global_value =
				values[idx_value];
		else
			stats->metadata[idx_metric].value[port_id] =
				values[idx_value];
	}

	rte_spinlock_unlock(&stats->lock);
	return 0;
}

int
rte_metrics_unreg_values(uint16_t key, uint16_t count)
{
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint16_t idx_metric;
	uint16_t idx_value;
	uint16_t cnt_setsize;

	/* Some sanity checks */
	if (count < 1)
		return -EINVAL;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone == NULL)
		return -EIO;

	stats = memzone->addr;
	if (stats->cnt_stats < count)
		return -EINVAL;

	if (key >= RTE_METRICS_MAX_METRICS)
		return -EINVAL;

	rte_spinlock_lock(&stats->lock);

	idx_metric = key;
	cnt_setsize = 1;
	while (idx_metric < RTE_METRICS_MAX_METRICS) {
		if (rte_bitmap_get(stats->bits, idx_metric)) {
			cnt_setsize++;
			idx_metric++;
		} else
			break;
	}
	/* Check update does not cross set border */
	if (count > cnt_setsize) {
		rte_spinlock_unlock(&stats->lock);
		return -ERANGE;
	}

	for (idx_value = 0; idx_value < count; idx_value++) {
		idx_metric = key + idx_value;
		memset(stats->metadata[idx_metric].name, 0,
			RTE_METRICS_MAX_NAME_LEN);
		rte_bitmap_clear(stats->bits, idx_metric);
	}
	stats->cnt_stats -= count;
	rte_spinlock_unlock(&stats->lock);

	return 0;
}


int
rte_metrics_get_names(struct rte_metric_name *names,
	uint16_t capacity)
{
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint16_t idx_name, idx = 0;
	int return_value;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone == NULL)
		return -EIO;

	stats = memzone->addr;
	rte_spinlock_lock(&stats->lock);

	return_value = stats->cnt_stats;
	if (names == NULL || capacity < stats->cnt_stats) {
		rte_spinlock_unlock(&stats->lock);
		return return_value;
	}

	for (idx_name = 0; idx < stats->cnt_stats &&
		idx_name < RTE_METRICS_MAX_METRICS; idx_name++) {
		if (rte_bitmap_get(stats->bits, idx_name)) {
			strlcpy(names[idx].name,
				stats->metadata[idx_name].name,
				RTE_METRICS_MAX_NAME_LEN);
			idx++;
		}
	}

	rte_spinlock_unlock(&stats->lock);
	return return_value;
}

int
rte_metrics_get_values(int port_id,
	struct rte_metric_value *values,
	uint16_t capacity)
{
	struct rte_metrics_meta_s *entry;
	struct rte_metrics_data_s *stats;
	const struct rte_memzone *memzone;
	uint16_t idx_name, idx = 0;
	int return_value;

	if (port_id != RTE_METRICS_GLOBAL &&
			(port_id < 0 || port_id >= RTE_MAX_ETHPORTS))
		return -EINVAL;

	memzone = rte_memzone_lookup(RTE_METRICS_MEMZONE_NAME);
	if (memzone == NULL)
		return -EIO;

	stats = memzone->addr;
	rte_spinlock_lock(&stats->lock);

	return_value = stats->cnt_stats;

	if (values == NULL || capacity < stats->cnt_stats) {
		rte_spinlock_unlock(&stats->lock);
		return return_value;
	}

	for (idx_name = 0; idx < stats->cnt_stats &&
			idx_name < RTE_METRICS_MAX_METRICS;
			idx_name++) {
		if (rte_bitmap_get(stats->bits, idx_name)) {
			entry = &stats->metadata[idx_name];
			values[idx].key = idx_name;
			if (port_id == RTE_METRICS_GLOBAL)
				values[idx].value = entry->global_value;
			else
				values[idx].value = entry->value[port_id];
			idx++;
		}
	}

	rte_spinlock_unlock(&stats->lock);

	return return_value;
}

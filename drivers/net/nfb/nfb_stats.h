/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Cesnet
 * Copyright(c) 2018 Netcope Technologies, a.s. <info@netcope.com>
 * All rights reserved.
 */

#ifndef _NFB_STATS_H_
#define _NFB_STATS_H_

#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>
#include <sys/types.h>

#include <sys/mman.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_dev.h>

/**
 * DPDK callback to get device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] stats
 *   Stats structure output buffer.
 *
 * @return
 *   0 on success and stats is filled, negative errno value otherwise and
 *   rte_errno is set.
 */
int nfb_eth_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats);

/**
 * DPDK callback to clear device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void nfb_eth_stats_reset(struct rte_eth_dev *dev);




#endif /* _NFB_STATS_H_ */

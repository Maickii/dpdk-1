/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Cesnet
 * Copyright(c) 2018 Netcope Technologies, a.s. <info@netcope.com>
 * All rights reserved.
 */



#ifndef _NFB_RX_H_
#define _NFB_RX_H_

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

struct ndp_rx_queue {
	struct nfb_device *nfb;	     /* nfb dev structure */
	struct ndp_queue *queue;     /* rx queue */
	uint16_t rx_queue_id;	     /* index */
	uint8_t in_port;	     /* port */

	struct rte_mempool *mb_pool; /* memory pool to allocate packets */
	uint16_t buf_size;           /* mbuf size */

	volatile uint64_t rx_pkts;   /* packets read */
	volatile uint64_t rx_bytes;  /* bytes read */
	volatile uint64_t err_pkts;  /* erroneous packets */
};

/**
 * Initialize ndp_rx_queue structure
 *
 * @param nfb
 *   Pointer to nfb device structure.
 * @param rx_queue_id
 *   RX queue index.
 * @param port_id
 *   Device [external] port identifier.
 * @param mb_pool
 *   Memory pool for buffer allocations.
 * @param[out] rxq
 *   Pointer to ndp_rx_queue output structure
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
nfb_eth_rx_queue_init(struct nfb_device *nfb,
	uint16_t rx_queue_id,
	uint16_t port_id,
	struct rte_mempool *mb_pool,
	struct ndp_rx_queue *rxq);


/**
 * DPDK callback to setup a RX queue for use.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   RX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *   Thresholds parameters.
 * @param mb_pool
 *   Memory pool for buffer allocations.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
nfb_eth_rx_queue_setup(struct rte_eth_dev *dev,
	uint16_t rx_queue_id,
	uint16_t nb_rx_desc __rte_unused,
	unsigned int socket_id,
	const struct rte_eth_rxconf *rx_conf __rte_unused,
	struct rte_mempool *mb_pool);


/**
 * DPDK callback to release a RX queue.
 *
 * @param dpdk_rxq
 *   Generic RX queue pointer.
 */
void
nfb_eth_rx_queue_release(void *q);

/**
 * Start traffic on Rx queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param txq_id
 *   RX queue index.
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
nfb_eth_rx_queue_start(struct rte_eth_dev *dev, uint16_t rxq_id);

/**
 * Stop traffic on Rx queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param txq_id
 *   RX queue index.
 */
int
nfb_eth_rx_queue_stop(struct rte_eth_dev *dev, uint16_t rxq_id);

/**
 * DPDK callback for RX.
 *
 * @param dpdk_rxq
 *   Generic pointer to RX queue structure.
 * @param[out] bufs
 *   Array to store received packets.
 * @param nb_pkts
 *   Maximum number of packets in array.
 *
 * @return
 *   Number of packets successfully received (<= nb_pkts).
 */
static __rte_always_inline uint16_t
nfb_eth_ndp_rx(void *queue,
	struct rte_mbuf **bufs,
	uint16_t nb_pkts)
{
	struct ndp_rx_queue *ndp = queue;

	if (unlikely(ndp->queue == NULL || nb_pkts == 0)) {
		RTE_LOG(ERR, PMD, "RX invalid arguments!\n");
		return 0;
	}

	struct rte_mbuf *mbufs[nb_pkts];

	unsigned int i;
	// returns either all or nothing
	i = rte_pktmbuf_alloc_bulk(ndp->mb_pool, mbufs, nb_pkts);
	if (unlikely(i != 0))
		return 0;

	uint16_t packet_size;
	uint64_t num_bytes = 0;
	const uint16_t buf_size = ndp->buf_size;

	struct rte_mbuf *mbuf;
	struct ndp_packet packets[nb_pkts];


	uint16_t num_rx = ndp_rx_burst_get(ndp->queue, packets, nb_pkts);

	if (unlikely(num_rx != nb_pkts)) {
		for (i = num_rx; i < nb_pkts; i++)
			rte_pktmbuf_free(mbufs[i]);
	}

	nb_pkts = num_rx;

	num_rx = 0;
	/*
	 * Reads the given number of packets from NDP queue given
	 * by queue and copies the packet data into a newly allocated mbuf
	 * to return.
	 */
	for (i = 0; i < nb_pkts; ++i) {
		mbuf = mbufs[i];

		/* get the space available for data in the mbuf */
		packet_size = packets[i].data_length;

		if (likely(packet_size <= buf_size)) {
			/* NDP packet will fit in one mbuf, go ahead and copy */
			rte_memcpy(rte_pktmbuf_mtod(mbuf, void *),
				packets[i].data, packet_size);

			mbuf->data_len = (uint16_t)packet_size;

			mbuf->pkt_len = packet_size;
			mbuf->port = ndp->in_port;
			bufs[num_rx++] = mbuf;
			num_bytes += packet_size;
		} else {
			/*
			 * NDP packet will not fit in one mbuf,
			 * scattered mode is not enabled, drop packet
			 */
			RTE_LOG(ERR, PMD,
				"NDP segment %d bytes will not fit in one mbuf "
				"(%d bytes), scattered mode is not enabled, "
				"drop packet!!\n",
				packet_size, buf_size);
			rte_pktmbuf_free(mbuf);
		}
	}


	ndp_rx_burst_put(ndp->queue);

	ndp->rx_pkts += num_rx;
	ndp->rx_bytes += num_bytes;
	return num_rx;
}



#endif /* _NFB_RX_H_ */

/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Intel Corporation.
 */

#include <rte_mbuf.h>
#include <rte_ethdev_driver.h>
#include <rte_ethdev_vdev.h>
#include <rte_malloc.h>
#include <rte_kvargs.h>
#include <rte_bus_vdev.h>

#include <linux/if_ether.h>
#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <asm/barrier.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <bpf/bpf.h>
#include <xsk.h>

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef PF_XDP
#define PF_XDP AF_XDP
#endif

#define ETH_AF_XDP_IFACE_ARG			"iface"
#define ETH_AF_XDP_QUEUE_IDX_ARG		"queue"

#define ETH_AF_XDP_FRAME_SIZE		XSK_UMEM__DEFAULT_FRAME_SIZE
#define ETH_AF_XDP_NUM_BUFFERS		4096
/* mempool hdrobj size (64 bytes) + sizeof(struct rte_mbuf) (128 bytes) */
#define ETH_AF_XDP_MBUF_OVERHEAD	192
/* data start from offset 320 (192 + 128) bytes */
#define ETH_AF_XDP_DATA_HEADROOM				\
	(ETH_AF_XDP_MBUF_OVERHEAD + RTE_PKTMBUF_HEADROOM)
#define ETH_AF_XDP_DFLT_NUM_DESCS	XSK_RING_CONS__DEFAULT_NUM_DESCS
#define ETH_AF_XDP_DFLT_QUEUE_IDX	0

#define ETH_AF_XDP_RX_BATCH_SIZE	32
#define ETH_AF_XDP_TX_BATCH_SIZE	32

#define ETH_AF_XDP_MAX_QUEUE_PAIRS     16

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	struct rte_mempool *mb_pool;
	void *buffer;
};

struct pkt_rx_queue {
	struct xsk_ring_cons rx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	struct rte_mempool *mb_pool;

	unsigned long rx_pkts;
	unsigned long rx_bytes;
	unsigned long rx_dropped;

	struct pkt_tx_queue *pair;
	uint16_t queue_idx;
};

struct pkt_tx_queue {
	struct xsk_ring_prod tx;

	unsigned long tx_pkts;
	unsigned long err_pkts;
	unsigned long tx_bytes;

	struct pkt_rx_queue *pair;
	uint16_t queue_idx;
};

struct pmd_internals {
	int if_index;
	char if_name[IFNAMSIZ];
	uint16_t queue_idx;
	struct ether_addr eth_addr;
	struct xsk_umem_info *umem;
	struct rte_mempool *mb_pool_share;

	struct pkt_rx_queue rx_queues[ETH_AF_XDP_MAX_QUEUE_PAIRS];
	struct pkt_tx_queue tx_queues[ETH_AF_XDP_MAX_QUEUE_PAIRS];
};

static const char * const valid_arguments[] = {
	ETH_AF_XDP_IFACE_ARG,
	ETH_AF_XDP_QUEUE_IDX_ARG,
	NULL
};

static struct rte_eth_link pmd_link = {
	.link_speed = ETH_SPEED_NUM_10G,
	.link_duplex = ETH_LINK_FULL_DUPLEX,
	.link_status = ETH_LINK_DOWN,
	.link_autoneg = ETH_LINK_AUTONEG
};

static inline struct rte_mbuf *
addr_to_mbuf(struct xsk_umem_info *umem, uint64_t addr)
{
	uint64_t offset = (addr / ETH_AF_XDP_FRAME_SIZE *
			ETH_AF_XDP_FRAME_SIZE);
	struct rte_mbuf *mbuf = (struct rte_mbuf *)((uint64_t)umem->buffer +
				    offset + ETH_AF_XDP_MBUF_OVERHEAD -
				    sizeof(struct rte_mbuf));
	mbuf->data_off = addr - offset - ETH_AF_XDP_MBUF_OVERHEAD;
	return mbuf;
}

static inline uint64_t
mbuf_to_addr(struct xsk_umem_info *umem, struct rte_mbuf *mbuf)
{
	return (uint64_t)mbuf->buf_addr + mbuf->data_off -
		(uint64_t)umem->buffer;
}

static inline int
reserve_fill_queue(struct xsk_umem_info *umem, int reserve_size)
{
	struct xsk_ring_prod *fq = &umem->fq;
	struct rte_mbuf *mbuf;
	uint32_t idx;
	uint64_t addr;
	int i, ret = 0;

	ret = xsk_ring_prod__reserve(fq, reserve_size, &idx);
	if (!ret) {
		RTE_LOG(ERR, PMD, "Failed to reserve enough fq descs.\n");
		return ret;
	}

	for (i = 0; i < reserve_size; i++) {
		mbuf = rte_pktmbuf_alloc(umem->mb_pool);
		addr = mbuf_to_addr(umem, mbuf);
		*xsk_ring_prod__fill_addr(fq, idx++) = addr;
	}

	xsk_ring_prod__submit(fq, reserve_size);

	return 0;
}

static uint16_t
eth_af_xdp_rx(void *queue, struct rte_mbuf **bufs, uint16_t nb_pkts)
{
	struct pkt_rx_queue *rxq = queue;
	struct xsk_ring_cons *rx = &rxq->rx;
	struct xsk_umem_info *umem = rxq->umem;
	struct xsk_ring_prod *fq = &umem->fq;
	uint32_t idx_rx;
	uint32_t free_thresh = fq->size >> 1;
	struct rte_mbuf *mbuf;
	unsigned long dropped = 0;
	unsigned long rx_bytes = 0;
	uint16_t count = 0;
	int rcvd, i;

	nb_pkts = nb_pkts < ETH_AF_XDP_RX_BATCH_SIZE ?
		nb_pkts : ETH_AF_XDP_RX_BATCH_SIZE;

	rcvd = xsk_ring_cons__peek(rx, nb_pkts, &idx_rx);
	if (!rcvd)
		return 0;

	if (xsk_prod_nb_free(fq, free_thresh) >= free_thresh)
		(void)reserve_fill_queue(umem, ETH_AF_XDP_RX_BATCH_SIZE);

	for (i = 0; i < rcvd; i++) {
		uint64_t addr = xsk_ring_cons__rx_desc(rx, idx_rx)->addr;
		uint32_t len = xsk_ring_cons__rx_desc(rx, idx_rx++)->len;
		char *pkt = xsk_umem__get_data(rxq->umem->buffer, addr);

		mbuf = rte_pktmbuf_alloc(rxq->mb_pool);
		if (mbuf) {
			memcpy(rte_pktmbuf_mtod(mbuf, void*), pkt, len);
			rte_pktmbuf_pkt_len(mbuf) =
				rte_pktmbuf_data_len(mbuf) = len;
			rx_bytes += len;
			bufs[count++] = mbuf;
		} else {
			dropped++;
		}
		rte_pktmbuf_free(addr_to_mbuf(umem, addr));
	}

	xsk_ring_cons__release(rx, rcvd);

	/* statistics */
	rxq->rx_pkts += (rcvd - dropped);
	rxq->rx_bytes += rx_bytes;
	rxq->rx_dropped += dropped;

	return count;
}

static void pull_umem_cq(struct xsk_umem_info *umem, int size)
{
	struct xsk_ring_cons *cq = &umem->cq;
	int i, n;
	uint32_t idx_cq;
	uint64_t addr;

	n = xsk_ring_cons__peek(cq, size, &idx_cq);
	if (n > 0) {
		for (i = 0; i < n; i++) {
			addr = *xsk_ring_cons__comp_addr(cq, idx_cq++);
			rte_pktmbuf_free(addr_to_mbuf(umem, addr));
		}

		xsk_ring_cons__release(cq, n);
	}
}

static void kick_tx(struct pkt_tx_queue *txq)
{
	struct xsk_umem_info *umem = txq->pair->umem;
	int ret;

	while (1) {
		ret = sendto(xsk_socket__fd(txq->pair->xsk), NULL, 0,
			     MSG_DONTWAIT, NULL, 0);

		/* everything is ok */
		if (ret >= 0)
			break;

		/* some thing unexpected */
		if (errno != EBUSY && errno != EAGAIN)
			break;

		/* pull from complete qeueu to leave more space */
		if (errno == EAGAIN)
			pull_umem_cq(umem, ETH_AF_XDP_TX_BATCH_SIZE);
	}
}

static uint16_t
eth_af_xdp_tx(void *queue, struct rte_mbuf **bufs, uint16_t nb_pkts)
{
	struct pkt_tx_queue *txq = queue;
	struct xsk_umem_info *umem = txq->pair->umem;
	struct rte_mbuf *mbuf;
	struct rte_mbuf *mbuf_to_tx;
	unsigned long tx_bytes = 0;
	int i, valid = 0;
	uint32_t idx_tx;

	nb_pkts = nb_pkts < ETH_AF_XDP_TX_BATCH_SIZE ?
		nb_pkts : ETH_AF_XDP_TX_BATCH_SIZE;

	pull_umem_cq(umem, nb_pkts);

	if (xsk_ring_prod__reserve(&txq->tx, nb_pkts, &idx_tx)
			!= nb_pkts)
		return 0;

	for (i = 0; i < nb_pkts; i++) {
		struct xdp_desc *desc;
		char *pkt;
		unsigned int buf_len = ETH_AF_XDP_FRAME_SIZE
					- ETH_AF_XDP_DATA_HEADROOM;
		desc = xsk_ring_prod__tx_desc(&txq->tx, idx_tx + i);
		mbuf = bufs[i];
		if (mbuf->pkt_len <= buf_len) {
			mbuf_to_tx = rte_pktmbuf_alloc(umem->mb_pool);
			if (!mbuf_to_tx) {
				rte_pktmbuf_free(mbuf);
				continue;
			}
			desc->addr = mbuf_to_addr(umem, mbuf_to_tx);
			desc->len = mbuf->pkt_len;
			pkt = xsk_umem__get_data(umem->buffer,
						 desc->addr);
			memcpy(pkt, rte_pktmbuf_mtod(mbuf, void *),
			       desc->len);
			valid++;
			tx_bytes += mbuf->pkt_len;
		}
		rte_pktmbuf_free(mbuf);
	}

	xsk_ring_prod__submit(&txq->tx, nb_pkts);

	kick_tx(txq);

	txq->err_pkts += nb_pkts - valid;
	txq->tx_pkts += valid;
	txq->tx_bytes += tx_bytes;

	return nb_pkts;
}

static int
eth_dev_start(struct rte_eth_dev *dev)
{
	dev->data->dev_link.link_status = ETH_LINK_UP;

	return 0;
}

/* This function gets called when the current port gets stopped. */
static void
eth_dev_stop(struct rte_eth_dev *dev)
{
	dev->data->dev_link.link_status = ETH_LINK_DOWN;
}

static int
eth_dev_configure(struct rte_eth_dev *dev __rte_unused)
{
	/* rx/tx must be paired */
	if (dev->data->nb_rx_queues != dev->data->nb_tx_queues)
		return -EINVAL;

	return 0;
}

static void
eth_dev_info(struct rte_eth_dev *dev, struct rte_eth_dev_info *dev_info)
{
	struct pmd_internals *internals = dev->data->dev_private;

	dev_info->if_index = internals->if_index;
	dev_info->max_mac_addrs = 1;
	dev_info->max_rx_pktlen = (uint32_t)ETH_FRAME_LEN;
	dev_info->max_rx_queues = 1;
	dev_info->max_tx_queues = 1;
	dev_info->min_rx_bufsize = 0;

	dev_info->default_rxportconf.nb_queues = 1;
	dev_info->default_txportconf.nb_queues = 1;
	dev_info->default_rxportconf.ring_size = ETH_AF_XDP_DFLT_NUM_DESCS;
	dev_info->default_txportconf.ring_size = ETH_AF_XDP_DFLT_NUM_DESCS;
}

static int
eth_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct xdp_statistics xdp_stats;
	struct pkt_rx_queue *rxq;
	socklen_t optlen;
	int i;

	optlen = sizeof(struct xdp_statistics);
	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		rxq = &internals->rx_queues[i];
		stats->q_ipackets[i] = internals->rx_queues[i].rx_pkts;
		stats->q_ibytes[i] = internals->rx_queues[i].rx_bytes;

		stats->q_opackets[i] = internals->tx_queues[i].tx_pkts;
		stats->q_errors[i] = internals->tx_queues[i].err_pkts;
		stats->q_obytes[i] = internals->tx_queues[i].tx_bytes;

		stats->ipackets += stats->q_ipackets[i];
		stats->ibytes += stats->q_ibytes[i];
		stats->imissed += internals->rx_queues[i].rx_dropped;
		getsockopt(xsk_socket__fd(rxq->xsk), SOL_XDP, XDP_STATISTICS,
				&xdp_stats, &optlen);
		stats->imissed += xdp_stats.rx_dropped;

		stats->opackets += stats->q_opackets[i];
		stats->oerrors += stats->q_errors[i];
		stats->obytes += stats->q_obytes[i];
	}

	return 0;
}

static void
eth_stats_reset(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;
	int i;

	for (i = 0; i < ETH_AF_XDP_MAX_QUEUE_PAIRS; i++) {
		internals->rx_queues[i].rx_pkts = 0;
		internals->rx_queues[i].rx_bytes = 0;
		internals->rx_queues[i].rx_dropped = 0;

		internals->tx_queues[i].tx_pkts = 0;
		internals->tx_queues[i].err_pkts = 0;
		internals->tx_queues[i].tx_bytes = 0;
	}
}

static void remove_xdp_program(struct pmd_internals *internals)
{
	uint32_t curr_prog_id = 0;

	if (bpf_get_link_xdp_id(internals->if_index, &curr_prog_id,
				XDP_FLAGS_UPDATE_IF_NOEXIST)) {
		RTE_LOG(ERR, PMD, "bpf_get_link_xdp_id failed\n");
		return;
	}
	bpf_set_link_xdp_fd(internals->if_index, -1,
			XDP_FLAGS_UPDATE_IF_NOEXIST);
}

static void
eth_dev_close(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct pkt_rx_queue *rxq;
	int i;

	RTE_LOG(INFO, PMD, "Closing AF_XDP ethdev on numa socket %u\n",
		rte_socket_id());

	for (i = 0; i < ETH_AF_XDP_MAX_QUEUE_PAIRS; i++) {
		rxq = &internals->rx_queues[i];
		if (!rxq->umem)
			break;
		xsk_socket__delete(rxq->xsk);
	}

	(void)xsk_umem__delete(internals->umem->umem);
	remove_xdp_program(internals);
}

static void
eth_queue_release(void *q __rte_unused)
{
}

static int
eth_link_update(struct rte_eth_dev *dev __rte_unused,
		int wait_to_complete __rte_unused)
{
	return 0;
}

static void xdp_umem_destroy(struct xsk_umem_info *umem)
{
	if (umem->mb_pool)
		rte_mempool_free(umem->mb_pool);

	free(umem);
}

static inline uint64_t get_base_addr(struct rte_mempool *mp)
{
	struct rte_mempool_memhdr *memhdr;

	memhdr = STAILQ_FIRST(&mp->mem_list);
	return (uint64_t)(memhdr->addr);
}

static inline uint64_t get_len(struct rte_mempool *mp)
{
	struct rte_mempool_memhdr *memhdr;

	memhdr = STAILQ_FIRST(&mp->mem_list);
	return (uint64_t)(memhdr->len);
}

static struct xsk_umem_info *xdp_umem_configure(void)
{
	struct xsk_umem_info *umem;
	struct xsk_umem_config usr_config = {
		.fill_size = ETH_AF_XDP_DFLT_NUM_DESCS,
		.comp_size = ETH_AF_XDP_DFLT_NUM_DESCS,
		.frame_size = ETH_AF_XDP_FRAME_SIZE,
		.frame_headroom = ETH_AF_XDP_DATA_HEADROOM };
	void *base_addr = NULL;
	char pool_name[0x100];
	int ret;

	umem = calloc(1, sizeof(*umem));
	if (!umem) {
		RTE_LOG(ERR, PMD, "Failed to allocate umem info");
		return NULL;
	}

	snprintf(pool_name, 0x100, "af_xdp_ring");
	umem->mb_pool = rte_pktmbuf_pool_create_with_flags(pool_name,
			ETH_AF_XDP_NUM_BUFFERS,
			250, 0,
			ETH_AF_XDP_FRAME_SIZE -
			ETH_AF_XDP_MBUF_OVERHEAD,
			MEMPOOL_F_NO_SPREAD | MEMPOOL_F_PAGE_ALIGN,
			SOCKET_ID_ANY);

	if (!umem->mb_pool || umem->mb_pool->nb_mem_chunks != 1) {
		RTE_LOG(ERR, PMD,
			"Failed to create rte_mempool\n");
		goto err;
	}
	base_addr = (void *)get_base_addr(umem->mb_pool);

	ret = xsk_umem__create(&umem->umem, base_addr,
			       ETH_AF_XDP_NUM_BUFFERS * ETH_AF_XDP_FRAME_SIZE,
			       &umem->fq, &umem->cq,
			       &usr_config);

	if (ret) {
		RTE_LOG(ERR, PMD, "Failed to create umem");
		goto err;
	}
	umem->buffer = base_addr;

	return umem;

err:
	xdp_umem_destroy(umem);
	return NULL;
}

static int
xsk_configure(struct pmd_internals *internals, struct pkt_rx_queue *rxq,
	      int ring_size)
{
	struct xsk_socket_config cfg;
	struct pkt_tx_queue *txq = rxq->pair;
	int ret = 0;
	int reserve_size;

	rxq->umem = xdp_umem_configure();
	if (!rxq->umem) {
		ret = -ENOMEM;
		goto err;
	}

	cfg.rx_size = ring_size;
	cfg.tx_size = ring_size;
	cfg.libbpf_flags = 0;
	cfg.xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
	cfg.bind_flags = 0;
	ret = xsk_socket__create(&rxq->xsk, internals->if_name,
			internals->queue_idx, rxq->umem->umem, &rxq->rx,
			&txq->tx, &cfg);
	if (ret) {
		RTE_LOG(ERR, PMD, "Failed to create xsk socket.\n");
		goto err;
	}

	reserve_size = ETH_AF_XDP_DFLT_NUM_DESCS / 2;
	ret = reserve_fill_queue(rxq->umem, reserve_size);
	if (ret) {
		RTE_LOG(ERR, PMD, "Failed to reserve fill queue.\n");
		goto err;
	}

	return 0;

err:
	xdp_umem_destroy(rxq->umem);

	return ret;
}

static void
queue_reset(struct pmd_internals *internals, uint16_t queue_idx)
{
	struct pkt_rx_queue *rxq = &internals->rx_queues[queue_idx];
	struct pkt_tx_queue *txq = rxq->pair;
	int xsk_fd = xsk_socket__fd(rxq->xsk);

	if (xsk_fd) {
		close(xsk_fd);
		if (internals->umem) {
			xdp_umem_destroy(internals->umem);
			internals->umem = NULL;
		}
	}
	memset(rxq, 0, sizeof(*rxq));
	memset(txq, 0, sizeof(*txq));
	rxq->pair = txq;
	txq->pair = rxq;
	rxq->queue_idx = queue_idx;
	txq->queue_idx = queue_idx;
}

static int
eth_rx_queue_setup(struct rte_eth_dev *dev,
		   uint16_t rx_queue_id,
		   uint16_t nb_rx_desc,
		   unsigned int socket_id __rte_unused,
		   const struct rte_eth_rxconf *rx_conf __rte_unused,
		   struct rte_mempool *mb_pool)
{
	struct pmd_internals *internals = dev->data->dev_private;
	unsigned int buf_size, data_size;
	struct pkt_rx_queue *rxq;
	int ret = 0;

	if (mb_pool == NULL) {
		RTE_LOG(ERR, PMD,
			"Invalid mb_pool\n");
		ret = -EINVAL;
		goto err;
	}

	if (dev->data->nb_rx_queues <= rx_queue_id) {
		RTE_LOG(ERR, PMD,
			"Invalid rx queue id: %d\n", rx_queue_id);
		ret = -EINVAL;
		goto err;
	}

	rxq = &internals->rx_queues[rx_queue_id];
	queue_reset(internals, rx_queue_id);

	/* Now get the space available for data in the mbuf */
	buf_size = rte_pktmbuf_data_room_size(mb_pool) -
		RTE_PKTMBUF_HEADROOM;
	data_size = ETH_AF_XDP_FRAME_SIZE - ETH_AF_XDP_DATA_HEADROOM;

	if (data_size > buf_size) {
		RTE_LOG(ERR, PMD,
			"%s: %d bytes will not fit in mbuf (%d bytes)\n",
			dev->device->name, data_size, buf_size);
		ret = -ENOMEM;
		goto err;
	}

	rxq->mb_pool = mb_pool;

	if (xsk_configure(internals, rxq, nb_rx_desc)) {
		RTE_LOG(ERR, PMD,
			"Failed to configure xdp socket\n");
		ret = -EINVAL;
		goto err;
	}

	internals->umem = rxq->umem;

	dev->data->rx_queues[rx_queue_id] = rxq;
	return 0;

err:
	queue_reset(internals, rx_queue_id);
	return ret;
}

static int
eth_tx_queue_setup(struct rte_eth_dev *dev,
		   uint16_t tx_queue_id,
		   uint16_t nb_tx_desc,
		   unsigned int socket_id __rte_unused,
		   const struct rte_eth_txconf *tx_conf __rte_unused)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct pkt_tx_queue *txq;

	if (dev->data->nb_tx_queues <= tx_queue_id) {
		RTE_LOG(ERR, PMD, "Invalid tx queue id: %d\n", tx_queue_id);
		return -EINVAL;
	}

	RTE_LOG(WARNING, PMD, "tx queue setup size=%d will be skipped\n",
		nb_tx_desc);
	txq = &internals->tx_queues[tx_queue_id];

	dev->data->tx_queues[tx_queue_id] = txq;
	return 0;
}

static int
eth_dev_mtu_set(struct rte_eth_dev *dev, uint16_t mtu)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct ifreq ifr = { .ifr_mtu = mtu };
	int ret;
	int s;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -EINVAL;

	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", internals->if_name);
	ret = ioctl(s, SIOCSIFMTU, &ifr);
	close(s);

	if (ret < 0)
		return -EINVAL;

	return 0;
}

static void
eth_dev_change_flags(char *if_name, uint32_t flags, uint32_t mask)
{
	struct ifreq ifr;
	int s;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return;

	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", if_name);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0)
		goto out;
	ifr.ifr_flags &= mask;
	ifr.ifr_flags |= flags;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0)
		goto out;
out:
	close(s);
}

static void
eth_dev_promiscuous_enable(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;

	eth_dev_change_flags(internals->if_name, IFF_PROMISC, ~0);
}

static void
eth_dev_promiscuous_disable(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;

	eth_dev_change_flags(internals->if_name, 0, ~IFF_PROMISC);
}

static const struct eth_dev_ops ops = {
	.dev_start = eth_dev_start,
	.dev_stop = eth_dev_stop,
	.dev_close = eth_dev_close,
	.dev_configure = eth_dev_configure,
	.dev_infos_get = eth_dev_info,
	.mtu_set = eth_dev_mtu_set,
	.promiscuous_enable = eth_dev_promiscuous_enable,
	.promiscuous_disable = eth_dev_promiscuous_disable,
	.rx_queue_setup = eth_rx_queue_setup,
	.tx_queue_setup = eth_tx_queue_setup,
	.rx_queue_release = eth_queue_release,
	.tx_queue_release = eth_queue_release,
	.link_update = eth_link_update,
	.stats_get = eth_stats_get,
	.stats_reset = eth_stats_reset,
};

static struct rte_vdev_driver pmd_af_xdp_drv;

static void
parse_parameters(struct rte_kvargs *kvlist,
		 char **if_name,
		 int *queue_idx)
{
	struct rte_kvargs_pair *pair = NULL;
	unsigned int k_idx;

	for (k_idx = 0; k_idx < kvlist->count; k_idx++) {
		pair = &kvlist->pairs[k_idx];
		if (strstr(pair->key, ETH_AF_XDP_IFACE_ARG))
			*if_name = pair->value;
		else if (strstr(pair->key, ETH_AF_XDP_QUEUE_IDX_ARG))
			*queue_idx = atoi(pair->value);
	}
}

static int
get_iface_info(const char *if_name,
	       struct ether_addr *eth_addr,
	       int *if_index)
{
	struct ifreq ifr;
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

	if (sock < 0)
		return -1;

	strcpy(ifr.ifr_name, if_name);
	if (ioctl(sock, SIOCGIFINDEX, &ifr))
		goto error;

	if (ioctl(sock, SIOCGIFHWADDR, &ifr))
		goto error;

	memcpy(eth_addr, ifr.ifr_hwaddr.sa_data, 6);

	close(sock);
	*if_index = if_nametoindex(if_name);
	return 0;

error:
	close(sock);
	return -1;
}

static int
init_internals(struct rte_vdev_device *dev,
	       const char *if_name,
	       int queue_idx)
{
	const char *name = rte_vdev_device_name(dev);
	struct rte_eth_dev *eth_dev = NULL;
	const unsigned int numa_node = dev->device.numa_node;
	struct pmd_internals *internals = NULL;
	int ret;
	int i;

	internals = rte_zmalloc_socket(name, sizeof(*internals), 0, numa_node);
	if (!internals)
		return -ENOMEM;

	internals->queue_idx = queue_idx;
	strcpy(internals->if_name, if_name);

	for (i = 0; i < ETH_AF_XDP_MAX_QUEUE_PAIRS; i++) {
		internals->tx_queues[i].pair = &internals->rx_queues[i];
		internals->rx_queues[i].pair = &internals->tx_queues[i];
	}

	ret = get_iface_info(if_name, &internals->eth_addr,
			     &internals->if_index);
	if (ret)
		goto err;

	eth_dev = rte_eth_vdev_allocate(dev, 0);
	if (!eth_dev)
		goto err;

	eth_dev->data->dev_private = internals;
	eth_dev->data->dev_link = pmd_link;
	eth_dev->data->mac_addrs = &internals->eth_addr;
	eth_dev->dev_ops = &ops;
	eth_dev->rx_pkt_burst = eth_af_xdp_rx;
	eth_dev->tx_pkt_burst = eth_af_xdp_tx;

	rte_eth_dev_probing_finish(eth_dev);
	return 0;

err:
	rte_free(internals);
	return -1;
}

static int
rte_pmd_af_xdp_probe(struct rte_vdev_device *dev)
{
	struct rte_kvargs *kvlist;
	char *if_name = NULL;
	int queue_idx = ETH_AF_XDP_DFLT_QUEUE_IDX;
	struct rte_eth_dev *eth_dev;
	const char *name;
	int ret;

	RTE_LOG(INFO, PMD, "Initializing pmd_af_packet for %s\n",
		rte_vdev_device_name(dev));

	name = rte_vdev_device_name(dev);
	if (rte_eal_process_type() == RTE_PROC_SECONDARY &&
		strlen(rte_vdev_device_args(dev)) == 0) {
		eth_dev = rte_eth_dev_attach_secondary(name);
		if (!eth_dev) {
			RTE_LOG(ERR, PMD, "Failed to probe %s\n", name);
			return -EINVAL;
		}
		eth_dev->dev_ops = &ops;
		rte_eth_dev_probing_finish(eth_dev);
	}

	kvlist = rte_kvargs_parse(rte_vdev_device_args(dev), valid_arguments);
	if (!kvlist) {
		RTE_LOG(ERR, PMD,
			"Invalid kvargs\n");
		return -EINVAL;
	}

	if (dev->device.numa_node == SOCKET_ID_ANY)
		dev->device.numa_node = rte_socket_id();

	parse_parameters(kvlist, &if_name,
			 &queue_idx);

	ret = init_internals(dev, if_name, queue_idx);

	rte_kvargs_free(kvlist);

	return ret;
}

static int
rte_pmd_af_xdp_remove(struct rte_vdev_device *dev)
{
	struct rte_eth_dev *eth_dev = NULL;
	struct pmd_internals *internals;

	RTE_LOG(INFO, PMD, "Removing AF_XDP ethdev on numa socket %u\n",
		rte_socket_id());

	if (!dev)
		return -1;

	/* find the ethdev entry */
	eth_dev = rte_eth_dev_allocated(rte_vdev_device_name(dev));
	if (!eth_dev)
		return -1;

	internals = eth_dev->data->dev_private;

	rte_mempool_free(internals->umem->mb_pool);
	rte_free(internals->umem);
	rte_free(internals);

	rte_eth_dev_release_port(eth_dev);


	return 0;
}

static struct rte_vdev_driver pmd_af_xdp_drv = {
	.probe = rte_pmd_af_xdp_probe,
	.remove = rte_pmd_af_xdp_remove,
};

RTE_PMD_REGISTER_VDEV(net_af_xdp, pmd_af_xdp_drv);
RTE_PMD_REGISTER_ALIAS(net_af_xdp, eth_af_xdp);
RTE_PMD_REGISTER_PARAM_STRING(net_af_xdp,
			      "iface=<string> "
			      "queue=<int> ");

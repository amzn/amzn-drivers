/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifndef ENA_XDP_H
#define ENA_XDP_H

#include "ena_netdev.h"
#ifdef ENA_XDP_SUPPORT
#include <linux/bpf_trace.h>
#ifdef ENA_AF_XDP_SUPPORT
#include <net/xdp_sock_drv.h>

#define ENA_IS_XSK_RING(ring) (!!(ring)->xsk_pool)

#endif /* ENA_AF_XDP_SUPPORT */

/* The max MTU size is configured to be the ethernet frame size without
 * the overhead of the ethernet header, which can have a VLAN header, and
 * a frame check sequence (FCS).
 * The buffer size we share with the device is defined to be ENA_PAGE_SIZE
 */
#ifdef XDP_HAS_FRAME_SZ
#define ENA_XDP_MAX_MTU (ENA_PAGE_SIZE - ETH_HLEN - ETH_FCS_LEN -	\
			 VLAN_HLEN - XDP_PACKET_HEADROOM -		\
			 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#else
#define ENA_XDP_MAX_MTU (ENA_PAGE_SIZE - ETH_HLEN - ETH_FCS_LEN - \
			 VLAN_HLEN - XDP_PACKET_HEADROOM)
#endif

#define ENA_IS_XDP_INDEX(adapter, index) (((index) >= (adapter)->xdp_first_ring) && \
	((index) < (adapter)->xdp_first_ring + (adapter)->xdp_num_queues))

enum ENA_XDP_ACTIONS {
	ENA_XDP_PASS		= 0,
	ENA_XDP_TX		= BIT(0),
	ENA_XDP_REDIRECT	= BIT(1),
	ENA_XDP_DROP		= BIT(2)
};

#ifdef ENA_HAVE_NETDEV_XDP_ACT_XSK_ZEROCOPY
#define ENA_XDP_FEATURES (NETDEV_XDP_ACT_BASIC | \
			  NETDEV_XDP_ACT_REDIRECT | \
			  NETDEV_XDP_ACT_XSK_ZEROCOPY)
#else
#define ENA_XDP_FEATURES (NETDEV_XDP_ACT_BASIC | \
			  NETDEV_XDP_ACT_REDIRECT)
#endif

#define ENA_XDP_FORWARDED (ENA_XDP_TX | ENA_XDP_REDIRECT)

void ena_xdp_exchange_program_rx_in_range(struct ena_adapter *adapter,
					  struct bpf_prog *prog,
					  int first, int last);
int ena_xdp_io_poll(struct napi_struct *napi, int budget);
int ena_xdp_xmit_frame(struct ena_ring *tx_ring,
		       struct ena_adapter *adapter,
		       struct xdp_frame *xdpf);
int ena_xdp_xmit(struct net_device *dev, int n,
		 struct xdp_frame **frames, u32 flags);
int ena_xdp(struct net_device *netdev, struct netdev_bpf *bpf);
int ena_xdp_register_rxq_info(struct ena_ring *rx_ring);
void ena_xdp_unregister_rxq_info(struct ena_ring *rx_ring);
#ifdef ENA_AF_XDP_SUPPORT
void ena_xdp_free_tx_bufs_zc(struct ena_ring *tx_ring);
void ena_xdp_free_rx_bufs_zc(struct ena_ring *rx_ring);
int ena_xdp_xsk_wakeup(struct net_device *netdev, u32 qid, u32 flags);
#endif /* ENA_AF_XDP_SUPPORT */

enum ena_xdp_errors_t {
	ENA_XDP_ALLOWED = 0,
	ENA_XDP_CURRENT_MTU_TOO_LARGE,
	ENA_XDP_NO_ENOUGH_QUEUES,
};

static inline bool ena_xdp_present(struct ena_adapter *adapter)
{
	return !!adapter->xdp_bpf_prog;
}

static inline bool ena_xdp_present_ring(struct ena_ring *ring)
{
	return !!ring->xdp_bpf_prog;
}

static inline bool ena_xdp_legal_queue_count(struct ena_adapter *adapter,
					     u32 queues)
{
	return 2 * queues <= adapter->max_num_io_queues;
}

static inline enum ena_xdp_errors_t ena_xdp_allowed(struct ena_adapter *adapter)
{
	enum ena_xdp_errors_t rc = ENA_XDP_ALLOWED;

	if (adapter->netdev->mtu > ENA_XDP_MAX_MTU)
		rc = ENA_XDP_CURRENT_MTU_TOO_LARGE;
	else if (!ena_xdp_legal_queue_count(adapter, adapter->num_io_queues))
		rc = ENA_XDP_NO_ENOUGH_QUEUES;

	return rc;
}

#ifdef ENA_AF_XDP_SUPPORT
static inline bool ena_is_zc_q_exist(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		if (ENA_IS_XSK_RING(&adapter->rx_ring[i]))
			return true;

	return false;
}

#endif /* ENA_AF_XDP_SUPPORT */
static inline int ena_xdp_execute(struct ena_ring *rx_ring, struct xdp_buff *xdp)
{
	u32 verdict = ENA_XDP_PASS;
	struct bpf_prog *xdp_prog;
	struct ena_ring *xdp_ring;
	struct xdp_frame *xdpf;
	u64 *xdp_stat;

	xdp_prog = READ_ONCE(rx_ring->xdp_bpf_prog);

	verdict = bpf_prog_run_xdp(xdp_prog, xdp);

	switch (verdict) {
	case XDP_TX:
#ifdef XDP_CONVERT_TO_FRAME_NAME_CHANGED
		xdpf = xdp_convert_buff_to_frame(xdp);
#else
		xdpf = convert_to_xdp_frame(xdp);
#endif
		if (unlikely(!xdpf)) {
			trace_xdp_exception(rx_ring->netdev, xdp_prog, verdict);
			xdp_stat = &rx_ring->rx_stats.xdp_aborted;
			verdict = ENA_XDP_DROP;
			break;
		}

		/* Find xmit queue */
		xdp_ring = rx_ring->xdp_ring;

		/* The XDP queues are shared between XDP_TX and XDP_REDIRECT */
		spin_lock(&xdp_ring->xdp_tx_lock);

		if (ena_xdp_xmit_frame(xdp_ring, rx_ring->adapter, xdpf))
			xdp_return_frame(xdpf);

		spin_unlock(&xdp_ring->xdp_tx_lock);
		xdp_stat = &rx_ring->rx_stats.xdp_tx;
		verdict = ENA_XDP_TX;
		break;
	case XDP_REDIRECT:
		if (likely(!xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog))) {
			xdp_stat = &rx_ring->rx_stats.xdp_redirect;
			verdict = ENA_XDP_REDIRECT;
			break;
		}
		trace_xdp_exception(rx_ring->netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_aborted;
		verdict = ENA_XDP_DROP;
		break;
	case XDP_ABORTED:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_aborted;
		verdict = ENA_XDP_DROP;
		break;
	case XDP_DROP:
		xdp_stat = &rx_ring->rx_stats.xdp_drop;
		verdict = ENA_XDP_DROP;
		break;
	case XDP_PASS:
		xdp_stat = &rx_ring->rx_stats.xdp_pass;
		verdict = ENA_XDP_PASS;
		break;
	default:
		bpf_warn_invalid_xdp_action(rx_ring->netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_invalid;
		verdict = ENA_XDP_DROP;
	}

	ena_increase_stat(xdp_stat, 1, &rx_ring->syncp);

	return verdict;
}
#else /* ENA_XDP_SUPPORT */

#define ENA_IS_XDP_INDEX(adapter, index) (false)
#define xdp_return_frame(frame) do {} while (0)

static inline bool ena_xdp_present_ring(struct ena_ring *ring)
{
	return false;
}

static inline int ena_xdp_register_rxq_info(struct ena_ring *rx_ring)
{
	return 0;
}

static inline void ena_xdp_unregister_rxq_info(struct ena_ring *rx_ring) {}

static inline bool ena_xdp_legal_queue_count(struct ena_adapter *adapter,
					     u32 queues)
{
	return false;
}

static inline bool ena_xdp_present(struct ena_adapter *adapter)
{
	return false;
}
#endif /* ENA_XDP_SUPPORT */
#endif /* ENA_XDP_H */

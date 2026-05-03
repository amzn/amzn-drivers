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

/* Start from maximum RX buffer size = ENA_PAGE_SIZE
 * Remove Ethernet header overhead = ETH_HLEN + VLAN_HLEN
 * Reserve headroom (XDP_PACKET_HEADROOM) and tailroom
 * (SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
 */
#define ENA_XDP_MAX_SINGLE_FRAME_SIZE (ENA_PAGE_SIZE - ETH_HLEN - VLAN_HLEN - \
				       XDP_PACKET_HEADROOM -                  \
				       SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

enum ENA_XDP_ACTIONS {
	ENA_XDP_PASS		= 0,
	ENA_XDP_TX		= BIT(0),
	ENA_XDP_REDIRECT	= BIT(1),
	ENA_XDP_RECYCLE		= BIT(2),
	ENA_XDP_DROP		= BIT(3)
};

#define ENA_XDP_FEATURES (NETDEV_XDP_ACT_BASIC |        \
			  NETDEV_XDP_ACT_REDIRECT |     \
			  NETDEV_XDP_ACT_XSK_ZEROCOPY | \
			  NETDEV_XDP_ACT_RX_SG |        \
			  NETDEV_XDP_ACT_NDO_XMIT |	\
			  NETDEV_XDP_ACT_NDO_XMIT_SG)

#define ENA_XDP_FORWARDED (ENA_XDP_TX | ENA_XDP_REDIRECT)

#ifdef ENA_HAVE_XDP_HINTS_DEPS
extern const struct xdp_metadata_ops ena_xdp_md_ops;
#endif /* ENA_HAVE_XDP_HINTS_DEPS */
#if defined(ENA_AF_XDP_SUPPORT) && defined(ENA_HAVE_XSK_TX_METADATA)
extern const struct xsk_tx_metadata_ops ena_xsk_tx_metadata_ops;
#endif /* defined(ENA_AF_XDP_SUPPORT) && defined(ENA_HAVE_XSK_TX_METADATA) */

void ena_xdp_exchange_program_rx_in_range(struct ena_adapter *adapter,
					  struct bpf_prog *prog,
					  int first, int last);
int ena_af_xdp_io_poll(struct napi_struct *napi, int budget);
int ena_xdp_xmit_frame(struct ena_ring *tx_ring,
		       struct ena_adapter *adapter,
		       struct xdp_frame *xdpf);
int ena_xdp_xmit(struct net_device *dev, int n,
		 struct xdp_frame **frames, u32 flags);
int ena_xdp(struct net_device *netdev, struct netdev_bpf *bpf);
int ena_xdp_register_rxq_info(struct ena_ring *rx_ring);
void ena_xdp_unregister_rxq_info(struct ena_ring *rx_ring);
struct sk_buff *ena_rx_skb_after_xdp_pass(struct ena_ring *rx_ring,
					  struct ena_rx_buffer *rx_info,
					  struct ena_com_rx_ctx *ena_rx_ctx,
					  struct xdp_buff *xdp,
					  u8 nr_frags,
					  int xdp_len);
int ena_rx_xdp(struct ena_ring *rx_ring, struct xdp_buff *xdp, u16 descs,
	       int *xdp_len, u8 *nr_frags, struct bpf_prog *xdp_prog);
#ifdef ENA_AF_XDP_SUPPORT
void ena_xdp_free_rx_bufs_zc(struct ena_ring *rx_ring);
int ena_xdp_xsk_wakeup(struct net_device *netdev, u32 qid, u32 flags);
#endif /* ENA_AF_XDP_SUPPORT */

static inline bool ena_xdp_present(struct ena_adapter *adapter)
{
	return !!adapter->xdp_bpf_prog;
}

static inline bool ena_xdp_present_ring(struct ena_ring *ring)
{
	return !!ring->xdp_bpf_prog;
}

static inline bool ena_xdp_is_mtu_legal(unsigned int mtu, struct bpf_prog *prog)
{
#ifndef ENA_XDP_MB_SUPPORT
	return !prog || mtu <= ENA_XDP_MAX_SINGLE_FRAME_SIZE;
#else
	return !prog || prog->aux->xdp_has_frags || mtu <= ENA_XDP_MAX_SINGLE_FRAME_SIZE;
#endif
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
static inline int ena_xdp_execute(struct ena_ring *rx_ring,
				  struct xdp_buff *xdp,
				  struct bpf_prog *xdp_prog)
{
	struct net_device *netdev = rx_ring->netdev;
	u32 verdict = ENA_XDP_PASS;
	struct ena_ring *xdp_ring;
	struct netdev_queue *txq;
	u16 qid = rx_ring->qid;
	struct xdp_frame *xdpf;
	u64 *xdp_stat;

	verdict = bpf_prog_run_xdp(xdp_prog, xdp);

	switch (verdict) {
	case XDP_TX:
#ifdef XDP_CONVERT_TO_FRAME_NAME_CHANGED
		xdpf = xdp_convert_buff_to_frame(xdp);
#else
		xdpf = convert_to_xdp_frame(xdp);
#endif
		if (unlikely(!xdpf)) {
			trace_xdp_exception(netdev, xdp_prog, verdict);
			xdp_stat = &rx_ring->rx_stats.xdp_aborted;
			verdict = ENA_XDP_RECYCLE;
			break;
		}

		/* Find xmit queue */
		xdp_ring = &rx_ring->adapter->tx_ring[qid];

		txq = netdev_get_tx_queue(netdev, qid);
		__netif_tx_lock(txq, smp_processor_id());

		/* Avoid TX time out as we are sharing the queues */
		txq_trans_cond_update(txq);

		if (ena_xdp_xmit_frame(xdp_ring, rx_ring->adapter, xdpf))
			xdp_return_frame(xdpf);
		else
			ena_update_tx_stats(xdp_ring, 1, xdpf->len);

		__netif_tx_unlock(txq);
		xdp_stat = &rx_ring->rx_stats.xdp_tx;
		verdict = ENA_XDP_TX;
		break;
	case XDP_REDIRECT:
		if (likely(!xdp_do_redirect(netdev, xdp, xdp_prog))) {
			xdp_stat = &rx_ring->rx_stats.xdp_redirect;
			verdict = ENA_XDP_REDIRECT;
			break;
		}
		trace_xdp_exception(netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_aborted;
		if (likely(!ena_xdp_return_buff(xdp)))
			verdict = ENA_XDP_DROP;
		else
			verdict = ENA_XDP_RECYCLE;

		break;
	case XDP_ABORTED:
		trace_xdp_exception(netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_aborted;
		verdict = ENA_XDP_RECYCLE;
		break;
	case XDP_DROP:
		xdp_stat = &rx_ring->rx_stats.xdp_drop;
		verdict = ENA_XDP_RECYCLE;
		break;
	case XDP_PASS:
		xdp_stat = &rx_ring->rx_stats.xdp_pass;
		verdict = ENA_XDP_PASS;
		break;
	default:
		bpf_warn_invalid_xdp_action(netdev, xdp_prog, verdict);
		xdp_stat = &rx_ring->rx_stats.xdp_invalid;
		verdict = ENA_XDP_RECYCLE;
	}

	ena_increase_stat(xdp_stat, 1, &rx_ring->syncp);

	return verdict;
}

static inline void ena_xdp_buff_fill(struct xdp_buff *xdp,
				     struct ena_com_rx_ctx *ena_rx_ctx)
{
	struct ena_xdp_buff *ena_xdp = container_of(xdp, struct ena_xdp_buff,
						    xdp_buff);

	ena_xdp->ena_rx_ctx = ena_rx_ctx;
}

#else /* ENA_XDP_SUPPORT */

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

static inline bool ena_xdp_present(struct ena_adapter *adapter)
{
	return false;
}
#endif /* ENA_XDP_SUPPORT */
#endif /* ENA_XDP_H */

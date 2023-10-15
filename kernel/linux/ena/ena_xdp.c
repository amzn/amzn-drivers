// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "ena_xdp.h"
#ifdef ENA_XDP_SUPPORT

static int validate_xdp_req_id(struct ena_ring *tx_ring, u16 req_id)
{
	struct ena_tx_buffer *tx_info;

	tx_info = &tx_ring->tx_buffer_info[req_id];
	if (likely(tx_info->total_tx_size))
		return 0;

	return handle_invalid_req_id(tx_ring, req_id, tx_info, true);
}

static int ena_xdp_tx_map_frame(struct ena_ring *tx_ring,
				struct ena_tx_buffer *tx_info,
				struct xdp_frame *xdpf,
				struct ena_com_tx_ctx *ena_tx_ctx)
{
	struct ena_adapter *adapter = tx_ring->adapter;
	struct ena_com_buf *ena_buf;
	int push_len = 0;
	dma_addr_t dma;
	void *data;
	u32 size;

	tx_info->xdpf = xdpf;
	data = tx_info->xdpf->data;
	size = tx_info->xdpf->len;

	if (tx_ring->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		/* Designate part of the packet for LLQ */
		push_len = min_t(u32, size, tx_ring->tx_max_header_size);

		ena_tx_ctx->push_header = data;

		size -= push_len;
		data += push_len;
	}

	ena_tx_ctx->header_len = push_len;

	if (size > 0) {
		dma = dma_map_single(tx_ring->dev,
				     data,
				     size,
				     DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx_ring->dev, dma)))
			goto error_report_dma_error;

		tx_info->map_linear_data = 0;

		ena_buf = tx_info->bufs;
		ena_buf->paddr = dma;
		ena_buf->len = size;

		ena_tx_ctx->ena_bufs = ena_buf;
		ena_tx_ctx->num_bufs = tx_info->num_of_bufs = 1;
	}

	return 0;

error_report_dma_error:
	ena_increase_stat(&tx_ring->tx_stats.dma_mapping_err, 1,
			  &tx_ring->syncp);
	netif_warn(adapter, tx_queued, adapter->netdev, "Failed to map xdp buff\n");

	return -EINVAL;
}

int ena_xdp_xmit_frame(struct ena_ring *tx_ring,
		       struct ena_adapter *adapter,
		       struct xdp_frame *xdpf)
{
	struct ena_com_tx_ctx ena_tx_ctx = {};
	struct ena_tx_buffer *tx_info;
	u16 next_to_use, req_id;
	int rc;

	next_to_use = tx_ring->next_to_use;
	req_id = tx_ring->free_ids[next_to_use];
	tx_info = &tx_ring->tx_buffer_info[req_id];
	tx_info->num_of_bufs = 0;

	rc = ena_xdp_tx_map_frame(tx_ring, tx_info, xdpf, &ena_tx_ctx);
	if (unlikely(rc))
		goto err;

	ena_tx_ctx.req_id = req_id;

	rc = ena_xmit_common(adapter,
			     tx_ring,
			     tx_info,
			     &ena_tx_ctx,
			     next_to_use,
			     xdpf->len);
	if (rc)
		goto error_unmap_dma;

	return rc;

error_unmap_dma:
	ena_unmap_tx_buff(tx_ring, tx_info);
err:
	tx_info->xdpf = NULL;

	return rc;
}

int ena_xdp_xmit(struct net_device *dev, int n,
			struct xdp_frame **frames, u32 flags)
{
	struct ena_adapter *adapter = netdev_priv(dev);
	struct ena_ring *tx_ring;
	int qid, i, nxmit = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	if (!test_bit(ENA_FLAG_DEV_UP, &adapter->flags))
		return -ENETDOWN;

	/* We assume that all rings have the same XDP program */
	if (!READ_ONCE(adapter->rx_ring->xdp_bpf_prog))
		return -ENXIO;

	qid = smp_processor_id() % adapter->xdp_num_queues;
	qid += adapter->xdp_first_ring;
	tx_ring = &adapter->tx_ring[qid];

	/* Other CPU ids might try to send thorugh this queue */
	spin_lock(&tx_ring->xdp_tx_lock);

	for (i = 0; i < n; i++) {
		if (ena_xdp_xmit_frame(tx_ring, adapter, frames[i]))
			break;
		nxmit++;
	}

	/* Ring doorbell to make device aware of the packets */
	if (flags & XDP_XMIT_FLUSH)
		ena_ring_tx_doorbell(tx_ring);

	spin_unlock(&tx_ring->xdp_tx_lock);

#ifndef ENA_XDP_XMIT_FREES_FAILED_DESCS_INTERNALLY
	for (i = nxmit; unlikely(i < n); i++)
		xdp_return_frame(frames[i]);

#endif
	/* Return number of packets sent */
	return nxmit;
}

static void ena_init_all_xdp_queues(struct ena_adapter *adapter)
{
	adapter->xdp_first_ring = adapter->num_io_queues;
	adapter->xdp_num_queues = adapter->num_io_queues;

	ena_init_io_rings(adapter,
			  adapter->xdp_first_ring,
			  adapter->xdp_num_queues);
}

int ena_setup_and_create_all_xdp_queues(struct ena_adapter *adapter)
{
	int rc = 0;

	rc = ena_setup_tx_resources_in_range(adapter, adapter->xdp_first_ring,
					     adapter->xdp_num_queues);
	if (rc)
		goto setup_err;

	rc = ena_create_io_tx_queues_in_range(adapter,
					      adapter->xdp_first_ring,
					      adapter->xdp_num_queues);
	if (rc)
		goto create_err;

	return 0;

create_err:
	ena_free_all_io_tx_resources_in_range(adapter, adapter->xdp_first_ring,
					      adapter->xdp_num_queues);
setup_err:
	return rc;
}

/* Provides a way for both kernel and bpf-prog to know
 * more about the RX-queue a given XDP frame arrived on.
 */
int ena_xdp_register_rxq_info(struct ena_ring *rx_ring)
{
	int rc;

#ifdef AF_XDP_BUSY_POLL_SUPPORTED
	rc = xdp_rxq_info_reg(&rx_ring->xdp_rxq, rx_ring->netdev, rx_ring->qid,
			      rx_ring->napi->napi_id);
#else
	rc = xdp_rxq_info_reg(&rx_ring->xdp_rxq, rx_ring->netdev, rx_ring->qid);
#endif

	netif_dbg(rx_ring->adapter, ifup, rx_ring->netdev,
		  "Registering RX info for queue %d with napi id %d\n",
		  rx_ring->qid, rx_ring->napi->napi_id);
	if (rc) {
		netif_err(rx_ring->adapter, ifup, rx_ring->netdev,
			  "Failed to register xdp rx queue info. RX queue num %d rc: %d\n",
			  rx_ring->qid, rc);
		goto err;
	}

	if (ENA_IS_XSK_RING(rx_ring)) {
		rc = xdp_rxq_info_reg_mem_model(&rx_ring->xdp_rxq, MEM_TYPE_XSK_BUFF_POOL, NULL);
		xsk_pool_set_rxq_info(rx_ring->xsk_pool, &rx_ring->xdp_rxq);
	} else {
		rc = xdp_rxq_info_reg_mem_model(&rx_ring->xdp_rxq, MEM_TYPE_PAGE_SHARED,
						NULL);
	}

	if (rc) {
		netif_err(rx_ring->adapter, ifup, rx_ring->netdev,
			  "Failed to register xdp rx queue info memory model. RX queue num %d rc: %d\n",
			  rx_ring->qid, rc);
		xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
	}

err:
	return rc;
}

#ifdef ENA_AF_XDP_SUPPORT
void ena_xdp_free_tx_bufs_zc(struct ena_ring *tx_ring)
{
	struct xsk_buff_pool *xsk_pool = tx_ring->xsk_pool;
	int i, xsk_frames = 0;

	for (i = 0; i < tx_ring->ring_size; i++) {
		struct ena_tx_buffer *tx_info = &tx_ring->tx_buffer_info[i];

		if (tx_info->tx_sent_jiffies)
			xsk_frames++;

		tx_info->tx_sent_jiffies = 0;
	}

	if (xsk_frames)
		xsk_tx_completed(xsk_pool, xsk_frames);
}

void ena_xdp_free_rx_bufs_zc(struct ena_adapter *adapter, u32 qid)
{
	struct ena_ring *rx_ring = &adapter->rx_ring[qid];
	int i = 0;

	for (i = 0; i < rx_ring->ring_size; i++) {
		struct ena_rx_buffer *rx_info = &rx_ring->rx_buffer_info[i];

		if (rx_info->xdp)
			xsk_buff_free(rx_info->xdp);

		rx_info->xdp = NULL;
	}
}

#endif /* ENA_AF_XDP_SUPPORT */
void ena_xdp_unregister_rxq_info(struct ena_ring *rx_ring)
{
	netif_dbg(rx_ring->adapter, ifdown, rx_ring->netdev,
		  "Unregistering RX info for queue %d",
		  rx_ring->qid);
	xdp_rxq_info_unreg_mem_model(&rx_ring->xdp_rxq);
	xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
}

void ena_xdp_exchange_program_rx_in_range(struct ena_adapter *adapter,
						 struct bpf_prog *prog,
						 int first, int count)
{
	struct bpf_prog *old_bpf_prog;
	struct ena_ring *rx_ring;
	int i = 0;

	for (i = first; i < count; i++) {
		rx_ring = &adapter->rx_ring[i];
		old_bpf_prog = xchg(&rx_ring->xdp_bpf_prog, prog);

		if (!old_bpf_prog && prog) {
			rx_ring->rx_headroom = XDP_PACKET_HEADROOM;
		} else if (old_bpf_prog && !prog) {
			rx_ring->rx_headroom = NET_SKB_PAD;
		}
	}
}

static void ena_xdp_exchange_program(struct ena_adapter *adapter,
				     struct bpf_prog *prog)
{
	struct bpf_prog *old_bpf_prog = xchg(&adapter->xdp_bpf_prog, prog);

	ena_xdp_exchange_program_rx_in_range(adapter,
					     prog,
					     0,
					     adapter->num_io_queues);

	if (old_bpf_prog)
		bpf_prog_put(old_bpf_prog);
}

static int ena_destroy_and_free_all_xdp_queues(struct ena_adapter *adapter)
{
	bool was_up;
	int rc;

	was_up = test_bit(ENA_FLAG_DEV_UP, &adapter->flags);

	if (was_up)
		ena_down(adapter);

	adapter->xdp_first_ring = 0;
	adapter->xdp_num_queues = 0;
	ena_xdp_exchange_program(adapter, NULL);
	if (was_up) {
		rc = ena_up(adapter);
		if (rc)
			return rc;
	}
	return 0;
}

static int ena_xdp_set(struct net_device *netdev, struct netdev_bpf *bpf)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct bpf_prog *prog = bpf->prog;
	struct bpf_prog *old_bpf_prog;
	int rc, prev_mtu;
	bool is_up;

	is_up = test_bit(ENA_FLAG_DEV_UP, &adapter->flags);
	rc = ena_xdp_allowed(adapter);
	if (rc == ENA_XDP_ALLOWED) {
		old_bpf_prog = adapter->xdp_bpf_prog;
		if (prog) {
			if (!is_up) {
				ena_init_all_xdp_queues(adapter);
			} else if (!old_bpf_prog) {
				ena_down(adapter);
				ena_init_all_xdp_queues(adapter);
			}
			ena_xdp_exchange_program(adapter, prog);

			netif_dbg(adapter, drv, adapter->netdev, "Set a new XDP program\n");

			if (is_up && !old_bpf_prog) {
				rc = ena_up(adapter);
				if (rc)
					return rc;
			}
			xdp_features_set_redirect_target(netdev, false);
		} else if (old_bpf_prog) {
			xdp_features_clear_redirect_target(netdev);
			netif_dbg(adapter, drv, adapter->netdev,
				  "Removing XDP program\n");

			rc = ena_destroy_and_free_all_xdp_queues(adapter);
			if (rc)
				return rc;
		}

		prev_mtu = netdev->max_mtu;
		netdev->max_mtu = prog ? ENA_XDP_MAX_MTU : adapter->max_mtu;

		if (!old_bpf_prog)
			netif_info(adapter, drv, adapter->netdev,
				   "XDP program is set, changing the max_mtu from %d to %d",
				   prev_mtu, netdev->max_mtu);

	} else if (rc == ENA_XDP_CURRENT_MTU_TOO_LARGE) {
		netif_err(adapter, drv, adapter->netdev,
			  "Failed to set xdp program, the current MTU (%d) is larger than the maximum allowed MTU (%lu) while xdp is on",
			  netdev->mtu, ENA_XDP_MAX_MTU);
		NL_SET_ERR_MSG_MOD(bpf->extack,
				   "Failed to set xdp program, the current MTU is larger than the maximum allowed MTU. Check the dmesg for more info");
		return -EINVAL;
	} else if (rc == ENA_XDP_NO_ENOUGH_QUEUES) {
		netif_err(adapter, drv, adapter->netdev,
			  "Failed to set xdp program, the Rx/Tx channel count should be at most half of the maximum allowed channel count. The current queue count (%d), the maximal queue count (%d)\n",
			  adapter->num_io_queues, adapter->max_num_io_queues);
		NL_SET_ERR_MSG_MOD(bpf->extack,
				   "Failed to set xdp program, there is no enough space for allocating XDP queues, Check the dmesg for more info");
		return -EINVAL;
	}

	return 0;
}

#ifdef ENA_AF_XDP_SUPPORT
static bool ena_is_xsk_pool_params_allowed(struct xsk_buff_pool *pool)
{
	return xsk_pool_get_headroom(pool) == 0 &&
	       xsk_pool_get_chunk_size(pool) == ENA_PAGE_SIZE;
}

static int ena_xsk_pool_enable(struct ena_adapter *adapter,
			       struct xsk_buff_pool *pool,
			       u16 qid)
{
	struct ena_ring *rx_ring, *tx_ring;
	bool dev_was_up = false;
	int err;

	if (qid >= adapter->num_io_queues) {
		netdev_err(adapter->netdev,
			   "Max qid for XSK pool is %d (received %d)\n",
			   adapter->num_io_queues, qid);
		return -EINVAL;
	}

	if (ena_is_xsk_pool_params_allowed(pool))
		return -EINVAL;

	rx_ring = &adapter->rx_ring[qid];
	tx_ring = &adapter->tx_ring[qid];

	err = xsk_pool_dma_map(pool, adapter->ena_dev->dmadev, 0);
	if (err) {
		ena_increase_stat(&rx_ring->rx_stats.dma_mapping_err, 1,
				  &rx_ring->syncp);
		netif_err(adapter, drv, adapter->netdev,
			  "Failed to DMA map XSK pool for qid %d\n", qid);
		return err;
	}

	if (test_bit(ENA_FLAG_DEV_UP, &adapter->flags)) {
		dev_was_up = true;
		ena_down(adapter);
	}

	rx_ring->xsk_pool = tx_ring->xsk_pool = pool;

	netif_dbg(adapter, drv, adapter->netdev,
		  "Setting XSK pool for queue %d\n", qid);

	return dev_was_up ? ena_up(adapter) : 0;
}

static int ena_xsk_pool_disable(struct ena_adapter *adapter,
				u16 qid)
{
	struct ena_ring *rx_ring, *tx_ring;
	bool dev_was_up = false;

	if (qid >= adapter->num_io_queues)
		return -EINVAL;

	rx_ring = &adapter->rx_ring[qid];
	tx_ring = &adapter->tx_ring[qid];

	/* XSK pool isn't attached to this ring */
	if (!rx_ring->xsk_pool)
		return 0;

	if (test_bit(ENA_FLAG_DEV_UP, &adapter->flags)) {
		dev_was_up = true;
		ena_down(adapter);
	}

	xsk_pool_dma_unmap(rx_ring->xsk_pool, 0);

	rx_ring->xsk_pool = tx_ring->xsk_pool = NULL;

	netif_dbg(adapter, drv, adapter->netdev,
		  "Removing XSK pool for queue %d\n", qid);

	return dev_was_up ? ena_up(adapter) : 0;
}

static int ena_xsk_pool_setup(struct ena_adapter *adapter,
			      struct xsk_buff_pool *pool,
			      u16 qid)
{
	return pool ? ena_xsk_pool_enable(adapter, pool, qid) :
		      ena_xsk_pool_disable(adapter, qid);
}

#endif /* ENA_AF_XDP_SUPPORT */
/* This is the main xdp callback, it's used by the kernel to set/unset the xdp
 * program as well as to query the current xdp program id.
 */
int ena_xdp(struct net_device *netdev, struct netdev_bpf *bpf)
{
#if defined(ENA_XDP_QUERY_IN_DRIVER) || defined(ENA_AF_XDP_SUPPORT)
	struct ena_adapter *adapter = netdev_priv(netdev);

#endif /* ENA_XDP_QUERY_IN_DRIVER || ENA_AF_XDP_SUPPORT */
	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return ena_xdp_set(netdev, bpf);
#ifdef ENA_AF_XDP_SUPPORT
	case XDP_SETUP_XSK_POOL:
		return ena_xsk_pool_setup(adapter, bpf->xsk.pool, bpf->xsk.queue_id);
#endif /* ENA_AF_XDP_SUPPORT */
#ifdef ENA_XDP_QUERY_IN_DRIVER
	case XDP_QUERY_PROG:
		bpf->prog_id = adapter->xdp_bpf_prog ?
			adapter->xdp_bpf_prog->aux->id : 0;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef ENA_AF_XDP_SUPPORT
int ena_xdp_xsk_wakeup(struct net_device *netdev, u32 qid, u32 flags)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_ring *tx_ring;
	struct napi_struct *napi;

	if (!test_bit(ENA_FLAG_DEV_UP, &adapter->flags))
		return -ENETDOWN;

	if (qid >= adapter->num_io_queues)
		return -EINVAL;

	if (!adapter->xdp_bpf_prog)
		return -ENXIO;

	tx_ring = &adapter->tx_ring[qid];

	if (!ENA_IS_XSK_RING(tx_ring))
		return -ENXIO;

	ena_increase_stat(&tx_ring->tx_stats.xsk_wakeup_request, 1,
			  &tx_ring->syncp);

	napi = tx_ring->napi;

	napi_schedule(napi);

	return 0;
}

#endif /* ENA_AF_XDP_SUPPORT */
static bool ena_clean_xdp_irq(struct ena_ring *tx_ring, u32 budget)
{

	bool is_zc_q = ENA_IS_XSK_RING(tx_ring);
	u32 total_done = 0;
	u16 next_to_clean;
	bool needs_wakeup;
	int tx_pkts = 0;
	u16 req_id;
	int rc;

	next_to_clean = tx_ring->next_to_clean;

	while (tx_pkts < budget) {
		struct ena_tx_buffer *tx_info;
		struct xdp_frame *xdpf;

		rc = ena_com_tx_comp_req_id_get(tx_ring->ena_com_io_cq,
						&req_id);
		if (rc) {
			if (unlikely(rc == -EINVAL))
				handle_invalid_req_id(tx_ring, req_id, NULL,
						      true);
			else if (unlikely(rc == -EFAULT)) {
				ena_reset_device(
					tx_ring->adapter,
					ENA_REGS_RESET_TX_DESCRIPTOR_MALFORMED);
			}
			break;
		}

		/* validate that the request id points to a valid xdp_frame */
		rc = validate_xdp_req_id(tx_ring, req_id);
		if (rc)
			break;

		tx_info = &tx_ring->tx_buffer_info[req_id];

		tx_info->tx_sent_jiffies = 0;

		if (!is_zc_q) {
			xdpf = tx_info->xdpf;
			tx_info->xdpf = NULL;
			ena_unmap_tx_buff(tx_ring, tx_info);
			xdp_return_frame(xdpf);
		}

		netif_dbg(tx_ring->adapter, tx_done, tx_ring->netdev,
			  "tx_poll: q %d pkt #%d req_id %d\n", tx_ring->qid, tx_pkts, req_id);

		tx_pkts++;
		total_done += tx_info->tx_descs;

		tx_info->total_tx_size = 0;

		tx_ring->free_ids[next_to_clean] = req_id;
		next_to_clean = ENA_TX_RING_IDX_NEXT(next_to_clean,
						     tx_ring->ring_size);
	}

	tx_ring->next_to_clean = next_to_clean;
	ena_com_comp_ack(tx_ring->ena_com_io_sq, total_done);

	netif_dbg(tx_ring->adapter, tx_done, tx_ring->netdev,
		  "tx_poll: q %d done. total pkts: %d\n",
		  tx_ring->qid, tx_pkts);

	needs_wakeup = tx_pkts < budget;
#ifdef ENA_AF_XDP_SUPPORT
	if (is_zc_q) {
		struct xsk_buff_pool *xsk_pool = tx_ring->xsk_pool;

		if (tx_pkts)
			xsk_tx_completed(xsk_pool, tx_pkts);

		if (xsk_uses_need_wakeup(xsk_pool)) {
			if (needs_wakeup)
				xsk_set_tx_need_wakeup(xsk_pool);
			else
				xsk_clear_tx_need_wakeup(xsk_pool);
		}
	}
#endif /* ENA_AF_XDP_SUPPORT */

	return needs_wakeup;
}

#ifdef ENA_AF_XDP_SUPPORT
static bool ena_xdp_xmit_irq_zc(struct ena_ring *tx_ring,
				struct napi_struct *napi,
				int budget)
{
	struct xsk_buff_pool *xsk_pool = tx_ring->xsk_pool;
	int size, rc, push_len = 0, work_done = 0;
	struct ena_tx_buffer *tx_info;
	struct ena_com_buf *ena_buf;
	u16 next_to_use, req_id;
	bool need_wakeup = true;
	struct xdp_desc desc;
	dma_addr_t dma;

	while (likely(work_done < budget)) {
		struct ena_com_tx_ctx ena_tx_ctx = {};

		/* We assume the maximum number of descriptors, which is two
		 * (meta data included)
		 */
		if (unlikely(!ena_com_sq_have_enough_space(tx_ring->ena_com_io_sq, 2)))
			break;

		if (!xsk_tx_peek_desc(xsk_pool, &desc))
			break;

		next_to_use = tx_ring->next_to_use;
		req_id = tx_ring->free_ids[next_to_use];
		tx_info = &tx_ring->tx_buffer_info[req_id];

		size = desc.len;

		if (tx_ring->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
			/* Designate part of the packet for LLQ */
			push_len = min_t(u32, size, tx_ring->tx_max_header_size);
			ena_tx_ctx.push_header = xsk_buff_raw_get_data(xsk_pool, desc.addr);
			ena_tx_ctx.header_len = push_len;

			size -= push_len;
			if (!size)
				goto xmit_desc;
		}

		/* Pass the rest of the descriptor as a DMA address. Assuming
		 * single page descriptor.
		 */
		dma  = xsk_buff_raw_get_dma(xsk_pool, desc.addr);
		ena_buf = tx_info->bufs;
		ena_buf->paddr = dma + push_len;
		ena_buf->len = size;

		ena_tx_ctx.ena_bufs = ena_buf;
		ena_tx_ctx.num_bufs = 1;

xmit_desc:
		ena_tx_ctx.req_id = req_id;

		netif_dbg(tx_ring->adapter, tx_queued, tx_ring->netdev,
			  "Queueing zc packet on q %d, %s DMA part (req-id %d)\n",
			  tx_ring->qid, ena_tx_ctx.num_bufs ? "with" : "without", req_id);

		rc = ena_xmit_common(tx_ring->adapter,
				     tx_ring,
				     tx_info,
				     &ena_tx_ctx,
				     next_to_use,
				     desc.len);
		if (rc)
			break;

		work_done++;
	}

	if (work_done) {
		xsk_tx_release(xsk_pool);
		ena_ring_tx_doorbell(tx_ring);
	}

	if (work_done == budget) {
		need_wakeup = false;
		if (xsk_uses_need_wakeup(xsk_pool))
			xsk_clear_tx_need_wakeup(xsk_pool);
	}

	return need_wakeup;
}

static struct sk_buff *ena_xdp_rx_skb_zc(struct ena_ring *rx_ring, struct xdp_buff *xdp)
{
	u32 headroom, data_len;
	struct sk_buff *skb;
	void *data_addr;

	/* Assuming single-page packets for XDP */
	headroom  = xdp->data - xdp->data_hard_start;
	data_len  = xdp->data_end - xdp->data;
	data_addr = xdp->data;

	/* allocate a skb to store the frags */
	skb = __napi_alloc_skb(rx_ring->napi,
			       headroom + data_len,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb)) {
		ena_increase_stat(&rx_ring->rx_stats.skb_alloc_fail, 1,
				  &rx_ring->syncp);
		netif_err(rx_ring->adapter, rx_err, rx_ring->netdev,
			  "Failed to allocate skb in zc queue %d\n", rx_ring->qid);
		return NULL;
	}

	skb_reserve(skb, headroom);
	memcpy(__skb_put(skb, data_len), data_addr, data_len);

	skb->protocol = eth_type_trans(skb, rx_ring->netdev);

	return skb;
}

static int ena_xdp_clean_rx_irq_zc(struct ena_ring *rx_ring,
				   struct napi_struct *napi,
				   int budget)
{
	int i, refill_required, work_done, refill_threshold, pkt_copy;
	u16 next_to_clean = rx_ring->next_to_clean;
	int xdp_verdict, req_id, rc, total_len;
	struct ena_com_rx_ctx ena_rx_ctx;
	struct ena_rx_buffer *rx_info;
	bool xdp_prog_present;
	struct xdp_buff *xdp;
	struct sk_buff *skb;
	u32 xdp_flags = 0;

	netif_dbg(rx_ring->adapter, rx_status, rx_ring->netdev,
		  "%s qid %d\n", __func__, rx_ring->qid);

	ena_rx_ctx.ena_bufs = rx_ring->ena_bufs;
	ena_rx_ctx.max_bufs = rx_ring->sgl_size;

	xdp_prog_present = ena_xdp_present_ring(rx_ring);

	work_done = 0;
	total_len = 0;
	pkt_copy = 0;

	do {
		xdp_verdict = ENA_XDP_PASS;

		/* Poll a packet from HW */
		rc = ena_com_rx_pkt(rx_ring->ena_com_io_cq,
				    rx_ring->ena_com_io_sq,
				    &ena_rx_ctx);
		if (unlikely(rc))
			break;

		/* Polled all RX packets */
		if (unlikely(ena_rx_ctx.descs == 0))
			break;

		netif_dbg(rx_ring->adapter, rx_status, rx_ring->netdev,
			  "rx_poll: q %d got packet from ena. descs #: %d l3 proto %d l4 proto %d hash: %x\n",
			  rx_ring->qid, ena_rx_ctx.descs, ena_rx_ctx.l3_proto,
			  ena_rx_ctx.l4_proto, ena_rx_ctx.hash);

		/* First descriptor might have an offset set by the device */
		rx_info = &rx_ring->rx_buffer_info[ena_rx_ctx.ena_bufs[0].req_id];
		xdp = rx_info->xdp;
		xdp->data += ena_rx_ctx.pkt_offset;
		xdp->data_end = xdp->data + ena_rx_ctx.ena_bufs[0].len;
		xsk_buff_dma_sync_for_cpu(xdp, rx_ring->xsk_pool);

		/* XDP multi-buffer packets not supported */
		if (unlikely(ena_rx_ctx.descs > 1)) {
			netdev_err_once(rx_ring->adapter->netdev,
					"xdp: dropped multi-buffer packets. RX packets must be < %lu\n",
					ENA_XDP_MAX_MTU);
			ena_increase_stat(&rx_ring->rx_stats.xdp_drop, 1, &rx_ring->syncp);
			xdp_verdict = ENA_XDP_DROP;
			goto skip_xdp_prog;
		}

		if (likely(xdp_prog_present))
			xdp_verdict = ena_xdp_execute(rx_ring, xdp);

skip_xdp_prog:
		/* Note that there can be several descriptors, since device
		 * might not honor MTU
		 */
		for (i = 0; i < ena_rx_ctx.descs; i++) {
			req_id = rx_ring->ena_bufs[i].req_id;
			rx_ring->free_ids[next_to_clean] = req_id;
			next_to_clean =
				ENA_RX_RING_IDX_NEXT(next_to_clean,
						     rx_ring->ring_size);
		}

		if (likely(xdp_verdict)) {
			work_done++;
			total_len += ena_rx_ctx.ena_bufs[0].len;
			xdp_flags |= xdp_verdict;

			/* Mark buffer as consumed when it is redirected */
			if (likely(xdp_verdict & ENA_XDP_FORWARDED))
				rx_info->xdp = NULL;

			continue;
		}

		/* XDP PASS */
		skb = ena_xdp_rx_skb_zc(rx_ring, xdp);
		if (unlikely(!skb)) {
			rc = -ENOMEM;
			break;
		}

		pkt_copy++;
		work_done++;
		total_len += ena_rx_ctx.ena_bufs[0].len;
		ena_rx_checksum(rx_ring, &ena_rx_ctx, skb);
		ena_set_rx_hash(rx_ring, &ena_rx_ctx, skb);
		skb_record_rx_queue(skb, rx_ring->qid);
		napi_gro_receive(napi, skb);

	} while (likely(work_done <= budget));

	rx_ring->per_napi_packets += work_done;
	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->rx_stats.bytes += total_len;
	rx_ring->rx_stats.cnt += work_done;
	rx_ring->rx_stats.zc_queue_pkt_copy += pkt_copy;
	u64_stats_update_end(&rx_ring->syncp);

	rx_ring->next_to_clean = next_to_clean;

	if (xdp_flags & ENA_XDP_REDIRECT)
		xdp_do_flush();

	refill_required = ena_com_free_q_entries(rx_ring->ena_com_io_sq);
	refill_threshold =
		min_t(int, rx_ring->ring_size / ENA_RX_REFILL_THRESH_DIVIDER,
		      ENA_RX_REFILL_THRESH_PACKET);
	/* Optimization, try to batch new rx buffers */
	if (refill_required > refill_threshold)
		ena_refill_rx_bufs(rx_ring, refill_required);

	if (xsk_uses_need_wakeup(rx_ring->xsk_pool)) {
		if (likely(rc || work_done < budget)) {
			xsk_set_rx_need_wakeup(rx_ring->xsk_pool);
			ena_increase_stat(&rx_ring->rx_stats.xsk_need_wakeup_set, 1,
					  &rx_ring->syncp);
		} else {
			xsk_clear_rx_need_wakeup(rx_ring->xsk_pool);
		}
	}

	if (unlikely(rc)) {
		struct ena_adapter *adapter = netdev_priv(rx_ring->netdev);

		if (rc == -ENOSPC) {
			ena_increase_stat(&rx_ring->rx_stats.bad_desc_num, 1,
					  &rx_ring->syncp);
			ena_reset_device(adapter,
					 ENA_REGS_RESET_TOO_MANY_RX_DESCS);
		} else if (rc == -EIO) {
			ena_increase_stat(&rx_ring->rx_stats.bad_req_id, 1,
					  &rx_ring->syncp);
			ena_reset_device(adapter, ENA_REGS_RESET_INV_RX_REQ_ID);
		} else if (rc == -EFAULT) {
			ena_reset_device(adapter,
					 ENA_REGS_RESET_RX_DESCRIPTOR_MALFORMED);
		}

		return 0;
	}

	return work_done;
}

#endif /* ENA_AF_XDP_SUPPORT */
/* This is the XDP napi callback. XDP queues use a separate napi callback
 * than Rx/Tx queues.
 */
int ena_xdp_io_poll(struct napi_struct *napi, int budget)
{
	struct ena_napi *ena_napi = container_of(napi, struct ena_napi, napi);
	struct ena_ring *rx_ring, *tx_ring;
	bool needs_wakeup = true;
	u32 rx_work_done = 0;
	int ret;

	rx_ring = ena_napi->rx_ring;
	tx_ring = ena_napi->tx_ring;

	if (!test_bit(ENA_FLAG_DEV_UP, &tx_ring->adapter->flags) ||
	    test_bit(ENA_FLAG_TRIGGER_RESET, &tx_ring->adapter->flags)) {
		napi_complete_done(napi, 0);
		return 0;
	}

	needs_wakeup &= ena_clean_xdp_irq(tx_ring, budget);

#ifdef ENA_AF_XDP_SUPPORT
	if (!ENA_IS_XSK_RING(tx_ring))
		goto polling_done;

	needs_wakeup &= ena_xdp_xmit_irq_zc(tx_ring, napi, budget);

	rx_work_done = ena_xdp_clean_rx_irq_zc(rx_ring, napi, budget);
	needs_wakeup &= rx_work_done < budget;

polling_done:
#endif /* ENA_AF_XDP_SUPPORT */
	/* If the device is about to reset or down, avoid unmask
	 * the interrupt and return 0 so NAPI won't reschedule
	 */
	if (unlikely(!test_bit(ENA_FLAG_DEV_UP, &tx_ring->adapter->flags))) {
		napi_complete_done(napi, 0);
		ret = 0;
	} else if (needs_wakeup) {
		ena_increase_stat(&tx_ring->tx_stats.napi_comp, 1,
				  &tx_ring->syncp);
		if (napi_complete_done(napi, rx_work_done) &&
		    READ_ONCE(ena_napi->interrupts_masked)) {
			smp_rmb(); /* make sure interrupts_masked is read */
			WRITE_ONCE(ena_napi->interrupts_masked, false);
			ena_unmask_interrupt(tx_ring, NULL);
		}

		ena_update_ring_numa_node(tx_ring, NULL);
		ret = rx_work_done;
	} else {
		ret = budget;
	}

	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->tx_stats.tx_poll++;
	u64_stats_update_end(&tx_ring->syncp);
	tx_ring->tx_stats.last_napi_jiffies = jiffies;

	return ret;
}
#endif /* ENA_XDP_SUPPORT */

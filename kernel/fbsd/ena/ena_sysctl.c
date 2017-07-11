/*-
 * BSD LICENSE
 *
 * Copyright (c) 2015-2017 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ena_sysctl.h"

static int	ena_sysctl_update_stats(SYSCTL_HANDLER_ARGS);
static void	ena_sysctl_add_stats(struct ena_adapter *);
#ifdef ENA_DEBUG
static void	ena_sysctl_add_debug(struct ena_adapter *);
static int	ena_sysctl_update_state(SYSCTL_HANDLER_ARGS);
static int	ena_sysctl_dump_stats(SYSCTL_HANDLER_ARGS);
#endif

void
ena_sysctl_add_nodes(struct ena_adapter *adapter)
{
	ena_sysctl_add_stats(adapter);
#ifdef ENA_DEBUG
	ena_sysctl_add_debug(adapter);
#endif
}

static void
ena_sysctl_add_stats(struct ena_adapter *adapter)
{
	device_t dev;

	struct ena_ring *tx_ring;
	struct ena_ring *rx_ring;

	struct ena_hw_stats *hw_stats;
	struct ena_stats_dev *dev_stats;
	struct ena_stats_tx *tx_stats;
	struct ena_stats_rx *rx_stats;
	struct ena_com_stats_admin *admin_stats;

	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	struct sysctl_oid *queue_node, *tx_node, *rx_node, *hw_node;
	struct sysctl_oid *admin_node;
	struct sysctl_oid_list *queue_list, *tx_list, *rx_list, *hw_list;
	struct sysctl_oid_list *admin_list;

#define QUEUE_NAME_LEN 32
	char namebuf[QUEUE_NAME_LEN];
	int i;

	dev = adapter->pdev;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	tx_ring = adapter->tx_ring;
	rx_ring = adapter->rx_ring;

	hw_stats = &adapter->hw_stats;
	dev_stats = &adapter->dev_stats;
	admin_stats = &adapter->ena_dev->admin_queue.stats;

	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "tx_timeout",
	    CTLFLAG_RD, &dev_stats->tx_timeout,
	    "Driver TX timeouts");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "io_suspend",
	    CTLFLAG_RD, &dev_stats->io_suspend,
	    "IO queue suspends");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "io_resume",
	    CTLFLAG_RD, &dev_stats->io_resume,
	    "IO queue resumes");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "wd_expired",
	    CTLFLAG_RD, &dev_stats->wd_expired,
	    "Watchdog expiry count");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "interface_up",
	    CTLFLAG_RD, &dev_stats->interface_up,
	    "Network interface up count");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "interface_down",
	    CTLFLAG_RD, &dev_stats->interface_down,
	    "Network interface down count");
	SYSCTL_ADD_COUNTER_U64(ctx, child, OID_AUTO, "admin_q_pause",
	    CTLFLAG_RD, &dev_stats->admin_q_pause,
	    "Admin queue pauses");

	for (i = 0; i < adapter->num_queues; ++i, ++tx_ring, ++rx_ring) {
		snprintf(namebuf, QUEUE_NAME_LEN, "queue%d", i);

		queue_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO,
		    namebuf, CTLFLAG_RD, NULL, "Queue Name");
		queue_list = SYSCTL_CHILDREN(queue_node);

		/* TX specific stats */
		tx_node = SYSCTL_ADD_NODE(ctx, queue_list, OID_AUTO,
		    "tx_ring", CTLFLAG_RD, NULL, "TX ring");
		tx_list = SYSCTL_CHILDREN(tx_node);

		tx_stats = &tx_ring->tx_stats;

		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "count", CTLFLAG_RD,
		    &tx_stats->cnt, "Packets sent");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "bytes", CTLFLAG_RD,
		    &tx_stats->bytes, "Bytes sent");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "prepare_ctx_err", CTLFLAG_RD,
		    &tx_stats->prepare_ctx_err,
		    "TX buffer preparation failures");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "queue_wakeup", CTLFLAG_RD,
		    &tx_stats->queue_wakeup, "Queue wakeups");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "dma_mapping_err", CTLFLAG_RD,
		    &tx_stats->dma_mapping_err, "DMA mapping failures");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "unsupported_desc_num", CTLFLAG_RD,
		    &tx_stats->unsupported_desc_num,
		    "Excessive descriptor packet discards");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "napi_comp", CTLFLAG_RD,
		    &tx_stats->napi_comp, "Napi completions");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "tx_poll", CTLFLAG_RD,
		    &tx_stats->tx_poll, "TX poll count");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "doorbells", CTLFLAG_RD,
		    &tx_stats->doorbells, "Queue doorbells");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "missing_tx_comp", CTLFLAG_RD,
		    &tx_stats->missing_tx_comp, "TX completions missed");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "bad_req_id", CTLFLAG_RD,
		    &tx_stats->bad_req_id, "Bad request id count");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		    "stops", CTLFLAG_RD,
		    &tx_stats->queue_stop, "Queue stops");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		        "mbuf_collapses", CTLFLAG_RD,
		        &tx_stats->collapse,
		        "Mbuf collapse count");
		SYSCTL_ADD_COUNTER_U64(ctx, tx_list, OID_AUTO,
		        "mbuf_collapse_err", CTLFLAG_RD,
		        &tx_stats->collapse_err,
		        "Mbuf collapse failures");

		/* RX specific stats */
		rx_node = SYSCTL_ADD_NODE(ctx, queue_list, OID_AUTO,
		    "rx_ring", CTLFLAG_RD, NULL, "RX ring");
		rx_list = SYSCTL_CHILDREN(rx_node);

		rx_stats = &rx_ring->rx_stats;

		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "count", CTLFLAG_RD,
		    &rx_stats->cnt, "Packets received");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "bytes", CTLFLAG_RD,
		    &rx_stats->bytes, "Bytes received");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "refil_partial", CTLFLAG_RD,
		    &rx_stats->refil_partial, "Partial refilled mbufs");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "bad_csum", CTLFLAG_RD,
		    &rx_stats->bad_csum, "Bad RX checksum");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "page_alloc_fail", CTLFLAG_RD,
		    &rx_stats->page_alloc_fail, "Failed page allocs");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "mbuf_alloc_fail", CTLFLAG_RD,
		    &rx_stats->mbuf_alloc_fail, "Failed mbuf allocs");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "dma_mapping_err", CTLFLAG_RD,
		    &rx_stats->dma_mapping_err, "DMA mapping errors");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "bad_desc_num", CTLFLAG_RD,
		    &rx_stats->bad_desc_num, "Bad descriptor count");
		SYSCTL_ADD_COUNTER_U64(ctx, rx_list, OID_AUTO,
		    "small_copy_len_pkt", CTLFLAG_RD,
		    &rx_stats->small_copy_len_pkt, "Small copy packet count");
	}

	/* Stats read from device */
	hw_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "hw_stats",
	    CTLFLAG_RD, NULL, "Statistics from hardware");
	hw_list = SYSCTL_CHILDREN(hw_node);

	SYSCTL_ADD_U64(ctx, hw_list, OID_AUTO, "rx_packets", CTLFLAG_RD,
	    &hw_stats->rx_packets, 0, "Packets received");
	SYSCTL_ADD_U64(ctx, hw_list, OID_AUTO, "tx_packets", CTLFLAG_RD,
	    &hw_stats->tx_packets, 0, "Packets transmitted");
	SYSCTL_ADD_U64(ctx, hw_list, OID_AUTO, "rx_bytes", CTLFLAG_RD,
	    &hw_stats->rx_bytes, 0, "Bytes received");
	SYSCTL_ADD_U64(ctx, hw_list, OID_AUTO, "tx_bytes", CTLFLAG_RD,
	    &hw_stats->tx_bytes, 0, "Bytes transmitted");
	SYSCTL_ADD_U64(ctx, hw_list, OID_AUTO, "rx_drops", CTLFLAG_RD,
	    &hw_stats->rx_drops, 0, "Receive packet drops");

	SYSCTL_ADD_PROC(ctx, hw_list, OID_AUTO, "update_stats",
	    CTLTYPE_INT|CTLFLAG_RD, adapter, 0, ena_sysctl_update_stats,
	    "A", "Update stats from hardware");
#ifdef ENA_DEBUG
	SYSCTL_ADD_PROC(ctx, hw_list, OID_AUTO, "dump_stats",
	    CTLTYPE_INT | CTLFLAG_RD, adapter, 0, ena_sysctl_dump_stats, "A",
	    "Dump extended card statistics");
#endif
	/* ENA Admin queue stats */
	admin_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "admin_stats",
	    CTLFLAG_RD, NULL, "ENA Admin Queue statistics");
	admin_list = SYSCTL_CHILDREN(admin_node);

	SYSCTL_ADD_U32(ctx, admin_list, OID_AUTO, "aborted_cmd", CTLFLAG_RD,
	    &admin_stats->aborted_cmd, 0, "Aborted commands");
	SYSCTL_ADD_U32(ctx, admin_list, OID_AUTO, "sumbitted_cmd", CTLFLAG_RD,
	    &admin_stats->submitted_cmd, 0, "Submitted commands");
	SYSCTL_ADD_U32(ctx, admin_list, OID_AUTO, "completed_cmd", CTLFLAG_RD,
	    &admin_stats->completed_cmd, 0, "Completed commands");
	SYSCTL_ADD_U32(ctx, admin_list, OID_AUTO, "out_of_space", CTLFLAG_RD,
	    &admin_stats->out_of_space, 0, "Queue out of space");
	SYSCTL_ADD_U32(ctx, admin_list, OID_AUTO, "no_completion", CTLFLAG_RD,
	    &admin_stats->no_completion, 0, "Commands not completed");
}

static int
ena_sysctl_update_stats(SYSCTL_HANDLER_ARGS)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg1;
	int rc;

	if (adapter->up)
		ena_update_stats_counters(adapter);

	rc = sysctl_handle_string(oidp, "", 1, req);
	return (rc);
}

#ifdef ENA_DEBUG
static void
ena_sysctl_add_debug(struct ena_adapter *adapter)
{
	device_t dev;

	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = adapter->pdev;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "get_state",
	    CTLTYPE_INT|CTLFLAG_RW, adapter, 0, ena_sysctl_update_state,
	    "A", "Update state of the queueus");
}

static int
ena_sysctl_update_state(SYSCTL_HANDLER_ARGS)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg1;
	struct ena_ring *tx_ring, *rx_ring;
	int error, val, i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		val = 0;
		error = sysctl_handle_int(oidp, &val, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (adapter->up) {
		if (val >= adapter->num_queues)
			return EPERM;

		for (i = 0; i < adapter->num_queues; ++i) {
			/* if want single queue, skip others */
			if (val >= 0 && i != val)
				continue;

			tx_ring = &adapter->tx_ring[i];
			rx_ring = &adapter->rx_ring[i];

			device_printf(adapter->pdev,
			    "Queue %d RX sq used space: %d\n", i,
			    ena_com_sq_empty_space(rx_ring->ena_com_io_sq));
			device_printf(adapter->pdev,
			    "Queue %d TX sq free space: %d\n", i,
			    ena_com_sq_empty_space(tx_ring->ena_com_io_sq));

			device_printf(adapter->pdev, "Queue %d RX sq tail: %d, "
			    "next_to_comp: %d\n", i,
			    rx_ring->ena_com_io_sq->tail,
			    rx_ring->ena_com_io_sq->next_to_comp);
			device_printf(adapter->pdev, "Queue %d RX cq head: %d, "
			    "last_head_update: %d\n", i,
			    rx_ring->ena_com_io_cq->head,
			    rx_ring->ena_com_io_cq->last_head_update);

			device_printf(adapter->pdev, "Queue %d TX sq tail: %d, "
			    "next_to_comp: %d\n", i,
			    tx_ring->ena_com_io_sq->tail,
			    tx_ring->ena_com_io_sq->next_to_comp);
			device_printf(adapter->pdev, "Queue %d TX cq head: %d, "
			    "last_head_update: %d\n", i,
			    tx_ring->ena_com_io_cq->head,
			    tx_ring->ena_com_io_cq->last_head_update);
		}
	}
	return (0);
}

static int
ena_sysctl_dump_stats(SYSCTL_HANDLER_ARGS)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg1;
	char *ext_stats_string;
	unsigned int len = PAGE_SIZE;
	int error = 0;
	int rc;

	ext_stats_string = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	memset(ext_stats_string, 0x0, len);
	if (ext_stats_string == NULL) {
		device_printf(adapter->pdev, "Memory allocation for extended "
		    "device statistics failed!\n");
		return (ENOMEM);
	}

	rc = ena_com_get_dev_extended_stats(adapter->ena_dev, ext_stats_string,
	    len);

	device_printf(adapter->pdev, "ENA extended device stats dump:\n%s",
	    ext_stats_string);

	free(ext_stats_string, M_DEVBUF);

	sysctl_handle_string(oidp, "", 1, req);

	return error;
}
#endif /* ENA_DEBUG */

// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#include "ena_debug.h"
#include "ena_netdev.h"
#include "ena_xdp.h"
#include "ena_phc.h"
#include "ena_ethtool.h"

#define ENA_DEBUG_AREA_VERSION				1

#define ENA_DA_RESET_REASON_INFO_VERSION		1
#define ENA_DA_GLOBAL_STATS_VERSION			1
#define ENA_DA_TX_STATS_VERSION				1
#define ENA_DA_RX_STATS_VERSION				1
#define ENA_DA_PHC_STATS_VERSION			1

#define ENA_DA_DEBUG_INFO_ADMIN_TO_VERSION		1
#define ENA_DA_DEBUG_INFO_INV_RX_VERSION		1
#define ENA_DA_DEBUG_INFO_INV_TX_REQ_ID_VERSION		1
#define ENA_DA_DEBUG_INFO_DRIVER_INVALID_STATE_VERSION	1
#define ENA_DA_DEBUG_INFO_NETDEV_WD_VERSION		1

#define ENA_DA_NAME_ENTRY_SIZE				32
#define ENA_DA_LENGTH_ENTRY_SIZE			sizeof(u32)
#define ENA_DA_VERSION_ENTRY_SIZE			sizeof(u32)
#define ENA_DA_STAT_SIZE				sizeof(u64)

#define ENA_DA_XDP_SUPPORTED_BIT			BIT(0)
#define ENA_DA_XDP_MB_SUPPORTED_BIT			BIT(1)
#define ENA_DA_AF_XDP_SUPPORTED_BIT			BIT(2)
#define ENA_DA_PAGE_POOL_SUPPORTED_BIT			BIT(3)
#define ENA_DA_NAPI_STATE_BUSY_POLL_SUPPORTED_BIT	BIT(4)
#ifdef ENA_BUSY_POLL_SUPPORT
#define ENA_DA_BUSY_POLL_SUPPORTED_BIT			BIT(61)
#endif /* ENA_BUSY_POLL_SUPPORT */
#ifdef ENA_LPC_SUPPORT
#define ENA_DA_LPC_SUPPORTED_BIT			BIT(62)
#endif /* ENA_LPC_SUPPORT */

struct ena_debug_area_section_header {
	char section_name[ENA_DA_NAME_ENTRY_SIZE];
	u32 length;
	u32 version;
};

#define ENA_DEBUG_AREA_SECTION_ENTRY(name, ver) {	\
	.section_name = #name,				\
	.version = ver					\
}

static struct ena_debug_area_section_header ena_debug_area_sections[] = {
	ENA_DEBUG_AREA_SECTION_ENTRY(reset_reason_info, ENA_DA_RESET_REASON_INFO_VERSION),
	ENA_DEBUG_AREA_SECTION_ENTRY(global_stats, ENA_DA_GLOBAL_STATS_VERSION),
	ENA_DEBUG_AREA_SECTION_ENTRY(tx_stats, ENA_DA_TX_STATS_VERSION),
	ENA_DEBUG_AREA_SECTION_ENTRY(rx_stats, ENA_DA_RX_STATS_VERSION),
	ENA_DEBUG_AREA_SECTION_ENTRY(phc_stats, ENA_DA_PHC_STATS_VERSION),
};

#define ENA_DEBUG_AREA_SECTIONS_NUM	ARRAY_SIZE(ena_debug_area_sections)

struct ena_rx_stats_header {
	u64 supported_feat;
	u16 max_num_queues;
	u16 num_queues;
	u16 queue_depth;
	u16 reserved;
};

struct ena_tx_stats_header {
	u64 supported_feat;
	u16 max_num_queues;
	u16 num_queues;
	u16 queue_depth;
	u16 reserved;
};

struct ena_phc_stats_header {
	bool phc_active;
};

static int ena_debug_area_rx_stats_len(struct ena_adapter *adapter)
{
	int stats_num = ena_get_stats_array_rx_size();

	return sizeof(struct ena_rx_stats_header) + stats_num *
		ENA_DA_STAT_SIZE;
}

static int ena_debug_area_tx_stats_len(struct ena_adapter *adapter)
{
	int stats_num = ena_get_accum_stats_array_tx_size() +
		adapter->max_num_io_queues *
		ena_get_per_q_stats_array_tx_size();

	return sizeof(struct ena_tx_stats_header) + stats_num *
		ENA_DA_STAT_SIZE;
}

static int ena_debug_area_global_stats_len(struct ena_adapter *adapter)
{
	return ENA_DA_STAT_SIZE * ena_get_stats_array_global_size();
}

static int ena_debug_area_phc_stats_len(struct ena_adapter *adapter)
{
	return sizeof(struct ena_phc_stats_header) +
		ENA_DA_STAT_SIZE * ena_get_stats_array_ena_com_phc_size();
}

static int ena_debug_area_reset_info_len(struct ena_adapter *adapter)
{
	return sizeof(struct ena_reset_reason_debug_info);
}

static void ena_debug_area_dump_rx_stats(struct ena_adapter *adapter, u8 **data)
{
	struct ena_rx_stats_header rx_header = {};

#ifdef ENA_XDP_SUPPORT
	rx_header.supported_feat |= ENA_DA_XDP_SUPPORTED_BIT;
#endif /* ENA_XDP_SUPPORT */
#ifdef ENA_AF_XDP_SUPPORT
	rx_header.supported_feat |= ENA_DA_AF_XDP_SUPPORTED_BIT;
#endif /* ENA_AF_XDP_SUPPORT */
#ifdef ENA_PAGE_POOL_SUPPORT
#ifdef CONFIG_PAGE_POOL_STATS
	rx_header.supported_feat |= ENA_DA_PAGE_POOL_SUPPORTED_BIT;
#endif /* CONFIG_PAGE_POOL_STATS */
#endif /* ENA_PAGE_POOL_SUPPORT */
#ifdef ENA_HAVE_NAPI_STATE_BUSY_POLL
	rx_header.supported_feat |= ENA_DA_NAPI_STATE_BUSY_POLL_SUPPORTED_BIT;
#endif /* ENA_HAVE_NAPI_STATE_BUSY_POLL */
#ifdef ENA_BUSY_POLL_SUPPORT
	rx_header.supported_feat |= ENA_DA_BUSY_POLL_SUPPORTED_BIT;
#endif /* ENA_BUSY_POLL_SUPPORT */
#ifdef ENA_LPC_SUPPORT
	rx_header.supported_feat |= ENA_DA_LPC_SUPPORTED_BIT;
#endif /* ENA_LPC_SUPPORT */
	rx_header.max_num_queues = adapter->max_num_io_queues;
	rx_header.num_queues = adapter->num_io_queues;
	rx_header.queue_depth = adapter->requested_rx_ring_size;

	memcpy(*data, &rx_header, sizeof(struct ena_rx_stats_header));
	*data += sizeof(struct ena_rx_stats_header);

#ifdef ENA_PAGE_POOL_SUPPORT
	ena_fetch_page_pool_stats(adapter);
#endif /* ENA_PAGE_POOL_SUPPORT */
	ena_get_accumulated_rx_stats(adapter, (u64 **)data);
}

static void ena_debug_area_dump_tx_stats(struct ena_adapter *adapter, u8 **data)
{
	struct ena_tx_stats_header tx_header = {};
	int i;

#ifdef ENA_XDP_MB_SUPPORT
	tx_header.supported_feat |= ENA_DA_XDP_MB_SUPPORTED_BIT;
#endif /* ENA_XDP_MB_SUPPORT */
#ifdef ENA_AF_XDP_SUPPORT
	tx_header.supported_feat |= ENA_DA_AF_XDP_SUPPORTED_BIT;
#endif /* ENA_AF_XDP_SUPPORT */
	tx_header.max_num_queues = adapter->max_num_io_queues;
	tx_header.num_queues = adapter->num_io_queues;
	tx_header.queue_depth = adapter->requested_tx_ring_size;
	memcpy(*data, &tx_header, sizeof(struct ena_tx_stats_header));
	*data += sizeof(struct ena_tx_stats_header);

	ena_get_accumulated_tx_stats(adapter, (u64 **)data);

	for (i = 0; i < adapter->max_num_io_queues; i++)
		ena_get_per_queue_tx_stats(&adapter->tx_ring[i], (u64 **)data,
					   false);
}

static void ena_debug_area_dump_phc_stats(struct ena_adapter *adapter,
					  u8 **data)
{
	struct ena_phc_stats_header phc_header;

	phc_header.phc_active = ena_phc_is_active(adapter);
	memcpy(*data, &phc_header, sizeof(struct ena_phc_stats_header));
	*data += sizeof(struct ena_phc_stats_header);

	ena_get_phc_stats(adapter, (u64 **)data);
}

static void ena_debug_area_dump_global_stats(struct ena_adapter *adapter,
					     u8 **data)
{
	ena_get_global_stats(adapter, (u64 **)data);
}

static void ena_debug_area_dump_reset_reason_info(struct ena_adapter *adapter,
						  u8 **data)
{
	u32 buf_len = sizeof(struct ena_reset_reason_debug_info);
	struct ena_reset_reason_debug_info *da_info;

	da_info = adapter->debug_area_info.reset_info;
	memcpy(*data, da_info, buf_len);
	*data += buf_len;
}

static void ena_dec_queue_index(u16 *index, u16 depth)
{
	*index = (*index - 1) & (depth - 1);
}

static void ena_update_last_rx_req_ids(struct ena_com_io_cq *io_cq,
				       u16 *req_id_entries)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	int size = ENA_DA_NUM_LAST_REQ_IDS;
	u16 depth = io_cq->q_depth;
	u16 head_masked;
	u32 offset;

	head_masked = io_cq->head & (depth - 1);

	while (size--) {
		offset = head_masked * io_cq->cdesc_entry_size_in_bytes;
		cdesc = (struct ena_eth_io_rx_cdesc_base *)
				(io_cq->cdesc_addr.virt_addr + offset);
		req_id_entries[size] = cdesc->req_id;
		ena_dec_queue_index(&head_masked, depth);
	}
}

static void ena_update_last_rx_cdescs(struct ena_com_io_cq *io_cq,
				      struct ena_eth_io_rx_cdesc_base *rx_desc)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	int size = ENA_DA_NUM_LAST_DESCS;
	u16 depth = io_cq->q_depth;
	u16 head_masked;
	u32 offset;

	head_masked = io_cq->head & (depth - 1);

	while (size--) {
		offset = head_masked * io_cq->cdesc_entry_size_in_bytes;
		cdesc = (struct ena_eth_io_rx_cdesc_base *)
				(io_cq->cdesc_addr.virt_addr + offset);
		memcpy(&rx_desc[size], cdesc,
		       sizeof(struct ena_eth_io_rx_cdesc_base));
		ena_dec_queue_index(&head_masked, depth);
	}
}

static void ena_update_last_tx_req_ids(struct ena_com_io_cq *io_cq,
				       u16 *req_id_entries)
{
	int size = ENA_DA_NUM_LAST_REQ_IDS;
	struct ena_eth_io_tx_cdesc *cdesc;
	u16 depth = io_cq->q_depth;
	u16 head_masked;
	u32 offset;

	head_masked = io_cq->head & (depth - 1);

	while (size--) {
		offset = head_masked * io_cq->cdesc_entry_size_in_bytes;
		cdesc = (struct ena_eth_io_tx_cdesc *)
				(io_cq->cdesc_addr.virt_addr + offset);
		req_id_entries[size] = cdesc->req_id;
		ena_dec_queue_index(&head_masked, depth);
	}
}

static void ena_update_last_tx_cdescs(struct ena_com_io_cq *io_cq,
				      struct ena_eth_io_tx_cdesc *tx_desc)
{
	struct ena_eth_io_tx_cdesc *cdesc;
	int size = ENA_DA_NUM_LAST_DESCS;
	u16 depth = io_cq->q_depth;
	u16 head_masked;
	u32 offset;

	head_masked = io_cq->head & (depth - 1);

	while (size--) {
		offset = head_masked * io_cq->cdesc_entry_size_in_bytes;
		cdesc = (struct ena_eth_io_tx_cdesc *)
				(io_cq->cdesc_addr.virt_addr + offset);
		memcpy(&tx_desc[size], cdesc,
		       sizeof(struct ena_eth_io_tx_cdesc));
		ena_dec_queue_index(&head_masked, depth);
	}
}

static u16 ena_get_pending_tx_completions(struct ena_ring *tx_ring)
{
	struct ena_tx_buffer *tx_buf;
	u16 pending_tx = 0;
	int i;

	for (i = 0; i < tx_ring->ring_size; i++) {
		tx_buf = &tx_ring->tx_buffer_info[i];
		if (tx_buf->tx_sent_jiffies == 0)
			/* No pending Tx at this buffer */
			continue;
		pending_tx++;
	}

	return pending_tx;
}

void ena_update_reset_info_admin_to(struct ena_adapter *adapter)
{
	struct ena_reset_reason_debug_info *da_info;
	struct ena_debug_info_admin_to *reset_info;
	struct ena_com_admin_queue *admin_queue;

	da_info = adapter->debug_area_info.reset_info;
	admin_queue = &adapter->ena_dev->admin_queue;
	if (!da_info)
		return;

	reset_info = &da_info->admin_to;

	da_info->reset_data_version = ENA_DA_DEBUG_INFO_ADMIN_TO_VERSION;
	reset_info->admin_intr_mask_reg =
		ena_com_get_admin_intr_mask(adapter->ena_dev);
	reset_info->polling_state = admin_queue->polling;
	reset_info->dev_was_up = test_bit(ENA_FLAG_DEV_UP, &adapter->flags);
	reset_info->outstanding_cmds =
		(u16)atomic_read(&admin_queue->outstanding_cmds);
}

void ena_update_reset_info_inv_rx(struct ena_adapter *adapter)
{
	struct ena_reset_reason_debug_info *da_info;
	struct ena_debug_info_inv_rx *reset_info;
	struct ena_com_io_cq *io_cq;
	u16 qid;

	da_info = adapter->debug_area_info.reset_info;
	if (!da_info)
		return;

	qid = adapter->debug_area_info.offending_qid;
	if (qid >= adapter->max_num_io_queues)
		return;

	io_cq = adapter->rx_ring[qid].ena_com_io_cq;
	reset_info = &da_info->inv_rx;

	da_info->reset_data_version = ENA_DA_DEBUG_INFO_INV_RX_VERSION;
	ena_update_last_rx_cdescs(io_cq, reset_info->rx_desc);
	ena_update_last_rx_req_ids(io_cq, reset_info->req_id_entries);
}

void ena_update_reset_info_inv_tx_req_id(struct ena_adapter *adapter)
{
	struct ena_debug_info_inv_tx_req_id *reset_info;
	struct ena_reset_reason_debug_info *da_info;
	struct ena_com_io_cq *io_cq;
	u16 qid;

	da_info = adapter->debug_area_info.reset_info;
	if (!da_info)
		return;

	qid = adapter->debug_area_info.offending_qid;
	if (qid >= adapter->max_num_io_queues)
		return;

	io_cq = adapter->tx_ring[qid].ena_com_io_cq;
	reset_info = &da_info->inv_tx_req_id;

	da_info->reset_data_version = ENA_DA_DEBUG_INFO_INV_TX_REQ_ID_VERSION;
	ena_update_last_tx_cdescs(io_cq, reset_info->tx_desc);
	ena_update_last_tx_req_ids(io_cq, reset_info->req_id_entries);
}

void ena_update_reset_info_invalid_state(struct ena_adapter *adapter)
{
	struct ena_debug_info_driver_invalid_state *reset_info;
	struct ena_reset_reason_debug_info *da_info;
	struct ena_com_admin_queue *admin_queue;

	da_info = adapter->debug_area_info.reset_info;
	admin_queue = &adapter->ena_dev->admin_queue;
	if (!da_info)
		return;

	reset_info = &da_info->invalid_state;

	da_info->reset_data_version =
		ENA_DA_DEBUG_INFO_DRIVER_INVALID_STATE_VERSION;
	reset_info->polling_state = admin_queue->polling;
	reset_info->admin_intr_mask_reg =
		ena_com_get_admin_intr_mask(adapter->ena_dev);
}

void ena_update_reset_info_netdev_wd(struct ena_adapter *adapter)
{
	struct ena_reset_reason_debug_info *da_info;
	struct ena_debug_info_netdev_wd *reset_info;
	u64 msecs_from_keep_alive;
	u16 qid;

	da_info = adapter->debug_area_info.reset_info;
	if (!da_info)
		return;

	qid = adapter->debug_area_info.offending_qid;
	msecs_from_keep_alive =
		jiffies_to_msecs(jiffies - adapter->last_keep_alive_jiffies);
	reset_info = &da_info->netdev_wd;

	da_info->reset_data_version = ENA_DA_DEBUG_INFO_NETDEV_WD_VERSION;
	reset_info->msecs_from_last_keep_alive = msecs_from_keep_alive;
	/* In case qid is MAX_NUM_IO_QUEUES, no offending queue was found */
	if (qid >= ENA_MAX_NUM_IO_QUEUES) {
		reset_info->pending_tx_completions = 0;
		reset_info->pending_tx_submissions = 0;
	} else {
		struct ena_ring *tx_ring = &adapter->tx_ring[qid];

		reset_info->pending_tx_completions =
			ena_get_pending_tx_completions(tx_ring);
		reset_info->pending_tx_submissions =
			ena_com_used_q_entries(tx_ring->ena_com_io_sq);
	}
}

int ena_reset_reason_info_alloc(struct ena_adapter *adapter)
{
	int size = sizeof(*adapter->debug_area_info.reset_info);

	adapter->debug_area_info.reset_info = kzalloc(size, GFP_KERNEL);
	if (unlikely(!adapter->debug_area_info.reset_info))
		return -ENOMEM;

	return 0;
}

void ena_reset_reason_info_free(struct ena_adapter *adapter)
{
	kfree(adapter->debug_area_info.reset_info);
	adapter->debug_area_info.reset_info = NULL;
}

static int (*ena_sections_data_len[])(struct ena_adapter *adapter) = {
	ena_debug_area_reset_info_len,
	ena_debug_area_global_stats_len,
	ena_debug_area_tx_stats_len,
	ena_debug_area_rx_stats_len,
	ena_debug_area_phc_stats_len,
};

static void (*ena_sections_dump_functions[])(struct ena_adapter *adapter,
					     u8 **data) = {
	ena_debug_area_dump_reset_reason_info,
	ena_debug_area_dump_global_stats,
	ena_debug_area_dump_tx_stats,
	ena_debug_area_dump_rx_stats,
	ena_debug_area_dump_phc_stats,
};

u32 ena_get_debug_area_size(struct ena_adapter *adapter)
{
	u32 debug_area_size;
	int i;

	/* Debug area common header size */
	debug_area_size = sizeof(struct ena_com_debug_area_header);

	/* Sections' headers */
	debug_area_size +=
		ENA_DEBUG_AREA_SECTIONS_NUM *
		sizeof(struct ena_debug_area_section_header);

	/* Sections' data parts */
	for (i = 0; i < ENA_DEBUG_AREA_SECTIONS_NUM; i++)
		debug_area_size += ena_sections_data_len[i](adapter);

	return debug_area_size;
}

void ena_write_to_debug_area(struct ena_adapter *adapter)
{
	u8 *debug_area_ptr = adapter->ena_dev->host_attr.debug_area_virt_addr;
	struct ena_reset_reason_debug_info *da_info;
	u32 stat_section_header_size;
	int i;

	da_info = adapter->debug_area_info.reset_info;
	stat_section_header_size = sizeof(struct ena_debug_area_section_header);
	if (!debug_area_ptr || !da_info)
		return;

	da_info->timestamp = jiffies;
	da_info->reset_reason = ena_get_reset_reason(adapter);

	ena_com_write_debug_area_header(ENA_DEBUG_AREA_VERSION,
					ENA_ADMIN_OS_LINUX, debug_area_ptr);
	debug_area_ptr += sizeof(struct ena_com_debug_area_header);

	for (i = 0; i < ENA_DEBUG_AREA_SECTIONS_NUM; i++) {
		/* Write section header */
		struct ena_debug_area_section_header *section;

		section = &ena_debug_area_sections[i];
		section->length = ENA_DA_VERSION_ENTRY_SIZE +
			ena_sections_data_len[i](adapter);

		memcpy(debug_area_ptr, section, stat_section_header_size);
		debug_area_ptr += stat_section_header_size;

		/* Write section data */
		ena_sections_dump_functions[i](adapter, &debug_area_ptr);
	}
}

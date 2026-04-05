/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifndef _ENA_DEBUG_H_
#define _ENA_DEBUG_H_

#include "ena_com.h"

#define ENA_DA_NUM_LAST_DESCS			10
#define ENA_DA_NUM_LAST_REQ_IDS			1024

struct ena_adapter;

struct ena_debug_info_admin_to {
	/* Queried from ENA_REGS_INTR_MASK_OFF */
	u32 admin_intr_mask_reg;
	u16 outstanding_cmds;
	bool polling_state;
	bool dev_was_up;
};

struct ena_debug_info_inv_rx {
	struct ena_eth_io_rx_cdesc_base rx_desc[ENA_DA_NUM_LAST_DESCS];
	u16 req_id_entries[ENA_DA_NUM_LAST_REQ_IDS];
};

struct ena_debug_info_inv_tx_req_id {
	struct ena_eth_io_tx_cdesc tx_desc[ENA_DA_NUM_LAST_DESCS];
	u16 req_id_entries[ENA_DA_NUM_LAST_REQ_IDS];
};

struct ena_debug_info_driver_invalid_state {
	/* Queried from ENA_REGS_INTR_MASK_OFF */
	u32 admin_intr_mask_reg;
	bool polling_state;
};

struct ena_debug_info_netdev_wd {
	u64 msecs_from_last_keep_alive;
	u16 pending_tx_completions;
	u16 pending_tx_submissions;
};

struct ena_reset_reason_debug_info {
	u64 timestamp;
	u32 reset_data_version;
	u16 reset_reason;
	u16 reserved;
	union {
		struct ena_debug_info_admin_to admin_to;
		struct ena_debug_info_inv_rx inv_rx;
		struct ena_debug_info_inv_tx_req_id inv_tx_req_id;
		struct ena_debug_info_driver_invalid_state invalid_state;
		struct ena_debug_info_netdev_wd netdev_wd;
	};
};

void ena_update_reset_info_admin_to(struct ena_adapter *adapter);
void ena_update_reset_info_inv_rx(struct ena_adapter *adapter);
void ena_update_reset_info_inv_tx_req_id(struct ena_adapter *adapter);
void ena_update_reset_info_invalid_state(struct ena_adapter *adapter);
void ena_update_reset_info_netdev_wd(struct ena_adapter *adapter);
u32 ena_get_debug_area_size(struct ena_adapter *adapter);
int ena_reset_reason_info_alloc(struct ena_adapter *adapter);
void ena_reset_reason_info_free(struct ena_adapter *adapter);
void ena_write_to_debug_area(struct ena_adapter *adapter);

#endif /* _ENA_DEBUG_H_ */

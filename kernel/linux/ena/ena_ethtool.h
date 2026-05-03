/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifndef ENA_ETHTOOL_H
#define ENA_ETHTOOL_H

#include "ena_netdev.h"

void ena_get_global_stats(struct ena_adapter *adapter, u64 **data);
void ena_get_accumulated_rx_stats(struct ena_adapter *adapter, u64 **data);
void ena_get_accumulated_tx_stats(struct ena_adapter *adapter, u64 **data);
void ena_get_per_queue_tx_stats(struct ena_ring *ring, u64 **data,
				bool include_accum);
void ena_get_phc_stats(struct ena_adapter *adapter, u64 **data);
void ena_get_per_queue_rx_stats(struct ena_ring *ring, u64 **data);
#ifdef ENA_PAGE_POOL_SUPPORT
void ena_fetch_page_pool_stats(struct ena_adapter *adapter);
#endif /* ENA_PAGE_POOL_SUPPORT */
int ena_get_stats_array_global_size(void);
int ena_get_accum_stats_array_tx_size(void);
int ena_get_per_q_stats_array_tx_size(void);
int ena_get_stats_array_rx_size(void);
int ena_get_stats_array_ena_com_phc_size(void);
int ena_get_queue_sw_stats_count(struct ena_adapter *adapter,
				 bool print_accumulated_stats);
int ena_get_base_sw_stats_count(struct ena_adapter *adapter, bool max_stats);
#endif /* ENA_ETHTOOL_H */

/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#include "ena_netdev.h"

/* LPC definitions */
#define ENA_LPC_DEFAULT_MULTIPLIER 2
#define ENA_LPC_MAX_MULTIPLIER 32
#define ENA_LPC_MULTIPLIER_NOT_CONFIGURED -1
#define ENA_LPC_MULTIPLIER_UNIT 1024
#define ENA_LPC_MIN_NUM_OF_CHANNELS 16

/* Store DMA address along with the page */
struct ena_page {
	struct page *page;
	dma_addr_t dma_addr;
};

struct ena_page_cache {
	/* How many pages are produced */
	u32 head;
	/* How many of the entries were initialized */
	u32 current_size;
	/* Maximum number of pages the cache can hold */
	u32 max_size;

	struct ena_page cache[];
} ____cacheline_aligned;

int ena_create_page_caches(struct ena_adapter *adapter);
void ena_free_page_caches(struct ena_adapter *adapter);
void ena_free_all_cache_pages(struct ena_adapter *adapter);
struct page *ena_lpc_get_page(struct ena_ring *rx_ring, dma_addr_t *dma,
			      bool *is_lpc_page);
bool ena_is_lpc_supported(struct ena_adapter *adapter,
			  struct ena_ring *rx_ring,
			  bool error_print);

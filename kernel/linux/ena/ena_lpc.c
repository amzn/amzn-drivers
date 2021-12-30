// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */
#include "ena_lpc.h"
#include "ena_xdp.h"

static void ena_free_ring_page_cache(struct ena_ring *rx_ring);

static void ena_put_unmap_cache_page(struct ena_ring *rx_ring, struct ena_page *ena_page)
{
	dma_unmap_page(rx_ring->dev, ena_page->dma_addr, ENA_PAGE_SIZE,
		       DMA_BIDIRECTIONAL);

	put_page(ena_page->page);
}

/* Removes a page from page cache and allocate a new one instead. If an
 * allocation of a new page fails, the cache entry isn't changed
 */
static void ena_replace_cache_page(struct ena_ring *rx_ring,
				   struct ena_page *ena_page)
{
	struct page *new_page;
	dma_addr_t dma;

	new_page = ena_alloc_map_page(rx_ring, &dma);

	if (unlikely(IS_ERR(new_page)))
		return;

	ena_put_unmap_cache_page(rx_ring, ena_page);

	ena_page->page = new_page;
	ena_page->dma_addr = dma;
}

/* Mark the cache page as used and return it. If the page belongs to a different
 * NUMA than the current one, free the cache page and allocate another one
 * instead.
 */
static struct page *ena_return_cache_page(struct ena_ring *rx_ring,
					  struct ena_page *ena_page,
					  dma_addr_t *dma)
{
	/* Remove pages belonging to different node than the one the CPU runs on */
	if (unlikely(page_to_nid(ena_page->page) != numa_mem_id())) {
		ena_increase_stat(&rx_ring->rx_stats.lpc_wrong_numa, 1, &rx_ring->syncp);
		ena_replace_cache_page(rx_ring, ena_page);
	}

	/* Make sure no writes are pending for this page */
	dma_sync_single_for_device(rx_ring->dev, ena_page->dma_addr,
				   ENA_PAGE_SIZE,
				   DMA_BIDIRECTIONAL);

	/* Increase refcount to 2 so that the page is returned to the
	 * cache after being freed
	 */
	page_ref_inc(ena_page->page);

	*dma = ena_page->dma_addr;

	return ena_page->page;
}

struct page *ena_lpc_get_page(struct ena_ring *rx_ring, dma_addr_t *dma,
			      bool *is_lpc_page)
{
	struct ena_page_cache *page_cache = rx_ring->page_cache;
	u32 head, cache_current_size;
	struct ena_page *ena_page;

	/* Cache size of zero indicates disabled cache */
	if (!page_cache) {
		*is_lpc_page = false;
		return ena_alloc_map_page(rx_ring, dma);
	}

	*is_lpc_page = true;

	cache_current_size = page_cache->current_size;
	head = page_cache->head;

	ena_page = &page_cache->cache[head];
	/* Warm up phase. We fill the pages for the first time. The
	 * phase is done in the napi context to improve the chances we
	 * allocate on the correct NUMA node
	 */
	if (unlikely(cache_current_size < page_cache->max_size)) {
		/* Check if oldest allocated page is free */
		if (ena_page->page && page_ref_count(ena_page->page) == 1) {
			page_cache->head = (head + 1) % cache_current_size;
			return ena_return_cache_page(rx_ring, ena_page, dma);
		}

		ena_page = &page_cache->cache[cache_current_size];

		/* Add a new page to the cache */
		ena_page->page = ena_alloc_map_page(rx_ring, dma);
		if (unlikely(IS_ERR(ena_page->page)))
			return ena_page->page;

		ena_page->dma_addr = *dma;

		/* Increase refcount to 2 so that the page is returned to the
		 * cache after being freed
		 */
		page_ref_inc(ena_page->page);

		page_cache->current_size++;

		ena_increase_stat(&rx_ring->rx_stats.lpc_warm_up, 1, &rx_ring->syncp);

		return ena_page->page;
	}

	/* Next page is still in use, so we allocate outside the cache */
	if (unlikely(page_ref_count(ena_page->page) != 1)) {
		ena_increase_stat(&rx_ring->rx_stats.lpc_full, 1, &rx_ring->syncp);
		*is_lpc_page = false;
		return ena_alloc_map_page(rx_ring, dma);
	}

	page_cache->head = (head + 1) & (page_cache->max_size - 1);

	return ena_return_cache_page(rx_ring, ena_page, dma);
}

bool ena_is_lpc_supported(struct ena_adapter *adapter,
			  struct ena_ring *rx_ring,
			  bool error_print)
{
#ifdef ENA_NETDEV_LOGS_WITHOUT_RV
	void (*print_log)(const struct net_device *dev, const char *format, ...);
#else
	int (*print_log)(const struct net_device *dev, const char *format, ...);
#endif
	int channels_nr = adapter->num_io_queues + adapter->xdp_num_queues;

	print_log = (error_print) ? netdev_err : netdev_info;

	/* LPC is disabled below min number of channels */
	if (channels_nr < ENA_LPC_MIN_NUM_OF_CHANNELS) {
		print_log(adapter->netdev,
			  "Local page cache is disabled for less than %d channels\n",
			  ENA_LPC_MIN_NUM_OF_CHANNELS);

		/* Disable LPC for such case. It can enabled again through
		 * ethtool private-flag.
		 */
		adapter->used_lpc_size = 0;

		return false;
	}
#ifdef ENA_XDP_SUPPORT

	/* The driver doesn't support page caches under XDP */
	if (ena_xdp_present_ring(rx_ring)) {
		print_log(adapter->netdev,
			  "Local page cache is disabled when using XDP\n");
		return false;
	}
#endif /* ENA_XDP_SUPPORT */

	return true;
}

/* Calculate the size of the Local Page Cache. If LPC should be disabled, return
 * a size of 0.
 */
static u32 ena_calculate_cache_size(struct ena_adapter *adapter,
				    struct ena_ring *rx_ring)
{
	u32 page_cache_size = adapter->used_lpc_size;

	/* LPC cache size of 0 means disabled cache */
	if (page_cache_size == 0)
		return 0;

	if (!ena_is_lpc_supported(adapter, rx_ring, false))
		return 0;

	/* Clap the LPC size to its maximum value */
	if (page_cache_size > ENA_LPC_MAX_MULTIPLIER) {
		netdev_info(adapter->netdev,
			    "Configured LPC size %d is too large, reducing to %d (max)\n",
			    adapter->configured_lpc_size, ENA_LPC_MAX_MULTIPLIER);

		/* Override LPC size to avoid printing this message
		 * every up/down operation
		 */
		adapter->configured_lpc_size = ENA_LPC_MAX_MULTIPLIER;
		adapter->used_lpc_size = page_cache_size = ENA_LPC_MAX_MULTIPLIER;
	}

	page_cache_size = page_cache_size * ENA_LPC_MULTIPLIER_UNIT;
	page_cache_size = roundup_pow_of_two(page_cache_size);

	return page_cache_size;
}

int ena_create_page_caches(struct ena_adapter *adapter)
{
	struct ena_page_cache *cache;
	u32 page_cache_size;
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		struct ena_ring *rx_ring = &adapter->rx_ring[i];

		page_cache_size = ena_calculate_cache_size(adapter, rx_ring);

		if (!page_cache_size)
			return 0;

		cache = vzalloc(sizeof(struct ena_page_cache) +
				sizeof(struct ena_page) * page_cache_size);
		if (!cache)
			goto err_cache_alloc;

		cache->max_size = page_cache_size;
		rx_ring->page_cache = cache;
	}

	return 0;
err_cache_alloc:
	netif_err(adapter, ifup, adapter->netdev,
		  "Failed to initialize local page caches (LPCs)\n");
	while (--i >= 0) {
		struct ena_ring *rx_ring = &adapter->rx_ring[i];

		ena_free_ring_page_cache(rx_ring);
	}

	return -ENOMEM;
}

/* Release all pages from the page cache */
static void ena_free_ring_cache_pages(struct ena_adapter *adapter, int qid)
{
	struct ena_ring *rx_ring = &adapter->rx_ring[qid];
	struct ena_page_cache *page_cache;
	int i;

	/* Page cache is disabled */
	if (!rx_ring->page_cache)
		return;

	page_cache = rx_ring->page_cache;

	/* We check size value to make sure we don't
	 * free pages that weren't allocated.
	 */
	for (i = 0; i < page_cache->current_size; i++) {
		struct ena_page *ena_page = &page_cache->cache[i];

		WARN_ON(!ena_page->page);

		dma_unmap_page(rx_ring->dev, ena_page->dma_addr,
			       ENA_PAGE_SIZE,
			       DMA_BIDIRECTIONAL);

		/* If the page is also in the rx buffer, then this operation
		 * would only decrease its reference count
		 */
		__free_page(ena_page->page);
	}

	page_cache->head = page_cache->current_size = 0;
}

void ena_free_all_cache_pages(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		ena_free_ring_cache_pages(adapter, i);
}

static void ena_free_ring_page_cache(struct ena_ring *rx_ring)
{
	if(!rx_ring->page_cache)
		return;

	vfree(rx_ring->page_cache);
	rx_ring->page_cache = NULL;
}

void ena_free_page_caches(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		struct ena_ring *rx_ring = &adapter->rx_ring[i];

		ena_free_ring_page_cache(rx_ring);
	}
}

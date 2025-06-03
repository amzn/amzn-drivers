// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2023-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sizes.h>

#include "efa_nv_peermem.h"
#include "nv-p2p.h"

#define DRV_MODULE_NAME                "EFA NV Peermem"
#define DRV_MODULE_VER_MAJOR           1
#define DRV_MODULE_VER_MINOR           2
#define DRV_MODULE_VER_SUBMINOR        0

#ifndef DRV_MODULE_VERSION
#define DRV_MODULE_VERSION \
	__stringify(DRV_MODULE_VER_MAJOR) "."   \
	__stringify(DRV_MODULE_VER_MINOR) "."   \
	__stringify(DRV_MODULE_VER_SUBMINOR)
#endif

MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DRV_MODULE_NAME);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_SOFTDEP("pre: nvidia");

static char module_string[] = DRV_MODULE_NAME " v" DRV_MODULE_VERSION;

int efa_nv_peermem_p2p_get_pages(uint64_t p2p_token, uint32_t va_space,
				 uint64_t virtual_address, uint64_t length,
				 struct nvidia_p2p_page_table **page_table,
				 void (*free_callback)(void *data), void *data)
{
	return nvidia_p2p_get_pages(p2p_token, va_space, virtual_address, length,
				    page_table, free_callback, data);
}
EXPORT_SYMBOL_GPL(efa_nv_peermem_p2p_get_pages);

int efa_nv_peermem_p2p_dma_map_pages(struct pci_dev *peer,
				     struct nvidia_p2p_page_table *page_table,
				     struct nvidia_p2p_dma_mapping **dma_mapping)
{
	return nvidia_p2p_dma_map_pages(peer, page_table, dma_mapping);
}
EXPORT_SYMBOL_GPL(efa_nv_peermem_p2p_dma_map_pages);

int efa_nv_peermem_p2p_dma_unmap_pages(struct pci_dev *peer,
				       struct nvidia_p2p_page_table *page_table,
				       struct nvidia_p2p_dma_mapping *dma_mapping)
{
	return nvidia_p2p_dma_unmap_pages(peer, page_table, dma_mapping);
}
EXPORT_SYMBOL_GPL(efa_nv_peermem_p2p_dma_unmap_pages);

int efa_nv_peermem_p2p_put_pages(uint64_t p2p_token,
				 uint32_t va_space, uint64_t virtual_address,
				 struct nvidia_p2p_page_table *page_table)
{
	return nvidia_p2p_put_pages(p2p_token, va_space, virtual_address, page_table);
}
EXPORT_SYMBOL_GPL(efa_nv_peermem_p2p_put_pages);

int init_module(void)
{
	/* Make sure that we didn't inherit taint from Nvidia proprietary driver*/
	if (test_bit(TAINT_PROPRIETARY_MODULE, &THIS_MODULE->taints)) {
		pr_info("efa_nv_peermem: Nvidia proprietary is unsupported\n");
		return -EINVAL;
	}

	if (!nvidia_p2p_get_pages ||
	    !nvidia_p2p_dma_map_pages ||
	    !nvidia_p2p_dma_unmap_pages ||
	    !nvidia_p2p_put_pages) {
		pr_info("efa_nv_peermem: Nvidia P2P symbols are unavailable\n");
		return -ENOSYS;
	}

	pr_info("%s\n", module_string);
	return 0;
}

void cleanup_module(void)
{
}

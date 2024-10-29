/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/pci.h>

#include "nv-p2p.h"

int efa_nv_peermem_p2p_get_pages(uint64_t p2p_token, uint32_t va_space,
				 uint64_t virtual_address, uint64_t length,
				 struct nvidia_p2p_page_table **page_table,
				 void (*free_callback)(void *data), void *data);

int efa_nv_peermem_p2p_dma_map_pages(struct pci_dev *peer,
				     struct nvidia_p2p_page_table *page_table,
				     struct nvidia_p2p_dma_mapping **dma_mapping);

int efa_nv_peermem_p2p_dma_unmap_pages(struct pci_dev *peer,
				       struct nvidia_p2p_page_table *page_table,
				       struct nvidia_p2p_dma_mapping *dma_mapping);

int efa_nv_peermem_p2p_put_pages(uint64_t p2p_token,
				 uint32_t va_space, uint64_t virtual_address,
				 struct nvidia_p2p_page_table *page_table);

// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2019-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/module.h>

#include "efa_p2p.h"
#include "nv-p2p.h"

#define GPU_PAGE_SHIFT 16
#define GPU_PAGE_SIZE BIT_ULL(GPU_PAGE_SHIFT)

struct efa_nvmem_ops {
	int (*get_pages)(u64 p2p_token, u32 va_space, u64 virtual_address,
			 u64 length, struct nvidia_p2p_page_table **page_table,
			 void (*free_callback)(void *data), void *data);
	int (*dma_map_pages)(struct pci_dev *peer,
			     struct nvidia_p2p_page_table *page_table,
			     struct nvidia_p2p_dma_mapping **dma_mapping);
	int (*put_pages)(u64 p2p_token, u32 va_space, u64 virtual_address,
			 struct nvidia_p2p_page_table *page_table);
	int (*dma_unmap_pages)(struct pci_dev *peer,
			       struct nvidia_p2p_page_table *page_table,
			       struct nvidia_p2p_dma_mapping *dma_mapping);
};

struct efa_nvmem {
	struct efa_p2pmem p2pmem;
	struct efa_nvmem_ops ops;
	struct nvidia_p2p_page_table *pgtbl;
	struct nvidia_p2p_dma_mapping *dma_mapping;
	u64 virt_start;
};

static unsigned int nvmem_pgsz(struct efa_dev *dev, struct efa_p2pmem *p2pmem)
{
	struct efa_nvmem *nvmem;

	nvmem = container_of(p2pmem, struct efa_nvmem, p2pmem);

	switch (nvmem->pgtbl->page_size) {
	case NVIDIA_P2P_PAGE_SIZE_4KB:
		return SZ_4K;
	case NVIDIA_P2P_PAGE_SIZE_64KB:
		return SZ_64K;
	case NVIDIA_P2P_PAGE_SIZE_128KB:
		return SZ_128K;
	default:
		return 0;
	}
}

static int nvmem_get_fp(struct efa_nvmem *nvmem)
{
	nvmem->ops.get_pages = symbol_get(nvidia_p2p_get_pages);
	if (!nvmem->ops.get_pages)
		goto err_out;

	nvmem->ops.put_pages = symbol_get(nvidia_p2p_put_pages);
	if (!nvmem->ops.put_pages)
		goto err_put_get_pages;

	nvmem->ops.dma_map_pages = symbol_get(nvidia_p2p_dma_map_pages);
	if (!nvmem->ops.dma_map_pages)
		goto err_put_put_pages;

	nvmem->ops.dma_unmap_pages = symbol_get(nvidia_p2p_dma_unmap_pages);
	if (!nvmem->ops.dma_unmap_pages)
		goto err_put_dma_map_pages;

	return 0;

err_put_dma_map_pages:
	symbol_put(nvidia_p2p_dma_map_pages);
err_put_put_pages:
	symbol_put(nvidia_p2p_put_pages);
err_put_get_pages:
	symbol_put(nvidia_p2p_get_pages);
err_out:
	return -EINVAL;
}

static void nvmem_put_fp(void)
{
	symbol_put(nvidia_p2p_dma_unmap_pages);
	symbol_put(nvidia_p2p_dma_map_pages);
	symbol_put(nvidia_p2p_put_pages);
	symbol_put(nvidia_p2p_get_pages);
}

static void nvmem_free_cb(void *data)
{
	pr_debug("Free callback ticket %llu\n", (u64)data);
	efa_p2p_put((u64)data, true);
}

static int nvmem_get_pages(struct efa_dev *dev, struct efa_nvmem *nvmem,
			   u64 addr, u64 size, u64 ticket)
{
	int err;

	err = nvmem->ops.get_pages(0, 0, addr, size, &nvmem->pgtbl,
				   nvmem_free_cb, (void *)ticket);
	if (err) {
		ibdev_dbg(&dev->ibdev, "nvidia_p2p_get_pages failed %d\n", err);
		return err;
	}

	if (!NVIDIA_P2P_PAGE_TABLE_VERSION_COMPATIBLE(nvmem->pgtbl)) {
		ibdev_dbg(&dev->ibdev, "Incompatible page table version %#08x\n",
			  nvmem->pgtbl->version);
		nvmem->ops.put_pages(0, 0, addr, nvmem->pgtbl);
		nvmem->pgtbl = NULL;
		return -EINVAL;
	}

	return 0;
}

static int nvmem_dma_map(struct efa_dev *dev, struct efa_nvmem *nvmem)
{
	int err;

	err = nvmem->ops.dma_map_pages(dev->pdev, nvmem->pgtbl,
				       &nvmem->dma_mapping);
	if (err) {
		ibdev_dbg(&dev->ibdev, "nvidia_p2p_dma_map_pages failed %d\n",
			  err);
		return err;
	}

	if (!NVIDIA_P2P_DMA_MAPPING_VERSION_COMPATIBLE(nvmem->dma_mapping)) {
		ibdev_dbg(&dev->ibdev, "Incompatible DMA mapping version %#08x\n",
			  nvmem->dma_mapping->version);
		nvmem->ops.dma_unmap_pages(dev->pdev, nvmem->pgtbl,
					   nvmem->dma_mapping);
		nvmem->dma_mapping = NULL;
		return -EINVAL;
	}

	return 0;
}

static struct efa_p2pmem *nvmem_get(struct efa_dev *dev, u64 ticket, u64 start,
				    u64 length)
{
	struct efa_nvmem *nvmem;
	u64 virt_start;
	u64 virt_end;
	u64 pinsz;
	int err;

	nvmem = kzalloc(sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return NULL;

	virt_start = ALIGN_DOWN(start, GPU_PAGE_SIZE);
	virt_end = ALIGN(start + length, GPU_PAGE_SIZE);
	pinsz = virt_end - virt_start;
	nvmem->virt_start = virt_start;

	err = nvmem_get_fp(nvmem);
	if (err)
		/* Nvidia module is not loaded */
		goto err_free;

	err = nvmem_get_pages(dev, nvmem, virt_start, pinsz, ticket);
	if (err)
		/* Most likely not our pages */
		goto err_put_fp;

	err = nvmem_dma_map(dev, nvmem);
	if (err)
		goto err_put;

	return &nvmem->p2pmem;

err_put:
	nvmem->ops.put_pages(0, 0, virt_start, nvmem->pgtbl);
err_put_fp:
	nvmem_put_fp();
err_free:
	kfree(nvmem);
	return NULL;
}

static int nvmem_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			      u64 *page_list)
{
	struct nvidia_p2p_dma_mapping *dma_mapping;
	struct efa_nvmem *nvmem;
	int i;

	nvmem = container_of(p2pmem, struct efa_nvmem, p2pmem);
	dma_mapping = nvmem->dma_mapping;

	for (i = 0; i < dma_mapping->entries; i++)
		page_list[i] = dma_mapping->dma_addresses[i];

	return 0;
}

static void nvmem_release(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			  bool in_cb)
{
	struct efa_nvmem *nvmem;

	nvmem = container_of(p2pmem, struct efa_nvmem, p2pmem);

	if (!in_cb) {
		nvmem->ops.dma_unmap_pages(dev->pdev, nvmem->pgtbl,
					   nvmem->dma_mapping);
		nvmem->ops.put_pages(0, 0, nvmem->virt_start, nvmem->pgtbl);
	}

	nvmem_put_fp();
	kfree(nvmem);
}

bool nvmem_is_supported(void)
{
	struct efa_nvmem dummynv = {};

	if (nvmem_get_fp(&dummynv))
		return false;
	nvmem_put_fp();

	return true;
}

struct nvmem_provider {
	struct efa_p2p_provider p2p;
};

static const struct nvmem_provider prov = {
	.p2p = {
		.ops = {
			.try_get = nvmem_get,
			.to_page_list = nvmem_to_page_list,
			.release = nvmem_release,
			.get_page_size = nvmem_pgsz,
		},
		.type = EFA_P2P_PROVIDER_NVMEM,
	},
};

const struct efa_p2p_provider *nvmem_get_provider(void)
{
	return &prov.p2p;
}

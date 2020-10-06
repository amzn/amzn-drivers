// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2019-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_gdr.h"

#define GPU_PAGE_SHIFT 16
#define GPU_PAGE_SIZE BIT_ULL(GPU_PAGE_SHIFT)

static struct mutex nvmem_list_lock;
static struct list_head nvmem_list;
static atomic64_t next_nvmem_ticket;

void nvmem_init(void)
{
	mutex_init(&nvmem_list_lock);
	INIT_LIST_HEAD(&nvmem_list);
	/*
	 * Ideally, first ticket would be zero, but that would make callback
	 * data NULL which is invalid.
	 */
	atomic64_set(&next_nvmem_ticket, 1);
}

static int nvmem_pgsz(enum nvidia_p2p_page_size_type pgszt)
{
	switch (pgszt) {
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

static struct efa_nvmem *ticket_to_nvmem(u64 ticket)
{
	struct efa_nvmem *nvmem;

	lockdep_assert_held(&nvmem_list_lock);
	list_for_each_entry(nvmem, &nvmem_list, list) {
		if (nvmem->ticket == ticket)
			return nvmem;
	}

	return NULL;
}

int nvmem_put(u64 ticket, bool in_cb)
{
	struct efa_com_dereg_mr_params params = {};
	struct efa_nvmem *nvmem;
	struct efa_dev *dev;
	int err;

	mutex_lock(&nvmem_list_lock);
	nvmem = ticket_to_nvmem(ticket);
	if (!nvmem) {
		pr_debug("Ticket %llu not found in the nvmem list\n", ticket);
		mutex_unlock(&nvmem_list_lock);
		return 0;
	}

	dev = nvmem->dev;
	if (nvmem->needs_dereg) {
		params.l_key = nvmem->lkey;
		err = efa_com_dereg_mr(&dev->edev, &params);
		if (err) {
			mutex_unlock(&nvmem_list_lock);
			return err;
		}
		nvmem->needs_dereg = false;
	}

	nvmem_release(dev, nvmem, in_cb);

	/* Dereg is the last nvmem consumer, delete the ticket */
	if (!in_cb) {
		list_del(&nvmem->list);
		kfree(nvmem);
	}
	mutex_unlock(&nvmem_list_lock);

	return 0;
}

static void nvmem_free_cb(void *data)
{
	pr_debug("Free callback ticket %llu\n", (u64)data);
	nvmem_put((u64)data, true);
}

static int nvmem_get_pages(struct efa_dev *dev, struct efa_nvmem *nvmem,
			   u64 addr, u64 size)
{
	int err;

	err = nvidia_p2p_get_pages(0, 0, addr, size, &nvmem->pgtbl,
				   nvmem_free_cb, (void *)nvmem->ticket);
	if (err) {
		ibdev_dbg(&dev->ibdev, "nvidia_p2p_get_pages failed %d\n", err);
		return err;
	}

	if (!NVIDIA_P2P_PAGE_TABLE_VERSION_COMPATIBLE(nvmem->pgtbl)) {
		ibdev_dbg(&dev->ibdev, "Incompatible page table version %#08x\n",
			  nvmem->pgtbl->version);
		nvidia_p2p_put_pages(0, 0, addr, nvmem->pgtbl);
		nvmem->pgtbl = NULL;
		return -EINVAL;
	}

	return 0;
}

static int nvmem_dma_map(struct efa_dev *dev, struct efa_nvmem *nvmem)
{
	int err;

	err = nvidia_p2p_dma_map_pages(dev->pdev, nvmem->pgtbl,
				       &nvmem->dma_mapping);
	if (err) {
		ibdev_dbg(&dev->ibdev, "nvidia_p2p_dma_map_pages failed %d\n",
			  err);
		return err;
	}

	if (!NVIDIA_P2P_DMA_MAPPING_VERSION_COMPATIBLE(nvmem->dma_mapping)) {
		ibdev_dbg(&dev->ibdev, "Incompatible DMA mapping version %#08x\n",
			  nvmem->dma_mapping->version);
		nvidia_p2p_dma_unmap_pages(dev->pdev, nvmem->pgtbl,
					   nvmem->dma_mapping);
		nvmem->dma_mapping = NULL;
		return -EINVAL;
	}

	return 0;
}

struct efa_nvmem *nvmem_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			    u64 length, unsigned int *pgsz)
{
	struct efa_nvmem *nvmem;
	u64 virt_start;
	u64 pinsz;
	int err;

	nvmem = kzalloc(sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return NULL;

	nvmem->ticket = atomic64_fetch_inc(&next_nvmem_ticket);
	mr->nvmem_ticket = nvmem->ticket;
	nvmem->dev = dev;
	virt_start = ALIGN_DOWN(start, GPU_PAGE_SIZE);
	pinsz = start + length - virt_start;
	nvmem->virt_start = virt_start;

	err = nvmem_get_pages(dev, nvmem, virt_start, pinsz);
	if (err) {
		/* Most likely cpu pages */
		goto err_free;
	}

	err = nvmem_dma_map(dev, nvmem);
	if (err)
		goto err_put;

	*pgsz = nvmem_pgsz(nvmem->pgtbl->page_size);
	if (!*pgsz)
		goto err_unmap;

	mutex_lock(&nvmem_list_lock);
	list_add(&nvmem->list, &nvmem_list);
	mutex_unlock(&nvmem_list_lock);

	return nvmem;

err_unmap:
	nvidia_p2p_dma_unmap_pages(dev->pdev, nvmem->pgtbl, nvmem->dma_mapping);
err_put:
	nvidia_p2p_put_pages(0, 0, start, nvmem->pgtbl);
err_free:
	kfree(nvmem);
	return NULL;
}

int nvmem_to_page_list(struct efa_dev *dev, struct efa_nvmem *nvmem,
		       u64 *page_list)
{
	struct nvidia_p2p_dma_mapping *dma_mapping = nvmem->dma_mapping;
	int i;

	for (i = 0; i < dma_mapping->entries; i++)
		page_list[i] = dma_mapping->dma_addresses[i];

	return 0;
}

void nvmem_release(struct efa_dev *dev, struct efa_nvmem *nvmem, bool in_cb)
{
	if (in_cb) {
		if (nvmem->dma_mapping) {
			nvidia_p2p_free_dma_mapping(nvmem->dma_mapping);
			nvmem->dma_mapping = NULL;
		}

		if (nvmem->pgtbl) {
			nvidia_p2p_free_page_table(nvmem->pgtbl);
			nvmem->pgtbl = NULL;
		}
	} else {
		if (nvmem->dma_mapping)
			nvidia_p2p_dma_unmap_pages(dev->pdev, nvmem->pgtbl,
						   nvmem->dma_mapping);

		if (nvmem->pgtbl)
			nvidia_p2p_put_pages(0, 0, nvmem->virt_start,
					     nvmem->pgtbl);
	}
}

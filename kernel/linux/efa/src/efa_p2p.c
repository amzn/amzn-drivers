// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2019-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/module.h>

#include "efa_p2p.h"
#include "nv-p2p.h"

#define GPU_PAGE_SHIFT 16
#define GPU_PAGE_SIZE BIT_ULL(GPU_PAGE_SHIFT)

static struct mutex p2p_list_lock;
static struct list_head p2p_list;
static atomic64_t next_p2p_ticket;

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

void efa_p2p_init(void)
{
	mutex_init(&p2p_list_lock);
	INIT_LIST_HEAD(&p2p_list);
	/*
	 * Ideally, first ticket would be zero, but that would make callback
	 * data NULL which is invalid.
	 */
	atomic64_set(&next_p2p_ticket, 1);
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

static struct efa_p2pmem *ticket_to_p2p(u64 ticket)
{
	struct efa_p2pmem *p2pmem;

	lockdep_assert_held(&p2p_list_lock);
	list_for_each_entry(p2pmem, &p2p_list, list) {
		if (p2pmem->ticket == ticket)
			return p2pmem;
	}

	return NULL;
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

static void efa_p2p_release(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
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

int efa_p2p_put(u64 ticket, bool in_cb)
{
	struct efa_com_dereg_mr_params params = {};
	struct efa_p2pmem *p2pmem;
	struct efa_dev *dev;
	int err;

	mutex_lock(&p2p_list_lock);
	p2pmem = ticket_to_p2p(ticket);
	if (!p2pmem) {
		pr_debug("Ticket %llu not found in the p2pmem list\n", ticket);
		mutex_unlock(&p2p_list_lock);
		return 0;
	}

	dev = p2pmem->dev;
	if (p2pmem->needs_dereg) {
		params.l_key = p2pmem->lkey;
		err = efa_com_dereg_mr(&dev->edev, &params);
		if (err) {
			mutex_unlock(&p2p_list_lock);
			return err;
		}
		p2pmem->needs_dereg = false;
	}

	list_del(&p2pmem->list);
	mutex_unlock(&p2p_list_lock);
	efa_p2p_release(dev, p2pmem, in_cb);

	return 0;
}

static void nvmem_free_cb(void *data)
{
	pr_debug("Free callback ticket %llu\n", (u64)data);
	efa_p2p_put((u64)data, true);
}

static int nvmem_get_pages(struct efa_dev *dev, struct efa_nvmem *nvmem,
			   u64 addr, u64 size)
{
	int err;

	err = nvmem->ops.get_pages(0, 0, addr, size, &nvmem->pgtbl,
				   nvmem_free_cb, (void *)nvmem->p2pmem.ticket);
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

struct efa_p2pmem *efa_p2p_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			       u64 length, unsigned int *pgsz)
{
	struct efa_nvmem *nvmem;
	u64 virt_start;
	u64 virt_end;
	u64 pinsz;
	int err;

	nvmem = kzalloc(sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return NULL;

	nvmem->p2pmem.ticket = atomic64_fetch_inc(&next_p2p_ticket);
	mr->p2p_ticket = nvmem->p2pmem.ticket;
	nvmem->p2pmem.dev = dev;

	virt_start = ALIGN_DOWN(start, GPU_PAGE_SIZE);
	virt_end = ALIGN(start + length, GPU_PAGE_SIZE);
	pinsz = virt_end - virt_start;
	nvmem->virt_start = virt_start;

	err = nvmem_get_fp(nvmem);
	if (err)
		/* Nvidia module is not loaded */
		goto err_free;

	err = nvmem_get_pages(dev, nvmem, virt_start, pinsz);
	if (err) {
		/* Most likely cpu pages */
		goto err_put_fp;
	}

	err = nvmem_dma_map(dev, nvmem);
	if (err)
		goto err_put;

	*pgsz = nvmem_pgsz(nvmem->pgtbl->page_size);
	if (!*pgsz)
		goto err_unmap;

	mutex_lock(&p2p_list_lock);
	list_add(&nvmem->p2pmem.list, &p2p_list);
	mutex_unlock(&p2p_list_lock);

	return &nvmem->p2pmem;

err_unmap:
	nvmem->ops.dma_unmap_pages(dev->pdev, nvmem->pgtbl, nvmem->dma_mapping);
err_put:
	nvmem->ops.put_pages(0, 0, virt_start, nvmem->pgtbl);
err_put_fp:
	nvmem_put_fp();
err_free:
	kfree(nvmem);
	return NULL;
}

int efa_p2p_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
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

bool nvmem_is_supported(void)
{
	struct efa_nvmem dummynv = {};

	if (nvmem_get_fp(&dummynv))
		return false;
	nvmem_put_fp();

	return true;
}

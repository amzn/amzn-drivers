// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2021-2023 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/module.h>

#include "efa_p2p.h"
#include "neuron_p2p.h"

#define NEURON_PAGE_SHIFT 12
#define NEURON_PAGE_SIZE BIT_ULL(NEURON_PAGE_SHIFT)

struct efa_neuronmem_ops {
	int (*register_va)(u64 virtual_address, u64 length,
			   struct neuron_p2p_va_info **vainfo,
			   void (*free_callback)(void *data),
			   void *data);
	int (*unregister_va)(struct neuron_p2p_va_info *vainfo);
};

struct efa_neuronmem {
	struct efa_p2pmem p2pmem;
	struct efa_neuronmem_ops ops;
	struct neuron_p2p_va_info *va_info;
	u64 virt_start;
};

static unsigned int neuronmem_pgsz(struct efa_dev *dev,
				   struct efa_p2pmem *p2pmem)
{
	struct efa_neuronmem *neuronmem;

	neuronmem = container_of(p2pmem, struct efa_neuronmem, p2pmem);
	return BIT(neuronmem->va_info->shift_page_size);
}

static int neuronmem_get_fp(struct efa_neuronmem *neuronmem)
{
	neuronmem->ops.register_va = symbol_get(neuron_p2p_register_va);
	if (!neuronmem->ops.register_va)
		goto err_out;

	neuronmem->ops.unregister_va = symbol_get(neuron_p2p_unregister_va);
	if (!neuronmem->ops.unregister_va)
		goto err_put_register_va;

	return 0;

err_put_register_va:
	symbol_put(neuron_p2p_register_va);
err_out:
	return -EINVAL;
}

static void neuronmem_put_fp(void)
{
	symbol_put(neuron_p2p_unregister_va);
	symbol_put(neuron_p2p_register_va);
}

static void neuronmem_free_cb(void *data)
{
	pr_debug("Free callback ticket %llu\n", (u64)data);
	efa_p2p_put((u64)data, true);
}

static int neuronmem_register_va(struct efa_dev *dev, struct efa_neuronmem *neuronmem,
				 u64 addr, u64 size, u64 ticket)
{
	int err;

	err = neuronmem->ops.register_va(addr, size, &neuronmem->va_info,
					 neuronmem_free_cb, (void *)ticket);
	if (err) {
		ibdev_dbg(&dev->ibdev, "neuron_p2p_register_va failed %d\n", err);
		return err;
	}

	return 0;
}

static struct efa_p2pmem *neuronmem_get(struct efa_dev *dev, u64 ticket, u64 start,
					u64 length)
{
	struct efa_neuronmem *neuronmem;
	u64 virt_start;
	u64 virt_end;
	u64 pinsz;
	int err;

	neuronmem = kzalloc(sizeof(*neuronmem), GFP_KERNEL);
	if (!neuronmem)
		return NULL;

	virt_start = ALIGN_DOWN(start, NEURON_PAGE_SIZE);
	virt_end = ALIGN(start + length, NEURON_PAGE_SIZE);
	pinsz = virt_end - virt_start;
	neuronmem->virt_start = virt_start;

	err = neuronmem_get_fp(neuronmem);
	if (err)
		/* Neuron module is not loaded */
		goto err_free;

	err = neuronmem_register_va(dev, neuronmem, virt_start, pinsz, ticket);
	if (err)
		/* Most likely not our pages */
		goto err_put_fp;

	return &neuronmem->p2pmem;

err_put_fp:
	neuronmem_put_fp();
err_free:
	kfree(neuronmem);
	return NULL;
}

static int neuronmem_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
				  u64 *page_list)
{
	struct neuron_p2p_page_info *pg_info;
	struct neuron_p2p_va_info *va_info;
	struct efa_neuronmem *neuronmem;
	int ent_idx, pa_idx;
	int pg_idx = 0;
	u64 pa;

	neuronmem = container_of(p2pmem, struct efa_neuronmem, p2pmem);
	va_info = neuronmem->va_info;

	for (ent_idx = 0; ent_idx < va_info->entries; ent_idx++) {
		pg_info = va_info->page_info + ent_idx;
		pa = pg_info->physical_address;
		for (pa_idx = 0; pa_idx < pg_info->page_count; pa_idx++) {
			page_list[pg_idx++] = pa;
			pa += BIT(va_info->shift_page_size);
		}
	}

	return 0;
}

static void neuronmem_release(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			      bool in_cb)
{
	struct efa_neuronmem *neuronmem;

	neuronmem = container_of(p2pmem, struct efa_neuronmem, p2pmem);

	neuronmem->ops.unregister_va(neuronmem->va_info);
	neuronmem_put_fp();
	kfree(neuronmem);
}

static char *neuronmem_provider_string(void)
{
	struct efa_neuronmem dummy = {};

	if (neuronmem_get_fp(&dummy))
		return "";

	neuronmem_put_fp();
	return "NEURON";
}

struct neuronmem_provider {
	struct efa_p2p_provider p2p;
};

static const struct neuronmem_provider prov = {
	.p2p = {
		.ops = {
			.get_provider_string = neuronmem_provider_string,
			.try_get = neuronmem_get,
			.to_page_list = neuronmem_to_page_list,
			.release = neuronmem_release,
			.get_page_size = neuronmem_pgsz,
		},
		.type = EFA_P2P_PROVIDER_NEURON,
	},
};

const struct efa_p2p_provider *neuronmem_get_provider(void)
{
	return &prov.p2p;
}

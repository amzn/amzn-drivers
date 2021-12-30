// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2019-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_p2p.h"

static struct mutex p2p_list_lock;
static struct list_head p2p_list;
static atomic64_t next_p2p_ticket;

static const struct efa_p2p_provider *prov_arr[EFA_P2P_PROVIDER_MAX];

/* Register all providers here */
static void p2p_providers_init(void)
{
	prov_arr[EFA_P2P_PROVIDER_NVMEM] = nvmem_get_provider();
	prov_arr[EFA_P2P_PROVIDER_NEURON] = neuronmem_get_provider();
}

void efa_p2p_init(void)
{
	mutex_init(&p2p_list_lock);
	INIT_LIST_HEAD(&p2p_list);
	/*
	 * Ideally, first ticket would be zero, but that would make callback
	 * data NULL which is invalid.
	 */
	atomic64_set(&next_p2p_ticket, 1);

	p2p_providers_init();
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
	p2pmem->prov->ops.release(dev, p2pmem, in_cb);

	return 0;
}

struct efa_p2pmem *efa_p2p_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			       u64 length)
{
	const struct efa_p2p_provider *prov;
	struct efa_p2pmem *p2pmem;
	u64 ticket;
	int i;

	ticket = atomic64_fetch_inc(&next_p2p_ticket);
	for (i = 0; i < EFA_P2P_PROVIDER_MAX; i++) {
		prov = prov_arr[i];
		p2pmem = prov->ops.try_get(dev, ticket, start, length);
		if (p2pmem)
			break;
	}
	if (!p2pmem)
		/* No provider was found, most likely cpu pages */
		return NULL;

	p2pmem->dev = dev;
	p2pmem->ticket = ticket;
	p2pmem->prov = prov;
	mr->p2p_ticket = p2pmem->ticket;

	mutex_lock(&p2p_list_lock);
	list_add(&p2pmem->list, &p2p_list);
	mutex_unlock(&p2p_list_lock);

	return p2pmem;
}

int efa_p2p_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			 u64 *page_list)
{
	return p2pmem->prov->ops.to_page_list(dev, p2pmem, page_list);
}

unsigned int efa_p2p_get_page_size(struct efa_dev *dev,
				   struct efa_p2pmem *p2pmem)
{
	return p2pmem->prov->ops.get_page_size(dev, p2pmem);
}

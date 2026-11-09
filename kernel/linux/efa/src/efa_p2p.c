// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2019-2025 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_p2p.h"

static struct mutex p2p_list_lock;
static struct list_head p2p_list;
static atomic64_t next_p2p_ticket;

static const struct efa_p2p_provider *prov_arr[EFA_P2P_PROVIDER_MAX];

/* Register all providers here */
static void p2p_providers_init(void)
{
	prov_arr[EFA_P2P_PROVIDER_NVMEM_V1] = nvmem_v1_get_provider();
	prov_arr[EFA_P2P_PROVIDER_NVMEM_V2] = nvmem_v2_get_provider();
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

/* Returns the node with a reference held; caller must efa_p2p_put_node() when done. */
static struct efa_p2p_node *efa_p2p_lookup_locked(u64 ticket)
{
	struct efa_p2p_node *node;

	lockdep_assert_held(&p2p_list_lock);
	list_for_each_entry(node, &p2p_list, list) {
		if (node->ticket == ticket) {
			kref_get(&node->refcount);
			return node;
		}
	}

	return NULL;
}

static void efa_p2p_node_release(struct kref *kref)
{
	struct efa_p2p_node *node =
		container_of(kref, struct efa_p2p_node, refcount);

	kfree(node);
}

static void efa_p2p_put_node(struct efa_p2p_node *node)
{
	kref_put(&node->refcount, efa_p2p_node_release);
}

static void efa_p2p_list_get_and_add(struct efa_p2p_node *node)
{
	mutex_lock(&p2p_list_lock);
	kref_get(&node->refcount);
	list_add(&node->list, &p2p_list);
	mutex_unlock(&p2p_list_lock);
}

static void efa_p2p_list_del_and_put_locked(struct efa_p2p_node *node)
{
	lockdep_assert_held(&p2p_list_lock);
	list_del_init(&node->list);
	efa_p2p_put_node(node);
}

static void efa_p2p_list_del_and_put(struct efa_p2p_node *node)
{
	mutex_lock(&p2p_list_lock);
	efa_p2p_list_del_and_put_locked(node);
	mutex_unlock(&p2p_list_lock);
}

char *efa_p2p_provider_string(void)
{
	const struct efa_p2p_provider *prov;
	char *prov_string;
	int i;

	for (i = 0; i < EFA_P2P_PROVIDER_MAX; i++) {
		prov = prov_arr[i];
		prov_string = prov->ops.get_provider_string();
		if (prov_string[0] != '\0')
			/* Only the first available provider is returned */
			return prov_string;
	}

	return "";
}

int efa_p2p_put(u64 ticket, bool in_cb)
{
	struct efa_com_dereg_mr_params params = {};
	struct efa_p2p_node *node;
	struct efa_p2pmem *p2pmem;
	struct efa_dev *dev;
	int status;

	mutex_lock(&p2p_list_lock);
	node = efa_p2p_lookup_locked(ticket);
	if (!node) {
		pr_debug("Ticket %llu not found in the p2p list\n", ticket);
		mutex_unlock(&p2p_list_lock);
		return 0;
	}

	if (in_cb && !node->initialized) {
		mutex_unlock(&p2p_list_lock);
		wait_for_completion(&node->init_done);
		mutex_lock(&p2p_list_lock);
		if (list_empty(&node->list)) {
			status = 0;
			goto dec_and_done;
		}
	}

	p2pmem = node->p2pmem;
	dev = p2pmem->dev;
	if (node->initialized) {
		params.l_key = node->lkey;
		status = efa_com_dereg_mr(&dev->edev, &params);
		if (status) {
			goto dec_and_done;
		}
	}
	efa_p2p_list_del_and_put_locked(node);
	mutex_unlock(&p2p_list_lock);
	/* Wakes a callback parked on an uninitialized node; a no-op once committed. */
	complete_all(&node->init_done);
	p2pmem->prov->ops.release(dev, p2pmem, in_cb);
	efa_p2p_put_node(node); /* access reference */

	return 0;

dec_and_done:
	mutex_unlock(&p2p_list_lock);
	efa_p2p_put_node(node); /* access reference */
	return status;
}

struct efa_p2pmem *efa_p2p_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			       u64 length)
{
	const struct efa_p2p_provider *prov;
	struct efa_p2p_node *node;
	static bool message_printed;
	struct efa_p2pmem *p2pmem;
	u64 ticket;
	int i;

	ticket = atomic64_fetch_inc(&next_p2p_ticket);

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;
	node->ticket = ticket;
	init_completion(&node->init_done);
	kref_init(&node->refcount);
	efa_p2p_list_get_and_add(node);

	for (i = 0; i < EFA_P2P_PROVIDER_MAX; i++) {
		prov = prov_arr[i];
		p2pmem = prov->ops.try_get(dev, ticket, start, length);
		if (p2pmem)
			break;
	}
	if (!p2pmem) {
		/* No provider was found, most likely cpu pages */
		efa_p2p_list_del_and_put(node);
		complete_all(&node->init_done);
		efa_p2p_put_node(node); /* creation reference */
		return NULL;
	}

	p2pmem->dev = dev;
	p2pmem->prov = prov;
	mr->p2p_ticket = ticket;
	node->p2pmem = p2pmem;

	if (!message_printed) {
		pr_info("efa: Acquired peer memory using P2P");
		message_printed = true;
	}

	/* Hand off: the list now owns the node; drop the creation reference. */
	efa_p2p_put_node(node);
	return p2pmem;
}

void efa_p2p_commit_done(u64 ticket, u32 lkey)
{
	struct efa_p2p_node *node;

	mutex_lock(&p2p_list_lock);
	node = efa_p2p_lookup_locked(ticket);
	/* Set lkey before signalling, so a woken callback observes it. */
	node->lkey = lkey;
	node->initialized = true;
	mutex_unlock(&p2p_list_lock);
	complete_all(&node->init_done);
	efa_p2p_put_node(node); /* access reference */
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

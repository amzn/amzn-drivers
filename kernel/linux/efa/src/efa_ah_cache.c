// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/slab.h>

#include "efa_ah_cache.h"

static const struct rhashtable_params ah_cache_params = {
	.key_len = sizeof(struct efa_ah_cache_key),
	.key_offset = offsetof(struct efa_ah_cache_entry, key),
	.head_offset = offsetof(struct efa_ah_cache_entry, linkage),
};

int efa_ah_cache_init(struct efa_ah_cache *ah_cache)
{
	int err;

	mutex_init(&ah_cache->lock);
	err = rhashtable_init(&ah_cache->hashtable, &ah_cache_params);
	if (err)
		mutex_destroy(&ah_cache->lock);

	return err;
}

static void efa_ah_cache_entry_free(void *ptr, void *arg)
{
	struct efa_ah_cache_entry *entry = ptr;

	WARN_ON(entry->usecnt);
	mutex_destroy(&entry->lock);
	kfree(entry);
}

void efa_ah_cache_destroy(struct efa_ah_cache *ah_cache)
{
	rhashtable_free_and_destroy(&ah_cache->hashtable, efa_ah_cache_entry_free, NULL);
	mutex_destroy(&ah_cache->lock);
}

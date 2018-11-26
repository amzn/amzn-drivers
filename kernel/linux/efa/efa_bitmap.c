// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#include <linux/bitmap.h>

#include "efa.h"

u32 efa_bitmap_alloc(struct efa_bitmap *bitmap)
{
	u32 obj;

	spin_lock(&bitmap->lock);

	obj = find_next_zero_bit(bitmap->table, bitmap->max, bitmap->last);
	if (obj >= bitmap->max)
		obj = find_first_zero_bit(bitmap->table, bitmap->max);

	if (obj < bitmap->max) {
		set_bit(obj, bitmap->table);
		bitmap->last = obj + 1;
		if (bitmap->last == bitmap->max)
			bitmap->last = 0;
	} else {
		obj = EFA_BITMAP_INVAL;
	}

	if (obj != EFA_BITMAP_INVAL)
		--bitmap->avail;

	spin_unlock(&bitmap->lock);

	return obj;
}

void efa_bitmap_free(struct efa_bitmap *bitmap, u32 obj)
{
	obj &= bitmap->mask;

	spin_lock(&bitmap->lock);
	bitmap_clear(bitmap->table, obj, 1);
	bitmap->avail++;
	spin_unlock(&bitmap->lock);
}

u32 efa_bitmap_avail(struct efa_bitmap *bitmap)
{
	return bitmap->avail;
}

int efa_bitmap_init(struct efa_bitmap *bitmap, u32 num)
{
	/* num must be a power of 2 */
	if (!is_power_of_2(num))
		return -EINVAL;

	bitmap->last  = 0;
	bitmap->max   = num;
	bitmap->mask  = num - 1;
	bitmap->avail = num;
	spin_lock_init(&bitmap->lock);
	bitmap->table = kcalloc(BITS_TO_LONGS(bitmap->max),
				sizeof(long), GFP_KERNEL);
	if (!bitmap->table)
		return -ENOMEM;

	return 0;
}

void efa_bitmap_cleanup(struct efa_bitmap *bitmap)
{
	kfree(bitmap->table);
}

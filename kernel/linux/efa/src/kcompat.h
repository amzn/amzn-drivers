/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#include "config.h"

#if defined(HAVE_CREATE_AH_NO_UDATA) || !defined(HAVE_IB_QUERY_DEVICE_UDATA)
#define HAVE_CUSTOM_COMMANDS
#endif

#ifndef ALIGN_DOWN
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#endif

#ifndef HAVE_IB_IS_UDATA_CLEARED
#include <linux/string.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>

static inline bool ib_is_udata_cleared(struct ib_udata *udata,
				       size_t offset,
				       size_t len)
{
	const void __user *p = udata->inbuf + offset;
	bool ret = false;
	u8 *buf;

	if (len > USHRT_MAX)
		return false;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return false;

	if (copy_from_user(buf, p, len))
		goto free;

	ret = !memchr_inv(buf, 0, len);

free:
	kfree(buf);
	return ret;
}
#endif

#ifndef HAVE_IB_QPT_DRIVER
#define IB_QPT_DRIVER 0xFF
#endif

#if defined(HAVE_DRIVER_ID) && !defined(HAVE_UPSTREAM_EFA)
#define RDMA_DRIVER_EFA 17
#endif

#ifndef HAVE_IBDEV_PRINT
#define ibdev_err(_ibdev, format, arg...) \
	dev_err(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg(_ibdev, format, arg...) \
	dev_dbg(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn(_ibdev, format, arg...) \
	dev_warn(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info(_ibdev, format, arg...) \
	dev_info(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#ifndef HAVE_IBDEV_PRINT_RATELIMITED
#define ibdev_err_ratelimited(_ibdev, format, arg...) \
	dev_err_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg_ratelimited(_ibdev, format, arg...) \
	dev_dbg_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn_ratelimited(_ibdev, format, arg...) \
	dev_warn_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info_ratelimited(_ibdev, format, arg...) \
	dev_info_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#ifndef HAVE_KVZALLOC
#include <linux/slab.h>
#include <linux/vmalloc.h>

static inline void *kvzalloc(size_t size, gfp_t flags)
{
	void *addr;

	addr = kzalloc(size, flags | __GFP_NOWARN);
	if (addr)
		return addr;

	return vzalloc(size);
}
#endif

#ifndef HAVE_IB_PORT_PHYS_STATE_LINK_UP
#define IB_PORT_PHYS_STATE_LINK_UP 5
#endif

#ifndef HAVE_CORE_MMAP_XA
#include <linux/types.h>
#include <linux/device.h>

struct rdma_user_mmap_entry {
	struct ib_ucontext *ucontext;
	unsigned long start_pgoff;
	size_t npages;
};

/* Return the offset (in bytes) the user should pass to libc's mmap() */
static inline u64
rdma_user_mmap_get_offset(const struct rdma_user_mmap_entry *entry)
{
	return (u64)entry->start_pgoff << PAGE_SHIFT;
}

/*
 * Backported kernels don't keep refcnt on entries, hence they should not
 * be removed.
 */
static inline void
rdma_user_mmap_entry_remove(struct rdma_user_mmap_entry *entry)
{
}

static inline void rdma_user_mmap_entry_put(struct rdma_user_mmap_entry *entry)
{
}
#endif

#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#ifndef HAVE_BITFIELD_H
#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define FIELD_PREP(_mask, _val)                                         \
	({                                                              \
		((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask);   \
	})

#define FIELD_GET(_mask, _reg)                                          \
	({                                                              \
		(typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask)); \
	})
#endif

#ifndef HAVE_RDMA_NODE_UNSPECIFIED
enum {
	RDMA_NODE_UNSPECIFIED = 7,
};
#endif

#ifndef HAVE_ATOMIC64_FETCH_INC
static __always_inline s64
atomic64_fetch_inc(atomic64_t *v)
{
	return atomic64_inc_return(v) - 1;
}
#endif

#endif /* _KCOMPAT_H_ */

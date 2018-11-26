/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
#include <linux/utsrelease.h>
#else
#include <generated/utsrelease.h>
#endif

/******************************************************************************/
/**************************** RHEL macros *************************************/
/******************************************************************************/

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) (((a) << 8) + (b))
#endif

#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif

/*****************************************************************************/
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)) || \
	(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,4))))
#define HAVE_UMEM_SCATTERLIST_IF
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0) || \
	(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0)))
#define HAVE_GET_PORT_IMMUTABLE
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0) || \
	(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(7,0)))
#define HAVE_CREATE_AH_UDATA
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0) || \
	(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(7,0)))
#define HAVE_IB_QUERY_DEVICE_UDATA
#define HAVE_CREATE_CQ_ATTR
#endif

#if !defined(HAVE_CREATE_AH_UDATA) || !defined(HAVE_IB_QUERY_DEVICE_UDATA)
#define HAVE_CUSTOM_COMMANDS
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0) && \
	(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0))
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

#endif /* _KCOMPAT_H_ */

/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
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
/**************************** SuSE macros *************************************/
/******************************************************************************/

/* SuSE version macro is the same as Linux kernel version */
#ifndef SLE_VERSION
#define SLE_VERSION(a,b,c) KERNEL_VERSION(a,b,c)
#endif
#ifdef CONFIG_SUSE_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 14)
#include <linux/suse_version.h>
#define SLE_VERSION_CODE SLE_VERSION(SUSE_VERSION, SUSE_PATCHLEVEL, SUSE_AUXRELEASE)
#endif
#endif /* CONFIG_SUSE_KERNEL */
#ifndef SLE_VERSION_CODE
#define SLE_VERSION_CODE 0
#endif /* SLE_VERSION_CODE */
#ifndef SUSE_VERSION
#define SUSE_VERSION 0
#endif /* SUSE_VERSION */

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
/* Start of upstream defines */
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
#define HAVE_HW_STATS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,6))
#define HAVE_CREATE_AH_RDMA_ATTR
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,6))
#define HAVE_DEV_PARENT
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,7))
#define HAVE_DRIVER_ID
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,7)) || \
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(15, 1, 0))
#define HAVE_POST_CONST_WR
#define HAVE_MAX_SEND_RCV_SGE
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,7)) || \
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(15, 1, 0))
#define HAVE_IB_REGISTER_DEVICE_NAME_PARAM
#define HAVE_IB_MODIFY_QP_IS_OK_FOUR_PARAMS
#define HAVE_RDMA_USER_MMAP_IO
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define HAVE_IB_DEV_OPS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define HAVE_CREATE_DESTROY_AH_FLAGS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
#define HAVE_SG_DMA_PAGE_ITER
#define HAVE_PD_CORE_ALLOCATION
#define HAVE_UCONTEXT_CORE_ALLOCATION
#define HAVE_NO_KVERBS_DRIVERS
#define HAVE_IB_UMEM_GET_UDATA
#define HAVE_UDATA_TO_DRV_CONTEXT
#define HAVE_IB_REGISTER_DEVICE_TWO_PARAMS
#define HAVE_SAFE_IB_ALLOC_DEVICE
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
#define HAVE_AH_CORE_ALLOCATION
#define HAVE_ALLOC_PD_NO_UCONTEXT
#define HAVE_CREATE_CQ_NO_UCONTEXT
#define HAVE_DEALLOC_PD_UDATA
#define HAVE_DEREG_MR_UDATA
#define HAVE_DESTROY_CQ_UDATA
#define HAVE_DESTROY_QP_UDATA
#define HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
#define HAVE_UPSTREAM_EFA
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
#define HAVE_IB_DEVICE_OPS_COMMON
#define HAVE_IB_VOID_DESTROY_CQ
#define HAVE_CQ_CORE_ALLOCATION
#endif

/* End of upstream defines */

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

#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(4,16,0) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,6)) || \
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(15, 1, 0)))
#define IB_QPT_DRIVER 0xFF
#endif

#if defined(HAVE_DRIVER_ID) && !defined(HAVE_UPSTREAM_EFA)
#define RDMA_DRIVER_EFA 17
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
#define ibdev_err(_ibdev, format, arg...) \
	dev_err(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg(_ibdev, format, arg...) \
	dev_dbg(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn(_ibdev, format, arg...) \
	dev_warn(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info(_ibdev, format, arg...) \
	dev_info(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#define ibdev_err_ratelimited(_ibdev, format, arg...) \
	dev_err_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg_ratelimited(_ibdev, format, arg...) \
	dev_dbg_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn_ratelimited(_ibdev, format, arg...) \
	dev_warn_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info_ratelimited(_ibdev, format, arg...) \
	dev_info_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0) && \
	RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7, 6)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#define IB_PORT_PHYS_STATE_LINK_UP 5
#endif

#endif /* _KCOMPAT_H_ */

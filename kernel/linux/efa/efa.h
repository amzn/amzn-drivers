/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#ifndef _EFA_H_
#define _EFA_H_

#include "kcompat.h"
#include <linux/bitops.h>
#ifdef HAVE_CUSTOM_COMMANDS
#include <linux/cdev.h>
#endif
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/version.h>

#include <rdma/ib_verbs.h>

#define DRV_MODULE_NAME         "efa"
#define DEVICE_NAME             "Elastic Fabric Adapter (EFA)"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) DRV_MODULE_NAME ": %s: " fmt, __func__

#define EFA_IRQNAME_SIZE        40
#define EFA_GID_SIZE            16

/* 1 for AENQ + ADMIN */
#define EFA_NUM_MSIX_VEC                  1
#define EFA_MGMNT_MSIX_VEC_IDX            0

#define EFA_BITMAP_INVAL U32_MAX

enum {
	EFA_DEVICE_RUNNING_BIT,
	EFA_MSIX_ENABLED_BIT
};

struct efa_caps {
	u32 max_sq;
	u32 max_sq_depth; /* wqes */
	u32 max_rq;
	u32 max_rq_depth; /* wqes */
	u32 max_cq;
	u32 max_cq_depth; /* cqes */
	u32 inline_buf_size;
	u32 max_sq_sge;
	u32 max_rq_sge;
	u32 max_mr;
	u64 max_mr_pages;
	u64 page_size_cap; /* bytes */
	u32 max_pd;
	u32 max_ah;
	u16 sub_cqs_per_cq;
	u16 max_inline_data; /* bytes */
};

struct efa_bitmap {
	u32                     last;
	u32                     max;
	u32                     mask;
	u32                     avail;
	/* Protects bitmap */
	spinlock_t              lock;
	unsigned long          *table;
};

struct efa_irq {
	irq_handler_t handler;
	void *data;
	int cpu;
	u32 vector;
	cpumask_t affinity_hint_mask;
	char name[EFA_IRQNAME_SIZE];
};

struct efa_sw_stats {
	u64 alloc_pd_alloc_err;
	u64 alloc_pd_bitmap_full_err;
	u64 mmap_entry_alloc_err;
	u64 create_qp_alloc_err;
	u64 create_cq_alloc_err;
	u64 reg_mr_alloc_err;
	u64 alloc_ucontext_alloc_err;
	u64 create_ah_alloc_err;
};

struct efa_stats {
	struct efa_sw_stats sw_stats;
	u64                 keep_alive_rcvd;
};

struct efa_dev {
	struct ib_device        ibdev;
	struct pci_dev         *pdev;
	struct efa_com_dev     *edev;
	struct efa_caps         caps;

	u64                     reg_bar_addr;
	u64                     reg_bar_len;
	u64                     mem_bar_addr;
	u64                     mem_bar_len;
	u64                     db_bar_addr;
	u64                     db_bar_len;
	u8                      addr[EFA_GID_SIZE];
	u32                     mtu;
	u8                      db_bar_idx;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	struct msix_entry       admin_msix_entry;
#else
	int                     admin_msix_vector_idx;
#endif
	unsigned long           state;
	struct efa_irq          admin_irq;

	struct list_head        ctx_list;

	/* Protects efa_dev state */
	struct mutex            efa_dev_lock;

	struct list_head        efa_ah_list;
	/* Protects efa_ah_list */
	struct mutex            ah_list_lock;
	struct efa_bitmap       pd_bitmap;
#ifdef HAVE_CUSTOM_COMMANDS
	struct device          *everbs_dev;
	struct cdev             cdev;
#endif

	struct efa_stats        stats;
};

u32 efa_bitmap_alloc(struct efa_bitmap *bitmap);
void efa_bitmap_free(struct efa_bitmap *bitmap, u32 obj);
int efa_bitmap_init(struct efa_bitmap *bitmap, u32 num);
void efa_bitmap_cleanup(struct efa_bitmap *bitmap);
u32 efa_bitmap_avail(struct efa_bitmap *bitmap);

#endif /* _EFA_H_ */

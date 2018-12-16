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
#include <linux/fs.h>
#endif
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/version.h>

#include <rdma/ib_verbs.h>

#include "efa_com_cmd.h"

#define DRV_MODULE_NAME         "efa"
#define DEVICE_NAME             "Elastic Fabric Adapter (EFA)"

#define EFA_IRQNAME_SIZE        40

/* 1 for AENQ + ADMIN */
#define EFA_NUM_MSIX_VEC                  1
#define EFA_MGMNT_MSIX_VEC_IDX            0

#define efa_stat_inc(dev, stat)                                         \
	do {                                                            \
		typeof(dev) _dev = dev;                                 \
		unsigned long flags;                                    \
									\
		spin_lock_irqsave(&_dev->stats_lock, flags);            \
		(stat)++;                                               \
		spin_unlock_irqrestore(&_dev->stats_lock, flags);       \
	} while (0)

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
	u64 alloc_pd_ida_full_err;
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
	struct ida              pd_ida;
#ifdef HAVE_CUSTOM_COMMANDS
	struct device          *everbs_dev;
	struct cdev             cdev;
#endif

	struct efa_stats        stats;
	spinlock_t              stats_lock; /* Protects stats */
};

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props,
		     struct ib_udata *udata);
#else
int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props);
#endif
int efa_query_port(struct ib_device *ibdev, u8 port,
		   struct ib_port_attr *props);
int efa_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask,
		 struct ib_qp_init_attr *qp_init_attr);
int efa_query_gid(struct ib_device *ibdev, u8 port, int index,
		  union ib_gid *gid);
int efa_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
		   u16 *pkey);
struct ib_pd *efa_alloc_pd(struct ib_device *ibdev,
			   struct ib_ucontext *ibucontext,
			   struct ib_udata *udata);
int efa_dealloc_pd(struct ib_pd *ibpd);
int efa_destroy_qp_handle(struct efa_dev *dev, u32 qp_handle);
int efa_destroy_qp(struct ib_qp *ibqp);
struct ib_qp *efa_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata);
int efa_destroy_cq(struct ib_cq *ibcq);
#ifdef HAVE_CREATE_CQ_ATTR
struct ib_cq *efa_create_cq(struct ib_device *ibdev,
			    const struct ib_cq_init_attr *attr,
			    struct ib_ucontext *ibucontext,
			    struct ib_udata *udata);
#else
struct ib_cq *efa_create_cq(struct ib_device *ibdev, int entries,
			    int vector,
			    struct ib_ucontext *ibucontext,
			    struct ib_udata *udata);
#endif
struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
			 u64 virt_addr, int access_flags,
			 struct ib_udata *udata);
int efa_dereg_mr(struct ib_mr *ibmr);
#ifdef HAVE_GET_PORT_IMMUTABLE
int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num,
			   struct ib_port_immutable *immutable);
#endif
struct ib_ucontext *efa_alloc_ucontext(struct ib_device *ibdev,
				       struct ib_udata *udata);
int efa_dealloc_ucontext(struct ib_ucontext *ibucontext);
int efa_mmap(struct ib_ucontext *ibucontext,
	     struct vm_area_struct *vma);
#ifdef HAVE_CREATE_AH_UDATA
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
			    struct rdma_ah_attr *ah_attr,
			    struct ib_udata *udata);
#else
struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
			    struct ib_ah_attr *ah_attr,
			    struct ib_udata *udata);
#endif
#else
struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
			    struct ib_ah_attr *ah_attr);
#endif
int efa_destroy_ah(struct ib_ah *ibah);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
int efa_post_send(struct ib_qp *ibqp,
		  struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr);
#else
int efa_post_send(struct ib_qp *ibqp,
		  const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
int efa_post_recv(struct ib_qp *ibqp,
		  struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr);
#else
int efa_post_recv(struct ib_qp *ibqp,
		  const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr);
#endif
int efa_poll_cq(struct ib_cq *ibcq, int num_entries,
		struct ib_wc *wc);
int efa_req_notify_cq(struct ib_cq *ibcq,
		      enum ib_cq_notify_flags flags);
struct ib_mr *efa_get_dma_mr(struct ib_pd *ibpd, int acc);
int efa_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata);
enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
					 u8 port_num);

#ifdef HAVE_CUSTOM_COMMANDS
#ifndef HAVE_CREATE_AH_UDATA
ssize_t efa_everbs_cmd_get_ah(struct efa_dev *dev,
			      const char __user *buf,
			      int in_len,
			      int out_len);
#endif
#ifndef HAVE_IB_QUERY_DEVICE_UDATA
ssize_t efa_everbs_cmd_get_ex_dev_attrs(struct efa_dev *dev,
					const char __user *buf,
					int in_len,
					int out_len);
#endif
#endif

#endif /* _EFA_H_ */

/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_H_
#define _EFA_H_

#include "kcompat.h"
#include <linux/bitops.h>
#ifdef HAVE_CUSTOM_COMMANDS
#include <linux/cdev.h>
#include <linux/fs.h>
#endif
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/version.h>

#include <rdma/ib_verbs.h>

#include "efa-abi.h"
#include "efa_com_cmd.h"

#define DRV_MODULE_NAME         "efa"
#define DEVICE_NAME             "Elastic Fabric Adapter (EFA)"

#define EFA_IRQNAME_SIZE        40

/* 1 for AENQ + ADMIN */
#define EFA_NUM_MSIX_VEC                  1
#define EFA_MGMNT_MSIX_VEC_IDX            0

struct efa_irq {
	irq_handler_t handler;
	void *data;
	int cpu;
	u32 vector;
	cpumask_t affinity_hint_mask;
	char name[EFA_IRQNAME_SIZE];
};

struct efa_sw_stats {
	atomic64_t alloc_pd_err;
	atomic64_t create_qp_err;
	atomic64_t create_cq_err;
	atomic64_t reg_mr_err;
	atomic64_t alloc_ucontext_err;
	atomic64_t create_ah_err;
};

/* Don't use anything other than atomic64 */
struct efa_stats {
	struct efa_sw_stats sw_stats;
	atomic64_t keep_alive_rcvd;
};

struct efa_dev {
	struct ib_device ibdev;
	struct efa_com_dev edev;
	struct pci_dev *pdev;
	struct efa_com_get_device_attr_result dev_attr;

	u64 reg_bar_addr;
	u64 reg_bar_len;
	u64 mem_bar_addr;
	u64 mem_bar_len;
	u64 db_bar_addr;
	u64 db_bar_len;
	u8 addr[EFA_GID_SIZE];
	u32 mtu;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	struct msix_entry admin_msix_entry;
#else
	int admin_msix_vector_idx;
#endif
	struct efa_irq admin_irq;

#ifndef HAVE_CREATE_AH_UDATA
	struct list_head efa_ah_list;
	/* Protects efa_ah_list */
	struct mutex ah_list_lock;
#endif
#ifdef HAVE_CUSTOM_COMMANDS
	struct device *everbs_dev;
	struct cdev cdev;
#endif

	struct efa_stats stats;
};

struct efa_ucontext {
	struct ib_ucontext ibucontext;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	struct xarray mmap_xa;
#else
	/* Protects ucontext state */
	struct mutex lock;
	struct list_head pending_mmaps;
#endif
	u32 mmap_xa_page;
	u16 uarn;
};

struct efa_pd {
	struct ib_pd ibpd;
	u16 pdn;
};

struct efa_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
};

struct efa_cq {
	struct ib_cq ibcq;
	struct efa_ucontext *ucontext;
	dma_addr_t dma_addr;
	void *cpu_addr;
	size_t size;
	u16 cq_idx;
};

struct efa_qp {
	struct ib_qp ibqp;
	dma_addr_t rq_dma_addr;
	void *rq_cpu_addr;
	size_t rq_size;
	enum ib_qp_state state;
	u32 qp_handle;
	u32 max_send_wr;
	u32 max_recv_wr;
	u32 max_send_sge;
	u32 max_recv_sge;
	u32 max_inline_data;
};

struct efa_ah {
	struct ib_ah ibah;
	u16 ah;
	/* dest_addr */
	u8 id[EFA_GID_SIZE];
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
#ifdef HAVE_ALLOC_PD_NO_UCONTEXT
int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
#else
int efa_alloc_pd(struct ib_pd *ibpd,
		 struct ib_ucontext *ibucontext,
		 struct ib_udata *udata);
#endif
#ifdef HAVE_DEALLOC_PD_UDATA
void efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
#elif defined(HAVE_PD_CORE_ALLOCATION)
void efa_dealloc_pd(struct ib_pd *ibpd);
#else
int efa_dealloc_pd(struct ib_pd *ibpd);
struct ib_pd *efa_kzalloc_pd(struct ib_device *ibdev,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata);
#endif
#ifdef HAVE_DESTROY_QP_UDATA
int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);
#else
int efa_destroy_qp(struct ib_qp *ibqp);
#endif
struct ib_qp *efa_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata);
#ifdef HAVE_IB_VOID_DESTROY_CQ
void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
#elif defined(HAVE_DESTROY_CQ_UDATA)
int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
#else
int efa_destroy_cq(struct ib_cq *ibcq);
#endif
#ifdef HAVE_CREATE_CQ_ATTR
int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata);
#else
int efa_create_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata);
#endif
#ifndef HAVE_CQ_CORE_ALLOCATION
#ifdef HAVE_CREATE_CQ_NO_UCONTEXT
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_udata *udata);
#elif defined(HAVE_CREATE_CQ_ATTR)
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata);
#else
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev, int entries,
			     int vector,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata);
#endif
#endif
struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
			 u64 virt_addr, int access_flags,
			 struct ib_udata *udata);
#ifdef HAVE_DEREG_MR_UDATA
int efa_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);
#else
int efa_dereg_mr(struct ib_mr *ibmr);
#endif
#ifdef HAVE_GET_PORT_IMMUTABLE
int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num,
			   struct ib_port_immutable *immutable);
#endif
int efa_alloc_ucontext(struct ib_ucontext *ibucontext, struct ib_udata *udata);
#ifdef HAVE_UCONTEXT_CORE_ALLOCATION
void efa_dealloc_ucontext(struct ib_ucontext *ibucontext);
#else
int efa_dealloc_ucontext(struct ib_ucontext *ibucontext);
struct ib_ucontext *efa_kzalloc_ucontext(struct ib_device *ibdev,
					 struct ib_udata *udata);
#endif
int efa_mmap(struct ib_ucontext *ibucontext,
	     struct vm_area_struct *vma);
int efa_create_ah(struct ib_ah *ibah,
#ifdef HAVE_CREATE_AH_RDMA_ATTR
		  struct rdma_ah_attr *ah_attr,
#else
		  struct ib_ah_attr *ah_attr,
#endif
		  u32 flags,
		  struct ib_udata *udata);
#ifndef HAVE_AH_CORE_ALLOCATION
#ifdef HAVE_CREATE_DESTROY_AH_FLAGS
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct rdma_ah_attr *ah_attr,
			     u32 flags,
			     struct ib_udata *udata);
#elif defined(HAVE_CREATE_AH_RDMA_ATTR)
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct rdma_ah_attr *ah_attr,
			     struct ib_udata *udata);
#elif defined(HAVE_CREATE_AH_UDATA)
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct ib_ah_attr *ah_attr,
			     struct ib_udata *udata);
#else
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct ib_ah_attr *ah_attr);
#endif
#endif
#ifdef HAVE_AH_CORE_ALLOCATION
void efa_destroy_ah(struct ib_ah *ibah, u32 flags);
#elif defined(HAVE_CREATE_DESTROY_AH_FLAGS)
int efa_destroy_ah(struct ib_ah *ibah, u32 flags);
#else
int efa_destroy_ah(struct ib_ah *ibah);
#endif
#ifndef HAVE_NO_KVERBS_DRIVERS
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
#endif
int efa_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_udata *udata);
enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
					 u8 port_num);
#ifdef HAVE_HW_STATS
struct rdma_hw_stats *efa_alloc_hw_stats(struct ib_device *ibdev, u8 port_num);
int efa_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		     u8 port_num, int index);
#endif

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

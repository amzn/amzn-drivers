/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_H_
#define _EFA_H_

#include "kcompat.h"
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/version.h>

#include <rdma/ib_verbs.h>

#include "efa-abi.h"
#include "efa_com_cmd.h"

#define DRV_MODULE_NAME         "efa"
#define DEVICE_NAME             "Elastic Fabric Adapter (EFA)"

#define EFA_IRQNAME_SIZE        40

#define EFA_MGMNT_MSIX_VEC_IDX            0
#define EFA_COMP_EQS_VEC_BASE             1

struct efa_irq {
	irq_handler_t handler;
	void *data;
	u32 irqn;
	u32 vector;
	cpumask_t affinity_hint_mask;
	char name[EFA_IRQNAME_SIZE];
};

/* Don't use anything other than atomic64 */
struct efa_stats {
	atomic64_t alloc_pd_err;
	atomic64_t create_qp_err;
	atomic64_t create_cq_err;
	atomic64_t reg_mr_err;
	atomic64_t alloc_ucontext_err;
	atomic64_t create_ah_err;
	atomic64_t mmap_err;
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

	int admin_msix_vector_idx;
	struct efa_irq admin_irq;

	struct efa_stats stats;

	/* Array of completion EQs */
	struct efa_eq *eqs;
	unsigned int neqs;

#ifdef HAVE_XARRAY
	/* Only stores CQs with interrupts enabled */
	struct xarray cqs_xa;
#else
	/* If xarray isn't available keep an array of all possible CQs */
	struct efa_cq *cqs_arr[BIT(sizeof_field(struct efa_admin_create_cq_resp,
						cq_idx) * 8)];
#endif
};

struct efa_ucontext {
	struct ib_ucontext ibucontext;
	u16 uarn;
#ifndef HAVE_CORE_MMAP_XA
	/* Protects ucontext state */
	struct mutex lock;
	struct list_head pending_mmaps;
	u32 mmap_page;
#endif /* !defined(HAVE_CORE_MMAP_XA) */
};

struct efa_pd {
	struct ib_pd ibpd;
	u16 pdn;
};

struct efa_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
#ifdef HAVE_EFA_GDR
	struct efa_nvmem *nvmem;
	u64 nvmem_ticket;
#endif
};

struct efa_cq {
	struct ib_cq ibcq;
	struct efa_ucontext *ucontext;
	dma_addr_t dma_addr;
	void *cpu_addr;
	struct rdma_user_mmap_entry *mmap_entry;
	struct rdma_user_mmap_entry *db_mmap_entry;
	size_t size;
	u16 cq_idx;
	/* NULL when no interrupts requested */
	struct efa_eq *eq;
};

struct efa_qp {
	struct ib_qp ibqp;
	dma_addr_t rq_dma_addr;
	void *rq_cpu_addr;
	size_t rq_size;
	enum ib_qp_state state;

	/* Used for saving mmap_xa entries */
	struct rdma_user_mmap_entry *sq_db_mmap_entry;
	struct rdma_user_mmap_entry *llq_desc_mmap_entry;
	struct rdma_user_mmap_entry *rq_db_mmap_entry;
	struct rdma_user_mmap_entry *rq_mmap_entry;

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

struct efa_eq {
	struct efa_com_eq eeq;
	struct efa_irq irq;
};

int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props,
		     struct ib_udata *udata);
int efa_query_port(struct ib_device *ibdev, port_t port,
		   struct ib_port_attr *props);
int efa_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask,
		 struct ib_qp_init_attr *qp_init_attr);
int efa_query_gid(struct ib_device *ibdev, port_t port, int index,
		  union ib_gid *gid);
int efa_query_pkey(struct ib_device *ibdev, port_t port, u16 index,
		   u16 *pkey);
#ifdef HAVE_ALLOC_PD_NO_UCONTEXT
int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
#else
int efa_alloc_pd(struct ib_pd *ibpd,
		 struct ib_ucontext *ibucontext,
		 struct ib_udata *udata);
#endif
#ifdef HAVE_DEALLOC_PD_UDATA_RC
int efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
#elif defined(HAVE_DEALLOC_PD_UDATA)
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
#ifdef HAVE_QP_CORE_ALLOCATION
int efa_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *init_attr,
		  struct ib_udata *udata);
#else
struct ib_qp *efa_kzalloc_qp(struct ib_pd *ibpd,
			     struct ib_qp_init_attr *init_attr,
			     struct ib_udata *udata);
#endif
#ifdef HAVE_IB_INT_DESTROY_CQ
int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
#elif defined(HAVE_IB_VOID_DESTROY_CQ)
void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
#elif defined(HAVE_DESTROY_CQ_UDATA)
int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
#else
int efa_destroy_cq(struct ib_cq *ibcq);
#endif
int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata);
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
int efa_get_port_immutable(struct ib_device *ibdev, port_t port_num,
			   struct ib_port_immutable *immutable);
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
#ifdef HAVE_CORE_MMAP_XA
void efa_mmap_free(struct rdma_user_mmap_entry *rdma_entry);
#endif
int efa_create_ah(struct ib_ah *ibah,
#ifdef HAVE_CREATE_AH_INIT_ATTR
		  struct rdma_ah_init_attr *init_attr,
#else
		  struct rdma_ah_attr *ah_attr,
		  u32 flags,
#endif
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
#endif
#endif
#ifdef HAVE_AH_CORE_ALLOCATION_DESTROY_RC
int efa_destroy_ah(struct ib_ah *ibah, u32 flags);
#elif defined(HAVE_AH_CORE_ALLOCATION)
void efa_destroy_ah(struct ib_ah *ibah, u32 flags);
#elif defined(HAVE_CREATE_DESTROY_AH_FLAGS)
int efa_destroy_ah(struct ib_ah *ibah, u32 flags);
#else
int efa_destroy_ah(struct ib_ah *ibah);
#endif
#ifndef HAVE_NO_KVERBS_DRIVERS
#ifdef HAVE_POST_CONST_WR
int efa_post_send(struct ib_qp *ibqp,
		  const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr);
#else
int efa_post_send(struct ib_qp *ibqp,
		  struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr);
#endif
#ifdef HAVE_POST_CONST_WR
int efa_post_recv(struct ib_qp *ibqp,
		  const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr);
#else
int efa_post_recv(struct ib_qp *ibqp,
		  struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr);
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
					 port_t port_num);
#ifdef HAVE_SPLIT_STATS_ALLOC
struct rdma_hw_stats *efa_alloc_hw_port_stats(struct ib_device *ibdev, port_t port_num);
struct rdma_hw_stats *efa_alloc_hw_device_stats(struct ib_device *ibdev);
#else
struct rdma_hw_stats *efa_alloc_hw_stats(struct ib_device *ibdev, port_t port_num);
#endif
int efa_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		     port_t port_num, int index);

#endif /* _EFA_H_ */

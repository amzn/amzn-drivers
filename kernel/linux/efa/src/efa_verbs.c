// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2018-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "kcompat.h"
#include <linux/vmalloc.h>

#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#ifdef HAVE_UDATA_TO_DRV_CONTEXT
#include <rdma/uverbs_ioctl.h>
#endif

#include "efa.h"

#ifdef HAVE_EFA_GDR
#include "efa_gdr.h"
#endif

enum {
	EFA_MMAP_DMA_PAGE = 0,
	EFA_MMAP_IO_WC,
	EFA_MMAP_IO_NC,
};

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

struct efa_user_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
#ifndef HAVE_CORE_MMAP_XA
	struct list_head list;
#endif
	u64 address;
	u8 mmap_flag;
};

#ifdef HAVE_HW_STATS
#define EFA_DEFINE_STATS(op) \
	op(EFA_TX_BYTES, "tx_bytes") \
	op(EFA_TX_PKTS, "tx_pkts") \
	op(EFA_RX_BYTES, "rx_bytes") \
	op(EFA_RX_PKTS, "rx_pkts") \
	op(EFA_RX_DROPS, "rx_drops") \
	op(EFA_SUBMITTED_CMDS, "submitted_cmds") \
	op(EFA_COMPLETED_CMDS, "completed_cmds") \
	op(EFA_CMDS_ERR, "cmds_err") \
	op(EFA_NO_COMPLETION_CMDS, "no_completion_cmds") \
	op(EFA_KEEP_ALIVE_RCVD, "keep_alive_rcvd") \
	op(EFA_ALLOC_PD_ERR, "alloc_pd_err") \
	op(EFA_CREATE_QP_ERR, "create_qp_err") \
	op(EFA_CREATE_CQ_ERR, "create_cq_err") \
	op(EFA_REG_MR_ERR, "reg_mr_err") \
	op(EFA_ALLOC_UCONTEXT_ERR, "alloc_ucontext_err") \
	op(EFA_CREATE_AH_ERR, "create_ah_err") \
	op(EFA_MMAP_ERR, "mmap_err")

#define EFA_STATS_ENUM(ename, name) ename,
#define EFA_STATS_STR(ename, name) [ename] = name,

enum efa_hw_stats {
	EFA_DEFINE_STATS(EFA_STATS_ENUM)
};

static const char *const efa_stats_names[] = {
	EFA_DEFINE_STATS(EFA_STATS_STR)
};
#endif

#define EFA_CHUNK_PAYLOAD_SHIFT       12
#define EFA_CHUNK_PAYLOAD_SIZE        BIT(EFA_CHUNK_PAYLOAD_SHIFT)
#define EFA_CHUNK_PAYLOAD_PTR_SIZE    8

#define EFA_CHUNK_SHIFT               12
#define EFA_CHUNK_SIZE                BIT(EFA_CHUNK_SHIFT)
#define EFA_CHUNK_PTR_SIZE            sizeof(struct efa_com_ctrl_buff_info)

#define EFA_PTRS_PER_CHUNK \
	((EFA_CHUNK_SIZE - EFA_CHUNK_PTR_SIZE) / EFA_CHUNK_PAYLOAD_PTR_SIZE)

#define EFA_CHUNK_USED_SIZE \
	((EFA_PTRS_PER_CHUNK * EFA_CHUNK_PAYLOAD_PTR_SIZE) + EFA_CHUNK_PTR_SIZE)

struct pbl_chunk {
	dma_addr_t dma_addr;
	u64 *buf;
	u32 length;
};

struct pbl_chunk_list {
	struct pbl_chunk *chunks;
	unsigned int size;
};

struct pbl_context {
	union {
		struct {
			dma_addr_t dma_addr;
		} continuous;
		struct {
			u32 pbl_buf_size_in_pages;
			struct scatterlist *sgl;
			int sg_dma_cnt;
			struct pbl_chunk_list chunk_list;
		} indirect;
	} phys;
	u64 *pbl_buf;
	u32 pbl_buf_size_in_bytes;
	u8 physically_continuous;
};

static inline struct efa_dev *to_edev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct efa_dev, ibdev);
}

static inline struct efa_ucontext *to_eucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct efa_ucontext, ibucontext);
}

static inline struct efa_pd *to_epd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct efa_pd, ibpd);
}

static inline struct efa_mr *to_emr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct efa_mr, ibmr);
}

static inline struct efa_qp *to_eqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct efa_qp, ibqp);
}

static inline struct efa_cq *to_ecq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct efa_cq, ibcq);
}

static inline struct efa_ah *to_eah(struct ib_ah *ibah)
{
	return container_of(ibah, struct efa_ah, ibah);
}

static inline struct efa_user_mmap_entry *
to_emmap(struct rdma_user_mmap_entry *rdma_entry)
{
	return container_of(rdma_entry, struct efa_user_mmap_entry, rdma_entry);
}

static inline bool is_rdma_read_cap(struct efa_dev *dev)
{
	return dev->dev_attr.device_caps & EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_RDMA_READ_MASK;
}

#define is_reserved_cleared(reserved) \
	!memchr_inv(reserved, 0, sizeof(reserved))

static void *efa_zalloc_mapped(struct efa_dev *dev, dma_addr_t *dma_addr,
			       size_t size, enum dma_data_direction dir)
{
	void *addr;

	addr = alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
	if (!addr)
		return NULL;

	*dma_addr = dma_map_single(&dev->pdev->dev, addr, size, dir);
	if (dma_mapping_error(&dev->pdev->dev, *dma_addr)) {
		ibdev_err(&dev->ibdev, "Failed to map DMA address\n");
		free_pages_exact(addr, size);
		return NULL;
	}

	return addr;
}

static void efa_free_mapped(struct efa_dev *dev, void *cpu_addr,
			    dma_addr_t dma_addr,
			    size_t size, enum dma_data_direction dir)
{
	dma_unmap_single(&dev->pdev->dev, dma_addr, size, dir);
	free_pages_exact(cpu_addr, size);
}

#ifndef HAVE_CORE_MMAP_XA
/*
 * This is only called when the ucontext is destroyed and there can be no
 * concurrent query via mmap or allocate on the database, thus we can be sure no
 * other thread is using the entry pointer. We also know that all the BAR
 * pages have either been zap'd or munmaped at this point.  Normal pages are
 * refcounted and will be freed at the proper time.
 */
static void mmap_entries_remove_free(struct efa_dev *dev,
				     struct efa_ucontext *ucontext)
{
	struct efa_user_mmap_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &ucontext->pending_mmaps, list) {
		list_del(&entry->list);
		ibdev_dbg(
			&dev->ibdev,
			"mmap: key[%#llx] addr[%#llx] len[%#zx] removed\n",
			rdma_user_mmap_get_offset(&entry->rdma_entry),
			entry->address, entry->rdma_entry.npages * PAGE_SIZE);
		kfree(entry);
	}
}

static int mmap_entry_validate(struct efa_ucontext *ucontext,
			       struct vm_area_struct *vma)
{
	size_t length = vma->vm_end - vma->vm_start;

	if (length % PAGE_SIZE != 0 || !(vma->vm_flags & VM_SHARED)) {
		ibdev_dbg(ucontext->ibucontext.device,
			  "length[%#zx] is not page size aligned[%#lx] or VM_SHARED is not set [%#lx]\n",
			  length, PAGE_SIZE, vma->vm_flags);
		return -EINVAL;
	}

	if (vma->vm_flags & VM_EXEC) {
		ibdev_dbg(ucontext->ibucontext.device,
			  "Mapping executable pages is not permitted\n");
		return -EPERM;
	}

	return 0;
}

struct rdma_user_mmap_entry *
rdma_user_mmap_entry_get(struct ib_ucontext *ibucontext,
			 struct vm_area_struct *vma)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	size_t length = vma->vm_end - vma->vm_start;
	struct efa_user_mmap_entry *entry, *tmp;
	u64 key = vma->vm_pgoff << PAGE_SHIFT;
	int err;

	err = mmap_entry_validate(ucontext, vma);
	if (err)
		return NULL;

	mutex_lock(&ucontext->lock);
	list_for_each_entry_safe(entry, tmp, &ucontext->pending_mmaps, list) {
		if (rdma_user_mmap_get_offset(&entry->rdma_entry) == key &&
		    entry->rdma_entry.npages * PAGE_SIZE == length) {
			ibdev_dbg(ibucontext->device,
				  "mmap: key[%#llx] addr[%#llx] len[%#zx] removed\n",
				  key, entry->address,
				  entry->rdma_entry.npages * PAGE_SIZE);
			mutex_unlock(&ucontext->lock);
			return &entry->rdma_entry;
		}
	}
	mutex_unlock(&ucontext->lock);

	return NULL;
}
#endif /* !defined (HAVE_CORE_MMAP_XA) */

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props,
		     struct ib_udata *udata)
#else
int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props)
#endif
{
	struct efa_com_get_device_attr_result *dev_attr;
#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	struct efa_ibv_ex_query_device_resp resp = {};
#endif
	struct efa_dev *dev = to_edev(ibdev);
#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	int err;

	if (udata && udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		return -EINVAL;
	}
#endif

	dev_attr = &dev->dev_attr;

	memset(props, 0, sizeof(*props));
	props->max_mr_size = dev_attr->max_mr_pages * PAGE_SIZE;
	props->page_size_cap = dev_attr->page_size_cap;
	props->vendor_id = dev->pdev->vendor;
	props->vendor_part_id = dev->pdev->device;
	props->hw_ver = dev->pdev->subsystem_device;
	props->max_qp = dev_attr->max_qp;
	props->max_cq = dev_attr->max_cq;
	props->max_pd = dev_attr->max_pd;
	props->max_mr = dev_attr->max_mr;
	props->max_ah = dev_attr->max_ah;
	props->max_cqe = dev_attr->max_cq_depth;
	props->max_qp_wr = min_t(u32, dev_attr->max_sq_depth,
				 dev_attr->max_rq_depth);
#ifdef HAVE_MAX_SEND_RCV_SGE
	props->max_send_sge = dev_attr->max_sq_sge;
	props->max_recv_sge = dev_attr->max_rq_sge;
#else
	props->max_sge = min_t(u16, dev_attr->max_sq_sge,
			       dev_attr->max_rq_sge);
#endif
	props->max_sge_rd = dev_attr->max_wr_rdma_sge;
	props->max_pkeys = 1;

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	if (udata && udata->outlen) {
		resp.max_sq_sge = dev_attr->max_sq_sge;
		resp.max_rq_sge = dev_attr->max_rq_sge;
		resp.max_sq_wr = dev_attr->max_sq_depth;
		resp.max_rq_wr = dev_attr->max_rq_depth;
		resp.max_rdma_size = dev_attr->max_rdma_size;

		if (is_rdma_read_cap(dev))
			resp.device_caps |= EFA_QUERY_DEVICE_CAPS_RDMA_READ;

		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(ibdev,
				  "Failed to copy udata for query_device\n");
			return err;
		}
	}
#endif

	return 0;
}

int efa_query_port(struct ib_device *ibdev, u8 port,
		   struct ib_port_attr *props)
{
	struct efa_dev *dev = to_edev(ibdev);

	props->lmc = 1;

	props->state = IB_PORT_ACTIVE;
	props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->active_speed = IB_SPEED_EDR;
	props->active_width = IB_WIDTH_4X;
#ifdef HAVE_IB_MTU_INT_TO_ENUM
	props->max_mtu = ib_mtu_int_to_enum(dev->dev_attr.mtu);
	props->active_mtu = ib_mtu_int_to_enum(dev->dev_attr.mtu);
#else
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = IB_MTU_4096;
#endif
	props->max_msg_sz = dev->dev_attr.mtu;
	props->max_vl_num = 1;

	return 0;
}

int efa_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask,
		 struct ib_qp_init_attr *qp_init_attr)
{
	struct efa_dev *dev = to_edev(ibqp->device);
	struct efa_com_query_qp_params params = {};
	struct efa_com_query_qp_result result;
	struct efa_qp *qp = to_eqp(ibqp);
	int err;

#define EFA_QUERY_QP_SUPP_MASK \
	(IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT | \
	 IB_QP_QKEY | IB_QP_SQ_PSN | IB_QP_CAP)

	if (qp_attr_mask & ~EFA_QUERY_QP_SUPP_MASK) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp_attr_mask[%#x] supported[%#x]\n",
			  qp_attr_mask, EFA_QUERY_QP_SUPP_MASK);
		return -EOPNOTSUPP;
	}

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	params.qp_handle = qp->qp_handle;
	err = efa_com_query_qp(&dev->edev, &params, &result);
	if (err)
		return err;

	qp_attr->qp_state = result.qp_state;
	qp_attr->qkey = result.qkey;
	qp_attr->sq_psn = result.sq_psn;
	qp_attr->sq_draining = result.sq_draining;
	qp_attr->port_num = 1;

	qp_attr->cap.max_send_wr = qp->max_send_wr;
	qp_attr->cap.max_recv_wr = qp->max_recv_wr;
	qp_attr->cap.max_send_sge = qp->max_send_sge;
	qp_attr->cap.max_recv_sge = qp->max_recv_sge;
	qp_attr->cap.max_inline_data = qp->max_inline_data;

	qp_init_attr->qp_type = ibqp->qp_type;
	qp_init_attr->recv_cq = ibqp->recv_cq;
	qp_init_attr->send_cq = ibqp->send_cq;
	qp_init_attr->qp_context = ibqp->qp_context;
	qp_init_attr->cap = qp_attr->cap;

	return 0;
}

int efa_query_gid(struct ib_device *ibdev, u8 port, int index,
		  union ib_gid *gid)
{
	struct efa_dev *dev = to_edev(ibdev);

	memcpy(gid->raw, dev->dev_attr.addr, sizeof(dev->dev_attr.addr));

	return 0;
}

int efa_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
		   u16 *pkey)
{
	if (index > 0)
		return -EINVAL;

	*pkey = 0xffff;
	return 0;
}

static int efa_pd_dealloc(struct efa_dev *dev, u16 pdn)
{
	struct efa_com_dealloc_pd_params params = {
		.pdn = pdn,
	};

	return efa_com_dealloc_pd(&dev->edev, &params);
}

#ifdef HAVE_ALLOC_PD_NO_UCONTEXT
int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
#else
int efa_alloc_pd(struct ib_pd *ibpd,
		 struct ib_ucontext *ibucontext,
		 struct ib_udata *udata)
#endif
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_ibv_alloc_pd_resp resp = {};
	struct efa_com_alloc_pd_result result;
	struct efa_pd *pd = to_epd(ibpd);
	int err;

#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(&dev->ibdev, "udata is NULL\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

	if (udata->inlen &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, udata->inlen - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		err = -EINVAL;
		goto err_out;
	}

	err = efa_com_alloc_pd(&dev->edev, &result);
	if (err)
		goto err_out;

	pd->pdn = result.pdn;
	resp.pdn = result.pdn;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for alloc_pd\n");
			goto err_dealloc_pd;
		}
	}

	ibdev_dbg(&dev->ibdev, "Allocated pd[%d]\n", pd->pdn);

	return 0;

err_dealloc_pd:
	efa_pd_dealloc(dev, result.pdn);
err_out:
	atomic64_inc(&dev->stats.sw_stats.alloc_pd_err);
	return err;
}

#ifndef HAVE_PD_CORE_ALLOCATION
struct ib_pd *efa_kzalloc_pd(struct ib_device *ibdev,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_pd *pd;
	int err;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		atomic64_inc(&dev->stats.sw_stats.alloc_pd_err);
		return ERR_PTR(-ENOMEM);
	}

	pd->ibpd.device = ibdev;

#ifdef HAVE_ALLOC_PD_NO_UCONTEXT
	err = efa_alloc_pd(&pd->ibpd, udata);
#else
	err = efa_alloc_pd(&pd->ibpd, ibucontext, udata);
#endif
	if (err)
		goto err_free;

	return &pd->ibpd;

err_free:
	kfree(pd);
	return ERR_PTR(err);
}
#endif

#ifdef HAVE_DEALLOC_PD_UDATA
void efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
#elif defined(HAVE_PD_CORE_ALLOCATION)
void efa_dealloc_pd(struct ib_pd *ibpd)
#else
int efa_dealloc_pd(struct ib_pd *ibpd)
#endif
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_pd *pd = to_epd(ibpd);

	ibdev_dbg(&dev->ibdev, "Dealloc pd[%d]\n", pd->pdn);
	efa_pd_dealloc(dev, pd->pdn);
#ifndef HAVE_PD_CORE_ALLOCATION
	kfree(pd);

	return 0;
#endif
}

static int efa_destroy_qp_handle(struct efa_dev *dev, u32 qp_handle)
{
	struct efa_com_destroy_qp_params params = { .qp_handle = qp_handle };

	return efa_com_destroy_qp(&dev->edev, &params);
}

static void efa_qp_user_mmap_entries_remove(struct efa_qp *qp)
{
	rdma_user_mmap_entry_remove(qp->rq_mmap_entry);
	rdma_user_mmap_entry_remove(qp->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(qp->llq_desc_mmap_entry);
	rdma_user_mmap_entry_remove(qp->sq_db_mmap_entry);
}

#ifdef HAVE_DESTROY_QP_UDATA
int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
#else
int efa_destroy_qp(struct ib_qp *ibqp)
#endif
{
	struct efa_dev *dev = to_edev(ibqp->pd->device);
	struct efa_qp *qp = to_eqp(ibqp);
	int err;

	ibdev_dbg(&dev->ibdev, "Destroy qp[%u]\n", ibqp->qp_num);

	efa_qp_user_mmap_entries_remove(qp);

	err = efa_destroy_qp_handle(dev, qp->qp_handle);
	if (err)
		return err;

	if (qp->rq_cpu_addr) {
		ibdev_dbg(&dev->ibdev,
			  "qp->cpu_addr[0x%p] freed: size[%lu], dma[%pad]\n",
			  qp->rq_cpu_addr, qp->rq_size,
			  &qp->rq_dma_addr);
		efa_free_mapped(dev, qp->rq_cpu_addr, qp->rq_dma_addr,
				qp->rq_size, DMA_TO_DEVICE);
	}

	kfree(qp);
	return 0;
}

#ifdef HAVE_CORE_MMAP_XA
static struct rdma_user_mmap_entry*
efa_user_mmap_entry_insert(struct ib_ucontext *ucontext,
			   u64 address, size_t length,
			   u8 mmap_flag, u64 *offset)
{
	struct efa_user_mmap_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	int err;

	if (!entry)
		return NULL;

	entry->address = address;
	entry->mmap_flag = mmap_flag;

	err = rdma_user_mmap_entry_insert(ucontext, &entry->rdma_entry,
					  length);
	if (err) {
		kfree(entry);
		return NULL;
	}
	*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}
#else
static struct rdma_user_mmap_entry *
efa_user_mmap_entry_insert(struct ib_ucontext *ibucontext, u64 address,
			   size_t length, u8 mmap_flag, u64 *offset)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_user_mmap_entry *entry;
	u64 next_mmap_page;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->address = address;
	entry->rdma_entry.npages = (u32)DIV_ROUND_UP(length, PAGE_SIZE);
	entry->mmap_flag = mmap_flag;

	mutex_lock(&ucontext->lock);
	next_mmap_page = ucontext->mmap_page + (length >> PAGE_SHIFT);
	if (next_mmap_page >= U32_MAX) {
		ibdev_dbg(ucontext->ibucontext.device, "Too many mmap pages\n");
		mutex_unlock(&ucontext->lock);
		kfree(entry);
		return NULL;
	}

	entry->rdma_entry.start_pgoff = ucontext->mmap_page;
	ucontext->mmap_page = next_mmap_page;
	list_add_tail(&entry->list, &ucontext->pending_mmaps);
	mutex_unlock(&ucontext->lock);

	*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);
	ibdev_dbg(
		ucontext->ibucontext.device,
		"mmap: addr[%#llx], len[%#zx], key[%#llx] inserted\n",
		entry->address, entry->rdma_entry.npages * PAGE_SIZE,
		rdma_user_mmap_get_offset(&entry->rdma_entry));

	return &entry->rdma_entry;
}
#endif

static int qp_mmap_entries_setup(struct efa_qp *qp,
				 struct efa_dev *dev,
				 struct efa_ucontext *ucontext,
				 struct efa_com_create_qp_params *params,
				 struct efa_ibv_create_qp_resp *resp)
{
	size_t length;
	u64 address;

	address = dev->db_bar_addr + resp->sq_db_offset;
	qp->sq_db_mmap_entry =
		efa_user_mmap_entry_insert(&ucontext->ibucontext,
					   address,
					   PAGE_SIZE, EFA_MMAP_IO_NC,
					   &resp->sq_db_mmap_key);
	if (!qp->sq_db_mmap_entry)
		return -ENOMEM;

	resp->sq_db_offset &= ~PAGE_MASK;

	address = dev->mem_bar_addr + resp->llq_desc_offset;
	length = PAGE_ALIGN(params->sq_ring_size_in_bytes +
			    (resp->llq_desc_offset & ~PAGE_MASK));

	qp->llq_desc_mmap_entry =
		efa_user_mmap_entry_insert(&ucontext->ibucontext,
					   address, length,
					   EFA_MMAP_IO_WC,
					   &resp->llq_desc_mmap_key);
	if (!qp->llq_desc_mmap_entry)
		goto err_remove_mmap;

	resp->llq_desc_offset &= ~PAGE_MASK;

	if (qp->rq_size) {
		address = dev->db_bar_addr + resp->rq_db_offset;

		qp->rq_db_mmap_entry =
			efa_user_mmap_entry_insert(&ucontext->ibucontext,
						   address, PAGE_SIZE,
						   EFA_MMAP_IO_NC,
						   &resp->rq_db_mmap_key);
		if (!qp->rq_db_mmap_entry)
			goto err_remove_mmap;

		resp->rq_db_offset &= ~PAGE_MASK;

		address = virt_to_phys(qp->rq_cpu_addr);
		qp->rq_mmap_entry =
			efa_user_mmap_entry_insert(&ucontext->ibucontext,
						   address, qp->rq_size,
						   EFA_MMAP_DMA_PAGE,
						   &resp->rq_mmap_key);
		if (!qp->rq_mmap_entry)
			goto err_remove_mmap;

		resp->rq_mmap_size = qp->rq_size;
	}

	return 0;

err_remove_mmap:
	efa_qp_user_mmap_entries_remove(qp);

	return -ENOMEM;
}

static int efa_qp_validate_cap(struct efa_dev *dev,
			       struct ib_qp_init_attr *init_attr)
{
	if (init_attr->cap.max_send_wr > dev->dev_attr.max_sq_depth) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested send wr[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_send_wr,
			  dev->dev_attr.max_sq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_wr > dev->dev_attr.max_rq_depth) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested receive wr[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_recv_wr,
			  dev->dev_attr.max_rq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_send_sge > dev->dev_attr.max_sq_sge) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested sge send[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_send_sge, dev->dev_attr.max_sq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_sge > dev->dev_attr.max_rq_sge) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested sge recv[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_recv_sge, dev->dev_attr.max_rq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_inline_data > dev->dev_attr.inline_buf_size) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested inline data[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_inline_data,
			  dev->dev_attr.inline_buf_size);
		return -EINVAL;
	}

	return 0;
}

static int efa_qp_validate_attr(struct efa_dev *dev,
				struct ib_qp_init_attr *init_attr)
{
	if (init_attr->qp_type != IB_QPT_DRIVER &&
	    init_attr->qp_type != IB_QPT_UD) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp type %d\n", init_attr->qp_type);
		return -EOPNOTSUPP;
	}

	if (init_attr->srq) {
		ibdev_dbg(&dev->ibdev, "SRQ is not supported\n");
		return -EOPNOTSUPP;
	}

	if (init_attr->create_flags) {
		ibdev_dbg(&dev->ibdev, "Unsupported create flags\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

struct ib_qp *efa_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata)
{
	struct efa_com_create_qp_params create_qp_params = {};
	struct efa_com_create_qp_result create_qp_resp;
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_ibv_create_qp_resp resp = {};
	struct efa_ibv_create_qp cmd = {};
	struct efa_ucontext *ucontext;
	struct efa_qp *qp;
	int err;

#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(&dev->ibdev, "udata is NULL\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

#ifdef HAVE_UDATA_TO_DRV_CONTEXT
	ucontext = rdma_udata_to_drv_context(udata, struct efa_ucontext,
					     ibucontext);
#else
	ucontext = ibpd->uobject ? to_eucontext(ibpd->uobject->context) :
				   NULL;
#endif

	err = efa_qp_validate_cap(dev, init_attr);
	if (err)
		goto err_out;

	err = efa_qp_validate_attr(dev, init_attr);
	if (err)
		goto err_out;

	if (offsetofend(typeof(cmd), driver_qp_type) > udata->inlen) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, no input udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (udata->inlen > sizeof(cmd) &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "Cannot copy udata for create_qp\n");
		goto err_out;
	}

	if (cmd.comp_mask) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		err = -ENOMEM;
		goto err_out;
	}

	create_qp_params.uarn = ucontext->uarn;
	create_qp_params.pd = to_epd(ibpd)->pdn;

	if (init_attr->qp_type == IB_QPT_UD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_UD;
	} else if (cmd.driver_qp_type == EFA_QP_DRIVER_TYPE_SRD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_SRD;
	} else {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp type %d driver qp type %d\n",
			  init_attr->qp_type, cmd.driver_qp_type);
		err = -EOPNOTSUPP;
		goto err_free_qp;
	}

	ibdev_dbg(&dev->ibdev, "Create QP: qp type %d driver qp type %#x\n",
		  init_attr->qp_type, cmd.driver_qp_type);
	create_qp_params.send_cq_idx = to_ecq(init_attr->send_cq)->cq_idx;
	create_qp_params.recv_cq_idx = to_ecq(init_attr->recv_cq)->cq_idx;
	create_qp_params.sq_depth = init_attr->cap.max_send_wr;
	create_qp_params.sq_ring_size_in_bytes = cmd.sq_ring_size;

	create_qp_params.rq_depth = init_attr->cap.max_recv_wr;
	create_qp_params.rq_ring_size_in_bytes = cmd.rq_ring_size;
	qp->rq_size = PAGE_ALIGN(create_qp_params.rq_ring_size_in_bytes);
	if (qp->rq_size) {
		qp->rq_cpu_addr = efa_zalloc_mapped(dev, &qp->rq_dma_addr,
						    qp->rq_size, DMA_TO_DEVICE);
		if (!qp->rq_cpu_addr) {
			err = -ENOMEM;
			goto err_free_qp;
		}

		ibdev_dbg(&dev->ibdev,
			  "qp->cpu_addr[0x%p] allocated: size[%lu], dma[%pad]\n",
			  qp->rq_cpu_addr, qp->rq_size, &qp->rq_dma_addr);
		create_qp_params.rq_base_addr = qp->rq_dma_addr;
	}

	err = efa_com_create_qp(&dev->edev, &create_qp_params,
				&create_qp_resp);
	if (err)
		goto err_free_mapped;

	resp.sq_db_offset = create_qp_resp.sq_db_offset;
	resp.rq_db_offset = create_qp_resp.rq_db_offset;
	resp.llq_desc_offset = create_qp_resp.llq_descriptors_offset;
	resp.send_sub_cq_idx = create_qp_resp.send_sub_cq_idx;
	resp.recv_sub_cq_idx = create_qp_resp.recv_sub_cq_idx;

	err = qp_mmap_entries_setup(qp, dev, ucontext, &create_qp_params,
				    &resp);
	if (err)
		goto err_destroy_qp;

	qp->qp_handle = create_qp_resp.qp_handle;
	qp->ibqp.qp_num = create_qp_resp.qp_num;
	qp->ibqp.qp_type = init_attr->qp_type;
	qp->max_send_wr = init_attr->cap.max_send_wr;
	qp->max_recv_wr = init_attr->cap.max_recv_wr;
	qp->max_send_sge = init_attr->cap.max_send_sge;
	qp->max_recv_sge = init_attr->cap.max_recv_sge;
	qp->max_inline_data = init_attr->cap.max_inline_data;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for qp[%u]\n",
				  create_qp_resp.qp_num);
			goto err_remove_mmap_entries;
		}
	}

	ibdev_dbg(&dev->ibdev, "Created qp[%d]\n", qp->ibqp.qp_num);

	return &qp->ibqp;

err_remove_mmap_entries:
	efa_qp_user_mmap_entries_remove(qp);
err_destroy_qp:
	efa_destroy_qp_handle(dev, create_qp_resp.qp_handle);
err_free_mapped:
	if (qp->rq_size)
		efa_free_mapped(dev, qp->rq_cpu_addr, qp->rq_dma_addr,
				qp->rq_size, DMA_TO_DEVICE);
err_free_qp:
	kfree(qp);
err_out:
	atomic64_inc(&dev->stats.sw_stats.create_qp_err);
	return ERR_PTR(err);
}

static int efa_modify_qp_validate(struct efa_dev *dev, struct efa_qp *qp,
				  struct ib_qp_attr *qp_attr, int qp_attr_mask,
				  enum ib_qp_state cur_state,
				  enum ib_qp_state new_state)
{
#define EFA_MODIFY_QP_SUPP_MASK \
	(IB_QP_STATE | IB_QP_CUR_STATE | IB_QP_EN_SQD_ASYNC_NOTIFY | \
	 IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_QKEY | IB_QP_SQ_PSN)

	if (qp_attr_mask & ~EFA_MODIFY_QP_SUPP_MASK) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp_attr_mask[%#x] supported[%#x]\n",
			  qp_attr_mask, EFA_MODIFY_QP_SUPP_MASK);
		return -EOPNOTSUPP;
	}

#ifdef HAVE_IB_MODIFY_QP_IS_OK_FOUR_PARAMS
	if (!ib_modify_qp_is_ok(cur_state, new_state, IB_QPT_UD,
				qp_attr_mask)) {
#else
	if (!ib_modify_qp_is_ok(cur_state, new_state, IB_QPT_UD,
				qp_attr_mask, IB_LINK_LAYER_UNSPECIFIED)) {
#endif
		ibdev_dbg(&dev->ibdev, "Invalid modify QP parameters\n");
		return -EINVAL;
	}

	if ((qp_attr_mask & IB_QP_PORT) && qp_attr->port_num != 1) {
		ibdev_dbg(&dev->ibdev, "Can't change port num\n");
		return -EOPNOTSUPP;
	}

	if ((qp_attr_mask & IB_QP_PKEY_INDEX) && qp_attr->pkey_index) {
		ibdev_dbg(&dev->ibdev, "Can't change pkey index\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

int efa_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibqp->device);
	struct efa_com_modify_qp_params params = {};
	struct efa_qp *qp = to_eqp(ibqp);
	enum ib_qp_state cur_state;
	enum ib_qp_state new_state;
	int err;

#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(&dev->ibdev, "udata is NULL\n");
		return -EOPNOTSUPP;
	}
#endif

	if (udata->inlen &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, udata->inlen - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		return -EINVAL;
	}

	cur_state = qp_attr_mask & IB_QP_CUR_STATE ? qp_attr->cur_qp_state :
						     qp->state;
	new_state = qp_attr_mask & IB_QP_STATE ? qp_attr->qp_state : cur_state;

	err = efa_modify_qp_validate(dev, qp, qp_attr, qp_attr_mask, cur_state,
				     new_state);
	if (err)
		return err;

	params.qp_handle = qp->qp_handle;

	if (qp_attr_mask & IB_QP_STATE) {
		params.modify_mask |= BIT(EFA_ADMIN_QP_STATE_BIT) |
				      BIT(EFA_ADMIN_CUR_QP_STATE_BIT);
		params.cur_qp_state = qp_attr->cur_qp_state;
		params.qp_state = qp_attr->qp_state;
	}

	if (qp_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		params.modify_mask |=
			BIT(EFA_ADMIN_SQ_DRAINED_ASYNC_NOTIFY_BIT);
		params.sq_drained_async_notify = qp_attr->en_sqd_async_notify;
	}

	if (qp_attr_mask & IB_QP_QKEY) {
		params.modify_mask |= BIT(EFA_ADMIN_QKEY_BIT);
		params.qkey = qp_attr->qkey;
	}

	if (qp_attr_mask & IB_QP_SQ_PSN) {
		params.modify_mask |= BIT(EFA_ADMIN_SQ_PSN_BIT);
		params.sq_psn = qp_attr->sq_psn;
	}

	err = efa_com_modify_qp(&dev->edev, &params);
	if (err)
		return err;

	qp->state = new_state;

	return 0;
}

static int efa_destroy_cq_idx(struct efa_dev *dev, int cq_idx)
{
	struct efa_com_destroy_cq_params params = { .cq_idx = cq_idx };

	return efa_com_destroy_cq(&dev->edev, &params);
}

#ifdef HAVE_IB_VOID_DESTROY_CQ
void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibcq->device);
	struct efa_cq *cq = to_ecq(ibcq);

	ibdev_dbg(&dev->ibdev,
		  "Destroy cq[%d] virt[0x%p] freed: size[%lu], dma[%pad]\n",
		  cq->cq_idx, cq->cpu_addr, cq->size, &cq->dma_addr);

	rdma_user_mmap_entry_remove(cq->mmap_entry);
	efa_destroy_cq_idx(dev, cq->cq_idx);
	efa_free_mapped(dev, cq->cpu_addr, cq->dma_addr, cq->size,
			DMA_FROM_DEVICE);
#ifndef HAVE_CQ_CORE_ALLOCATION
	kfree(cq);
#endif
}
#else
#ifdef HAVE_DESTROY_CQ_UDATA
int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
#else
int efa_destroy_cq(struct ib_cq *ibcq)
#endif
{
	struct efa_dev *dev = to_edev(ibcq->device);
	struct efa_cq *cq = to_ecq(ibcq);
	int err;

	ibdev_dbg(&dev->ibdev,
		  "Destroy cq[%d] virt[0x%p] freed: size[%lu], dma[%pad]\n",
		  cq->cq_idx, cq->cpu_addr, cq->size, &cq->dma_addr);

	rdma_user_mmap_entry_remove(cq->mmap_entry);
	err = efa_destroy_cq_idx(dev, cq->cq_idx);
	if (err)
		return err;

	efa_free_mapped(dev, cq->cpu_addr, cq->dma_addr, cq->size,
			DMA_FROM_DEVICE);

	kfree(cq);
	return 0;
}
#endif

static int cq_mmap_entries_setup(struct efa_dev *dev, struct efa_cq *cq,
				 struct efa_ibv_create_cq_resp *resp)
{
	resp->q_mmap_size = cq->size;
	cq->mmap_entry = efa_user_mmap_entry_insert(&cq->ucontext->ibucontext,
						    virt_to_phys(cq->cpu_addr),
						    cq->size, EFA_MMAP_DMA_PAGE,
						    &resp->q_mmap_key);
	if (!cq->mmap_entry)
		return -ENOMEM;

	return 0;
}

#ifdef HAVE_CREATE_CQ_ATTR
int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata)
#else
int efa_create_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata)
#endif
{
#ifdef HAVE_CREATE_CQ_NO_UCONTEXT
	struct efa_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct efa_ucontext, ibucontext);
#else
	struct efa_ucontext *ucontext = to_ecq(ibcq)->ucontext;
#endif
	struct efa_ibv_create_cq_resp resp = {};
	struct efa_com_create_cq_params params;
	struct efa_com_create_cq_result result;
	struct ib_device *ibdev = ibcq->device;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ibv_create_cq cmd = {};
	struct efa_cq *cq = to_ecq(ibcq);
#ifdef HAVE_CREATE_CQ_ATTR
	int entries = attr->cqe;
#endif
	int err;

	ibdev_dbg(ibdev, "create_cq entries %d\n", entries);

	if (entries < 1 || entries > dev->dev_attr.max_cq_depth) {
		ibdev_dbg(ibdev,
			  "cq: requested entries[%u] non-positive or greater than max[%u]\n",
			  entries, dev->dev_attr.max_cq_depth);
		err = -EINVAL;
		goto err_out;
	}

#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(ibdev, "udata is NULL\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

	if (offsetofend(typeof(cmd), num_sub_cqs) > udata->inlen) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, no input udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (udata->inlen > sizeof(cmd) &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(ibdev, "Cannot copy udata for create_cq\n");
		goto err_out;
	}

	if (cmd.comp_mask || !is_reserved_cleared(cmd.reserved_50)) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (!cmd.cq_entry_size) {
		ibdev_dbg(ibdev,
			  "Invalid entry size [%u]\n", cmd.cq_entry_size);
		err = -EINVAL;
		goto err_out;
	}

	if (cmd.num_sub_cqs != dev->dev_attr.sub_cqs_per_cq) {
		ibdev_dbg(ibdev,
			  "Invalid number of sub cqs[%u] expected[%u]\n",
			  cmd.num_sub_cqs, dev->dev_attr.sub_cqs_per_cq);
		err = -EINVAL;
		goto err_out;
	}

	cq->ucontext = ucontext;
	cq->size = PAGE_ALIGN(cmd.cq_entry_size * entries * cmd.num_sub_cqs);
	cq->cpu_addr = efa_zalloc_mapped(dev, &cq->dma_addr, cq->size,
					 DMA_FROM_DEVICE);
	if (!cq->cpu_addr) {
		err = -ENOMEM;
		goto err_out;
	}

	params.uarn = cq->ucontext->uarn;
	params.cq_depth = entries;
	params.dma_addr = cq->dma_addr;
	params.entry_size_in_bytes = cmd.cq_entry_size;
	params.num_sub_cqs = cmd.num_sub_cqs;
	err = efa_com_create_cq(&dev->edev, &params, &result);
	if (err)
		goto err_free_mapped;

	resp.cq_idx = result.cq_idx;
	cq->cq_idx = result.cq_idx;
	cq->ibcq.cqe = result.actual_depth;
	WARN_ON_ONCE(entries != result.actual_depth);

	err = cq_mmap_entries_setup(dev, cq, &resp);
	if (err) {
		ibdev_dbg(ibdev, "Could not setup cq[%u] mmap entries\n",
			  cq->cq_idx);
		goto err_destroy_cq;
	}

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(ibdev,
				  "Failed to copy udata for create_cq\n");
			goto err_remove_mmap;
		}
	}

	ibdev_dbg(ibdev, "Created cq[%d], cq depth[%u]. dma[%pad] virt[0x%p]\n",
		  cq->cq_idx, result.actual_depth, &cq->dma_addr, cq->cpu_addr);

	return 0;

err_remove_mmap:
	rdma_user_mmap_entry_remove(cq->mmap_entry);
err_destroy_cq:
	efa_destroy_cq_idx(dev, cq->cq_idx);
err_free_mapped:
	efa_free_mapped(dev, cq->cpu_addr, cq->dma_addr, cq->size,
			DMA_FROM_DEVICE);

err_out:
	atomic64_inc(&dev->stats.sw_stats.create_cq_err);
	return err;
}

#ifndef HAVE_CQ_CORE_ALLOCATION
#ifdef HAVE_CREATE_CQ_NO_UCONTEXT
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_udata *udata)
#elif defined(HAVE_CREATE_CQ_ATTR)
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata)
#else
struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev, int entries,
			     int vector,
			     struct ib_ucontext *ibucontext,
			     struct ib_udata *udata)
#endif
{
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_cq *cq;
	int err;

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		atomic64_inc(&dev->stats.sw_stats.create_cq_err);
		return ERR_PTR(-ENOMEM);
	}

#ifdef HAVE_CREATE_CQ_NO_UCONTEXT
	cq->ucontext = rdma_udata_to_drv_context(udata, struct efa_ucontext,
						 ibucontext);
#else
	cq->ucontext = to_eucontext(ibucontext);
#endif

	cq->ibcq.device = ibdev;
#ifdef HAVE_CREATE_CQ_ATTR
	err = efa_create_cq(&cq->ibcq, attr, udata);
#else
	err = efa_create_cq(&cq->ibcq, entries, udata);
#endif
	if (err)
		goto err_free_cq;

	return &cq->ibcq;

err_free_cq:
	kfree(cq);
	return ERR_PTR(err);
}
#endif

#ifdef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
static int umem_to_page_list(struct efa_dev *dev,
			     struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	struct ib_block_iter biter;
	unsigned int hp_idx = 0;

	ibdev_dbg(&dev->ibdev, "hp_cnt[%u], pages_in_hp[%u]\n",
		  hp_cnt, pages_in_hp);

	rdma_for_each_block(umem->sg_head.sgl, &biter, umem->nmap,
			    BIT(hp_shift))
		page_list[hp_idx++] = rdma_block_iter_dma_address(&biter);

	return 0;
}
#elif defined(HAVE_SG_DMA_PAGE_ITER)
static int umem_to_page_list(struct efa_dev *dev,
			     struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	struct sg_dma_page_iter sg_iter;
	unsigned int page_idx = 0;
	unsigned int hp_idx = 0;

	ibdev_dbg(&dev->ibdev, "hp_cnt[%u], pages_in_hp[%u]\n",
		  hp_cnt, pages_in_hp);

	for_each_sg_dma_page(umem->sg_head.sgl, &sg_iter, umem->nmap, 0) {
		if (page_idx % pages_in_hp == 0) {
			page_list[hp_idx] = sg_page_iter_dma_address(&sg_iter);
			hp_idx++;
		}

		page_idx++;
	}

	return 0;
}
#elif defined(HAVE_UMEM_SCATTERLIST_IF)
static int umem_to_page_list(struct efa_dev *dev,
			     struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	unsigned int page_idx = 0;
	unsigned int hp_idx = 0;
	struct scatterlist *sg;
	unsigned int entry;

	ibdev_dbg(&dev->ibdev, "hp_cnt[%u], pages_in_hp[%u]\n",
		  hp_cnt, pages_in_hp);

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		if (sg_dma_len(sg) != PAGE_SIZE) {
			ibdev_dbg(&dev->ibdev,
				  "sg_dma_len[%u] != PAGE_SIZE[%lu]\n",
				  sg_dma_len(sg), PAGE_SIZE);
			return -EINVAL;
		}

		if (page_idx % pages_in_hp == 0) {
			page_list[hp_idx] = sg_dma_address(sg);
			hp_idx++;
		}
		page_idx++;
	}

	return 0;
}
#else
static int umem_to_page_list(struct efa_dev *dev,
			     struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	struct ib_umem_chunk *chunk;
	unsigned int page_idx = 0;
	unsigned int hp_idx = 0;
	unsigned int entry;

	ibdev_dbg(&dev->ibdev, "hp_cnt[%u], pages_in_hp[%u]\n",
		  hp_cnt, pages_in_hp);

	list_for_each_entry(chunk, &umem->chunk_list, list) {
		for (entry = 0; entry < chunk->nents; entry++) {
			if (sg_dma_len(&chunk->page_list[entry]) != PAGE_SIZE) {
				ibdev_dbg(&dev->ibdev,
					  "sg_dma_len[%u] != PAGE_SIZE[%lu]\n",
					  sg_dma_len(&chunk->page_list[entry]),
					  PAGE_SIZE);
				return -EINVAL;
			}

			if (page_idx % pages_in_hp == 0) {
				page_list[hp_idx] =
					sg_dma_address(&chunk->page_list[entry]);
				hp_idx++;
			}
			page_idx++;
		}
	}

	return 0;
}
#endif

static struct scatterlist *efa_vmalloc_buf_to_sg(u64 *buf, int page_cnt)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kcalloc(page_cnt, sizeof(*sglist), GFP_KERNEL);
	if (!sglist)
		return NULL;
	sg_init_table(sglist, page_cnt);
	for (i = 0; i < page_cnt; i++) {
		pg = vmalloc_to_page(buf);
		if (!pg)
			goto err;
		sg_set_page(&sglist[i], pg, PAGE_SIZE, 0);
		buf += PAGE_SIZE / sizeof(*buf);
	}
	return sglist;

err:
	kfree(sglist);
	return NULL;
}

/*
 * create a chunk list of physical pages dma addresses from the supplied
 * scatter gather list
 */
static int pbl_chunk_list_create(struct efa_dev *dev, struct pbl_context *pbl)
{
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int page_cnt = pbl->phys.indirect.pbl_buf_size_in_pages;
	struct scatterlist *pages_sgl = pbl->phys.indirect.sgl;
	unsigned int chunk_list_size, chunk_idx, payload_idx;
	int sg_dma_cnt = pbl->phys.indirect.sg_dma_cnt;
	struct efa_com_ctrl_buff_info *ctrl_buf;
	u64 *cur_chunk_buf, *prev_chunk_buf;
#ifdef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
	struct ib_block_iter biter;
#else
	struct scatterlist *sg;
	unsigned int entry, payloads_in_sg;
#endif
	dma_addr_t dma_addr;
	int i;

	/* allocate a chunk list that consists of 4KB chunks */
	chunk_list_size = DIV_ROUND_UP(page_cnt, EFA_PTRS_PER_CHUNK);

	chunk_list->size = chunk_list_size;
	chunk_list->chunks = kcalloc(chunk_list_size,
				     sizeof(*chunk_list->chunks),
				     GFP_KERNEL);
	if (!chunk_list->chunks)
		return -ENOMEM;

	ibdev_dbg(&dev->ibdev,
		  "chunk_list_size[%u] - pages[%u]\n", chunk_list_size,
		  page_cnt);

	/* allocate chunk buffers: */
	for (i = 0; i < chunk_list_size; i++) {
		chunk_list->chunks[i].buf = kzalloc(EFA_CHUNK_SIZE, GFP_KERNEL);
		if (!chunk_list->chunks[i].buf)
			goto chunk_list_dealloc;

		chunk_list->chunks[i].length = EFA_CHUNK_USED_SIZE;
	}
	chunk_list->chunks[chunk_list_size - 1].length =
		((page_cnt % EFA_PTRS_PER_CHUNK) * EFA_CHUNK_PAYLOAD_PTR_SIZE) +
			EFA_CHUNK_PTR_SIZE;

	/* fill the dma addresses of sg list pages to chunks: */
	chunk_idx = 0;
	payload_idx = 0;
	cur_chunk_buf = chunk_list->chunks[0].buf;
#ifdef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
	rdma_for_each_block(pages_sgl, &biter, sg_dma_cnt,
			    EFA_CHUNK_PAYLOAD_SIZE) {
		cur_chunk_buf[payload_idx++] =
			rdma_block_iter_dma_address(&biter);

		if (payload_idx == EFA_PTRS_PER_CHUNK) {
			chunk_idx++;
			cur_chunk_buf = chunk_list->chunks[chunk_idx].buf;
			payload_idx = 0;
		}
	}
#else
	for_each_sg(pages_sgl, sg, sg_dma_cnt, entry) {
		payloads_in_sg = sg_dma_len(sg) >> EFA_CHUNK_PAYLOAD_SHIFT;
		for (i = 0; i < payloads_in_sg; i++) {
			cur_chunk_buf[payload_idx++] =
				(sg_dma_address(sg) & ~(EFA_CHUNK_PAYLOAD_SIZE - 1)) +
				(EFA_CHUNK_PAYLOAD_SIZE * i);

			if (payload_idx == EFA_PTRS_PER_CHUNK) {
				chunk_idx++;
				cur_chunk_buf = chunk_list->chunks[chunk_idx].buf;
				payload_idx = 0;
			}
		}
	}
#endif

	/* map chunks to dma and fill chunks next ptrs */
	for (i = chunk_list_size - 1; i >= 0; i--) {
		dma_addr = dma_map_single(&dev->pdev->dev,
					  chunk_list->chunks[i].buf,
					  chunk_list->chunks[i].length,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(&dev->pdev->dev, dma_addr)) {
			ibdev_err(&dev->ibdev,
				  "chunk[%u] dma_map_failed\n", i);
			goto chunk_list_unmap;
		}

		chunk_list->chunks[i].dma_addr = dma_addr;
		ibdev_dbg(&dev->ibdev,
			  "chunk[%u] mapped at [%pad]\n", i, &dma_addr);

		if (!i)
			break;

		prev_chunk_buf = chunk_list->chunks[i - 1].buf;

		ctrl_buf = (struct efa_com_ctrl_buff_info *)
				&prev_chunk_buf[EFA_PTRS_PER_CHUNK];
		ctrl_buf->length = chunk_list->chunks[i].length;

		efa_com_set_dma_addr(dma_addr,
				     &ctrl_buf->address.mem_addr_high,
				     &ctrl_buf->address.mem_addr_low);
	}

	return 0;

chunk_list_unmap:
	for (; i < chunk_list_size; i++) {
		dma_unmap_single(&dev->pdev->dev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
	}
chunk_list_dealloc:
	for (i = 0; i < chunk_list_size; i++)
		kfree(chunk_list->chunks[i].buf);

	kfree(chunk_list->chunks);
	return -ENOMEM;
}

static void pbl_chunk_list_destroy(struct efa_dev *dev, struct pbl_context *pbl)
{
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int i;

	for (i = 0; i < chunk_list->size; i++) {
		dma_unmap_single(&dev->pdev->dev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
		kfree(chunk_list->chunks[i].buf);
	}

	kfree(chunk_list->chunks);
}

/* initialize pbl continuous mode: map pbl buffer to a dma address. */
static int pbl_continuous_initialize(struct efa_dev *dev,
				     struct pbl_context *pbl)
{
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(&dev->pdev->dev, pbl->pbl_buf,
				  pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, dma_addr)) {
		ibdev_err(&dev->ibdev, "Unable to map pbl to DMA address\n");
		return -ENOMEM;
	}

	pbl->phys.continuous.dma_addr = dma_addr;
	ibdev_dbg(&dev->ibdev,
		  "pbl continuous - dma_addr = %pad, size[%u]\n",
		  &dma_addr, pbl->pbl_buf_size_in_bytes);

	return 0;
}

/*
 * initialize pbl indirect mode:
 * create a chunk list out of the dma addresses of the physical pages of
 * pbl buffer.
 */
static int pbl_indirect_initialize(struct efa_dev *dev, struct pbl_context *pbl)
{
	u32 size_in_pages = DIV_ROUND_UP(pbl->pbl_buf_size_in_bytes, PAGE_SIZE);
	struct scatterlist *sgl;
	int sg_dma_cnt, err;

	BUILD_BUG_ON(EFA_CHUNK_PAYLOAD_SIZE > PAGE_SIZE);
	sgl = efa_vmalloc_buf_to_sg(pbl->pbl_buf, size_in_pages);
	if (!sgl)
		return -ENOMEM;

	sg_dma_cnt = dma_map_sg(&dev->pdev->dev, sgl, size_in_pages, DMA_TO_DEVICE);
	if (!sg_dma_cnt) {
		err = -EINVAL;
		goto err_map;
	}

	pbl->phys.indirect.pbl_buf_size_in_pages = size_in_pages;
	pbl->phys.indirect.sgl = sgl;
	pbl->phys.indirect.sg_dma_cnt = sg_dma_cnt;
	err = pbl_chunk_list_create(dev, pbl);
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "chunk_list creation failed[%d]\n", err);
		goto err_chunk;
	}

	ibdev_dbg(&dev->ibdev,
		  "pbl indirect - size[%u], chunks[%u]\n",
		  pbl->pbl_buf_size_in_bytes,
		  pbl->phys.indirect.chunk_list.size);

	return 0;

err_chunk:
	dma_unmap_sg(&dev->pdev->dev, sgl, size_in_pages, DMA_TO_DEVICE);
err_map:
	kfree(sgl);
	return err;
}

static void pbl_indirect_terminate(struct efa_dev *dev, struct pbl_context *pbl)
{
	pbl_chunk_list_destroy(dev, pbl);
	dma_unmap_sg(&dev->pdev->dev, pbl->phys.indirect.sgl,
		     pbl->phys.indirect.pbl_buf_size_in_pages, DMA_TO_DEVICE);
	kfree(pbl->phys.indirect.sgl);
}

/* create a page buffer list from a mapped user memory region */
static int pbl_create(struct efa_dev *dev,
		      struct pbl_context *pbl,
#ifdef HAVE_EFA_GDR
		      struct efa_mr *mr,
#else
		      struct ib_umem *umem,
#endif
		      int hp_cnt,
		      u8 hp_shift)
{
	int err;

	pbl->pbl_buf_size_in_bytes = hp_cnt * EFA_CHUNK_PAYLOAD_PTR_SIZE;
	pbl->pbl_buf = kvzalloc(pbl->pbl_buf_size_in_bytes, GFP_KERNEL);
	if (!pbl->pbl_buf)
		return -ENOMEM;

	if (is_vmalloc_addr(pbl->pbl_buf)) {
		pbl->physically_continuous = 0;
#ifdef HAVE_EFA_GDR
		if (mr->umem)
			err = umem_to_page_list(dev, mr->umem, pbl->pbl_buf, hp_cnt,
						hp_shift);
		else
			err = nvmem_to_page_list(dev, mr->nvmem, pbl->pbl_buf);
#else
		err = umem_to_page_list(dev, umem, pbl->pbl_buf, hp_cnt,
					hp_shift);
#endif
		if (err)
			goto err_free;

		err = pbl_indirect_initialize(dev, pbl);
		if (err)
			goto err_free;
	} else {
		pbl->physically_continuous = 1;
#ifdef HAVE_EFA_GDR
		if (mr->umem)
			err = umem_to_page_list(dev, mr->umem, pbl->pbl_buf, hp_cnt,
						hp_shift);
		else
			err = nvmem_to_page_list(dev, mr->nvmem, pbl->pbl_buf);
#else
		err = umem_to_page_list(dev, umem, pbl->pbl_buf, hp_cnt,
					hp_shift);
#endif
		if (err)
			goto err_free;

		err = pbl_continuous_initialize(dev, pbl);
		if (err)
			goto err_free;
	}

	ibdev_dbg(&dev->ibdev,
		  "user_pbl_created: user_pages[%u], continuous[%u]\n",
		  hp_cnt, pbl->physically_continuous);

	return 0;

err_free:
	kvfree(pbl->pbl_buf);
	return err;
}

static void pbl_destroy(struct efa_dev *dev, struct pbl_context *pbl)
{
	if (pbl->physically_continuous)
		dma_unmap_single(&dev->pdev->dev, pbl->phys.continuous.dma_addr,
				 pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
	else
		pbl_indirect_terminate(dev, pbl);

	kvfree(pbl->pbl_buf);
}

static int efa_create_inline_pbl(struct efa_dev *dev, struct efa_mr *mr,
				 struct efa_com_reg_mr_params *params)
{
	int err;

	params->inline_pbl = 1;
#ifdef HAVE_EFA_GDR
	if (mr->umem)
		err = umem_to_page_list(dev, mr->umem, params->pbl.inline_pbl_array,
					params->page_num, params->page_shift);
	else
		err = nvmem_to_page_list(dev, mr->nvmem,
					 params->pbl.inline_pbl_array);
#else
	err = umem_to_page_list(dev, mr->umem, params->pbl.inline_pbl_array,
				params->page_num, params->page_shift);
#endif
	if (err)
		return err;

	ibdev_dbg(&dev->ibdev,
		  "inline_pbl_array - pages[%u]\n", params->page_num);

	return 0;
}

static int efa_create_pbl(struct efa_dev *dev,
			  struct pbl_context *pbl,
			  struct efa_mr *mr,
			  struct efa_com_reg_mr_params *params)
{
	int err;

#ifdef HAVE_EFA_GDR
	err = pbl_create(dev, pbl, mr, params->page_num,
			 params->page_shift);
#else
	err = pbl_create(dev, pbl, mr->umem, params->page_num,
			 params->page_shift);
#endif
	if (err) {
		ibdev_dbg(&dev->ibdev, "Failed to create pbl[%d]\n", err);
		return err;
	}

	params->inline_pbl = 0;
	params->indirect = !pbl->physically_continuous;
	if (pbl->physically_continuous) {
		params->pbl.pbl.length = pbl->pbl_buf_size_in_bytes;

		efa_com_set_dma_addr(pbl->phys.continuous.dma_addr,
				     &params->pbl.pbl.address.mem_addr_high,
				     &params->pbl.pbl.address.mem_addr_low);
	} else {
		params->pbl.pbl.length =
			pbl->phys.indirect.chunk_list.chunks[0].length;

		efa_com_set_dma_addr(pbl->phys.indirect.chunk_list.chunks[0].dma_addr,
				     &params->pbl.pbl.address.mem_addr_high,
				     &params->pbl.pbl.address.mem_addr_low);
	}

	return 0;
}

#ifndef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
static unsigned long efa_cont_pages(struct ib_umem *umem,
				    unsigned long page_size_cap,
				    u64 addr)
{
	unsigned long max_page_shift = fls64(page_size_cap);
#ifndef HAVE_UMEM_SCATTERLIST_IF
	struct ib_umem_chunk *chunk;
#else
	struct scatterlist *sg;
#endif
	u64 base = ~0, p = 0;
	unsigned long tmp;
	unsigned long m;
	u64 len, pfn;
	int i = 0;
	int entry;

	addr = addr >> PAGE_SHIFT;
	tmp = (unsigned long)addr;
	m = find_first_bit(&tmp, BITS_PER_LONG);
	m = min_t(unsigned long, max_page_shift - PAGE_SHIFT, m);

#ifndef HAVE_UMEM_SCATTERLIST_IF
	list_for_each_entry(chunk, &umem->chunk_list, list) {
		for (entry = 0; entry < chunk->nents; entry++) {
			len = DIV_ROUND_UP(sg_dma_len(&chunk->page_list[entry]),
					   PAGE_SIZE);
			pfn = sg_dma_address(&chunk->page_list[entry]) >> PAGE_SHIFT;
			if (base + p != pfn) {
				/*
				 * If either the offset or the new
				 * base are unaligned update m
				 */
				tmp = (unsigned long)(pfn | p);
				if (!IS_ALIGNED(tmp, 1 << m))
					m = find_first_bit(&tmp, BITS_PER_LONG);

				base = pfn;
				p = 0;
			}

			p += len;
			i += len;
		}
	}
#else
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = DIV_ROUND_UP(sg_dma_len(sg), PAGE_SIZE);
		pfn = sg_dma_address(sg) >> PAGE_SHIFT;
		if (base + p != pfn) {
			/*
			 * If either the offset or the new
			 * base are unaligned update m
			 */
			tmp = (unsigned long)(pfn | p);
			if (!IS_ALIGNED(tmp, 1 << m))
				m = find_first_bit(&tmp, BITS_PER_LONG);

			base = pfn;
			p = 0;
		}

		p += len;
		i += len;
	}
#endif

	if (i)
		m = min_t(unsigned long, ilog2(roundup_pow_of_two(i)), m);
	else
		m = 0;

	return BIT(PAGE_SHIFT + m);
}
#endif

struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
			 u64 virt_addr, int access_flags,
			 struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_com_reg_mr_params params = {};
	struct efa_com_reg_mr_result result = {};
	struct pbl_context pbl;
	int supp_access_flags;
	unsigned int pg_sz;
	struct efa_mr *mr;
	int inline_size;
	int err;

#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(&dev->ibdev, "udata is NULL\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

	if (udata && udata->inlen &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		err = -EINVAL;
		goto err_out;
	}

	supp_access_flags =
		IB_ACCESS_LOCAL_WRITE |
		(is_rdma_read_cap(dev) ? IB_ACCESS_REMOTE_READ : 0);

#ifdef HAVE_IB_ACCESS_OPTIONAL
	access_flags &= ~IB_ACCESS_OPTIONAL;
#endif
	if (access_flags & ~supp_access_flags) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported access flags[%#x], supported[%#x]\n",
			  access_flags, supp_access_flags);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		err = -ENOMEM;
		goto err_out;
	}

#ifdef HAVE_EFA_GDR
	mr->nvmem = nvmem_get(dev, mr, start, length, &pg_sz);
	if (!mr->nvmem) {
#ifdef HAVE_IB_UMEM_GET_DEVICE_PARAM
		mr->umem = ib_umem_get(ibpd->device, start, length,
				       access_flags);
#elif defined(HAVE_IB_UMEM_GET_NO_DMASYNC)
		mr->umem = ib_umem_get(udata, start, length, access_flags);
#elif defined(HAVE_IB_UMEM_GET_UDATA)
		mr->umem = ib_umem_get(udata, start, length, access_flags, 0);
#else
		mr->umem = ib_umem_get(ibpd->uobject->context, start, length,
				       access_flags, 0);
#endif
		if (IS_ERR(mr->umem)) {
			err = PTR_ERR(mr->umem);
			ibdev_dbg(&dev->ibdev,
				  "Failed to pin and map user space memory[%d]\n",
				  err);
			goto err_free;
		}

#ifdef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
		pg_sz = ib_umem_find_best_pgsz(mr->umem,
					       dev->dev_attr.page_size_cap,
					       virt_addr);
		if (!pg_sz) {
			err = -EOPNOTSUPP;
			ibdev_dbg(&dev->ibdev, "Failed to find a suitable page size in page_size_cap %#llx\n",
				  dev->dev_attr.page_size_cap);
			goto err_unmap;
		}
#else
		pg_sz = efa_cont_pages(mr->umem, dev->dev_attr.page_size_cap,
				       virt_addr);
#endif
	}
#else /* !defined(HAVE_EFA_GDR) */
#ifdef HAVE_IB_UMEM_GET_DEVICE_PARAM
	mr->umem = ib_umem_get(ibpd->device, start, length, access_flags);
#elif defined(HAVE_IB_UMEM_GET_NO_DMASYNC)
	mr->umem = ib_umem_get(udata, start, length, access_flags);
#elif defined(HAVE_IB_UMEM_GET_UDATA)
	mr->umem = ib_umem_get(udata, start, length, access_flags, 0);
#else
	mr->umem = ib_umem_get(ibpd->uobject->context, start, length,
			       access_flags, 0);
#endif
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		ibdev_dbg(&dev->ibdev,
			  "Failed to pin and map user space memory[%d]\n", err);
		goto err_free;
	}
#endif /* defined(HAVE_EFA_GDR) */

	params.pd = to_epd(ibpd)->pdn;
	params.iova = virt_addr;
	params.mr_length_in_bytes = length;
	params.permissions = access_flags;

#ifndef HAVE_EFA_GDR
#ifdef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
	pg_sz = ib_umem_find_best_pgsz(mr->umem,
				       dev->dev_attr.page_size_cap,
				       virt_addr);
	if (!pg_sz) {
		err = -EOPNOTSUPP;
		ibdev_dbg(&dev->ibdev, "Failed to find a suitable page size in page_size_cap %#llx\n",
			  dev->dev_attr.page_size_cap);
		goto err_unmap;
	}
#else
	pg_sz = efa_cont_pages(mr->umem, dev->dev_attr.page_size_cap,
			       virt_addr);
#endif /* defined(HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE) */
#endif /* !defined(HAVE_EFA_GDR) */

	params.page_shift = __ffs(pg_sz);
	params.page_num = DIV_ROUND_UP(length + (start & (pg_sz - 1)),
				       pg_sz);

	ibdev_dbg(&dev->ibdev,
		  "start %#llx length %#llx params.page_shift %u params.page_num %u\n",
		  start, length, params.page_shift, params.page_num);

	inline_size = ARRAY_SIZE(params.pbl.inline_pbl_array);
	if (params.page_num <= inline_size) {
		err = efa_create_inline_pbl(dev, mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		if (err)
			goto err_unmap;
	} else {
		err = efa_create_pbl(dev, &pbl, mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		pbl_destroy(dev, &pbl);

		if (err)
			goto err_unmap;
	}

	mr->ibmr.lkey = result.l_key;
	mr->ibmr.rkey = result.r_key;
#ifdef HAVE_IB_MR_LENGTH
	mr->ibmr.length = length;
#endif
#ifdef HAVE_EFA_GDR
	if (mr->nvmem) {
		mr->nvmem->lkey = result.l_key;
		mr->nvmem->needs_dereg = true;
	}
#endif
	ibdev_dbg(&dev->ibdev, "Registered mr[%d]\n", mr->ibmr.lkey);

	return &mr->ibmr;

err_unmap:
#ifdef HAVE_EFA_GDR
	if (mr->nvmem)
		nvmem_release(dev, mr->nvmem, false);
	else
		ib_umem_release(mr->umem);
#else
	ib_umem_release(mr->umem);
#endif
err_free:
	kfree(mr);
err_out:
	atomic64_inc(&dev->stats.sw_stats.reg_mr_err);
	return ERR_PTR(err);
}

#ifdef HAVE_DEREG_MR_UDATA
int efa_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
#else
int efa_dereg_mr(struct ib_mr *ibmr)
#endif
{
	struct efa_dev *dev = to_edev(ibmr->device);
	struct efa_com_dereg_mr_params params;
	struct efa_mr *mr = to_emr(ibmr);
	int err;

	ibdev_dbg(&dev->ibdev, "Deregister mr[%d]\n", ibmr->lkey);

#ifdef HAVE_EFA_GDR
	if (mr->nvmem){
		err = nvmem_put(mr->nvmem_ticket, false);
		if (err)
			return err;

		kfree(mr);
		return 0;
	}
#endif
	params.l_key = mr->ibmr.lkey;
	err = efa_com_dereg_mr(&dev->edev, &params);
	if (err)
		return err;

	ib_umem_release(mr->umem);
	kfree(mr);

	return 0;
}

#ifdef HAVE_GET_PORT_IMMUTABLE
int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num,
			   struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err) {
		ibdev_dbg(ibdev, "Couldn't query port err[%d]\n", err);
		return err;
	}

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}
#endif

static int efa_dealloc_uar(struct efa_dev *dev, u16 uarn)
{
	struct efa_com_dealloc_uar_params params = {
		.uarn = uarn,
	};

	return efa_com_dealloc_uar(&dev->edev, &params);
}

#define EFA_CHECK_USER_COMP(_dev, _comp_mask, _attr, _mask, _attr_str) \
	(_attr_str = (!(_dev)->dev_attr._attr || ((_comp_mask) & (_mask))) ? \
		     NULL : #_attr)

static int efa_user_comp_handshake(const struct ib_ucontext *ibucontext,
				   const struct efa_ibv_alloc_ucontext_cmd *cmd)
{
	struct efa_dev *dev = to_edev(ibucontext->device);
	char *attr_str;

	if (EFA_CHECK_USER_COMP(dev, cmd->comp_mask, max_tx_batch,
				EFA_ALLOC_UCONTEXT_CMD_COMP_TX_BATCH, attr_str))
		goto err;

	if (EFA_CHECK_USER_COMP(dev, cmd->comp_mask, min_sq_depth,
				EFA_ALLOC_UCONTEXT_CMD_COMP_MIN_SQ_WR,
				attr_str))
		goto err;

	return 0;

err:
	ibdev_dbg(&dev->ibdev, "Userspace handshake failed for %s attribute\n",
		  attr_str);
	return -EOPNOTSUPP;
}

int efa_alloc_ucontext(struct ib_ucontext *ibucontext, struct ib_udata *udata)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	struct efa_ibv_alloc_ucontext_resp resp = {};
	struct efa_ibv_alloc_ucontext_cmd cmd = {};
	struct efa_com_alloc_uar_result result;
	int err;

	/*
	 * it's fine if the driver does not know all request fields,
	 * we will ack input fields in our response.
	 */

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "Cannot copy udata for alloc_ucontext\n");
		goto err_out;
	}

	err = efa_user_comp_handshake(ibucontext, &cmd);
	if (err)
		goto err_out;

	err = efa_com_alloc_uar(&dev->edev, &result);
	if (err)
		goto err_out;

	ucontext->uarn = result.uarn;
#ifndef HAVE_CORE_MMAP_XA
	mutex_init(&ucontext->lock);
	INIT_LIST_HEAD(&ucontext->pending_mmaps);
#endif /* !defined(HAVE_CORE_MMAP_XA) */

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE;
#endif
#ifndef HAVE_CREATE_AH_NO_UDATA
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_CREATE_AH;
#endif
	resp.sub_cqs_per_cq = dev->dev_attr.sub_cqs_per_cq;
	resp.inline_buf_size = dev->dev_attr.inline_buf_size;
	resp.max_llq_size = dev->dev_attr.max_llq_size;
	resp.max_tx_batch = dev->dev_attr.max_tx_batch;
	resp.min_sq_wr = dev->dev_attr.min_sq_depth;

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err)
			goto err_dealloc_uar;
	}

	return 0;

err_dealloc_uar:
	efa_dealloc_uar(dev, result.uarn);
err_out:
	atomic64_inc(&dev->stats.sw_stats.alloc_ucontext_err);
	return err;
}

#ifndef HAVE_UCONTEXT_CORE_ALLOCATION
struct ib_ucontext *efa_kzalloc_ucontext(struct ib_device *ibdev,
					 struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ucontext *ucontext;
	int err;

	/*
	 * it's fine if the driver does not know all request fields,
	 * we will ack input fields in our response.
	 */

	ucontext = kzalloc(sizeof(*ucontext), GFP_KERNEL);
	if (!ucontext) {
		atomic64_inc(&dev->stats.sw_stats.alloc_ucontext_err);
		return ERR_PTR(-ENOMEM);
	}

	ucontext->ibucontext.device = ibdev;
	err = efa_alloc_ucontext(&ucontext->ibucontext, udata);
	if (err)
		goto err_free_ucontext;

	return &ucontext->ibucontext;

err_free_ucontext:
	kfree(ucontext);
	return ERR_PTR(err);
}
#endif

#ifdef HAVE_UCONTEXT_CORE_ALLOCATION
void efa_dealloc_ucontext(struct ib_ucontext *ibucontext)
#else
int efa_dealloc_ucontext(struct ib_ucontext *ibucontext)
#endif
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);

#ifndef HAVE_CORE_MMAP_XA
	mmap_entries_remove_free(dev, ucontext);
#endif
	efa_dealloc_uar(dev, ucontext->uarn);
#ifndef HAVE_UCONTEXT_CORE_ALLOCATION
	kfree(ucontext);

	return 0;
#endif
}

#ifdef HAVE_CORE_MMAP_XA
void efa_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct efa_user_mmap_entry *entry = to_emmap(rdma_entry);

	kfree(entry);
}
#endif

static int __efa_mmap(struct efa_dev *dev, struct efa_ucontext *ucontext,
		      struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct efa_user_mmap_entry *entry;
	unsigned long va;
	int err = 0;
	u64 pfn;

	rdma_entry = rdma_user_mmap_entry_get(&ucontext->ibucontext, vma);
	if (!rdma_entry) {
		ibdev_dbg(&dev->ibdev,
			  "pgoff[%#lx] does not have valid entry\n",
			  vma->vm_pgoff);
		atomic64_inc(&dev->stats.sw_stats.mmap_err);
		return -EINVAL;
	}
	entry = to_emmap(rdma_entry);

	ibdev_dbg(&dev->ibdev,
		  "Mapping address[%#llx], length[%#zx], mmap_flag[%d]\n",
		  entry->address, rdma_entry->npages * PAGE_SIZE,
		  entry->mmap_flag);

	pfn = entry->address >> PAGE_SHIFT;
	switch (entry->mmap_flag) {
	case EFA_MMAP_IO_NC:
#ifdef HAVE_CORE_MMAP_XA
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot),
					rdma_entry);
#elif defined(HAVE_RDMA_USER_MMAP_IO)
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot));
#else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, pfn,
					 entry->rdma_entry.npages * PAGE_SIZE,
					 vma->vm_page_prot);
#endif
		break;
	case EFA_MMAP_IO_WC:
#ifdef HAVE_CORE_MMAP_XA
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_writecombine(vma->vm_page_prot),
					rdma_entry);
#elif defined(HAVE_RDMA_USER_MMAP_IO)
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_writecombine(vma->vm_page_prot));
#else
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, pfn,
					 entry->rdma_entry.npages * PAGE_SIZE,
					 vma->vm_page_prot);
#endif
		break;
	case EFA_MMAP_DMA_PAGE:
		for (va = vma->vm_start; va < vma->vm_end;
		     va += PAGE_SIZE, pfn++) {
			err = vm_insert_page(vma, va, pfn_to_page(pfn));
			if (err)
				break;
		}
		break;
	default:
		err = -EINVAL;
	}

	if (err) {
		ibdev_dbg(
			&dev->ibdev,
			"Couldn't mmap address[%#llx] length[%#zx] mmap_flag[%d] err[%d]\n",
			entry->address, rdma_entry->npages * PAGE_SIZE,
			entry->mmap_flag, err);
		atomic64_inc(&dev->stats.sw_stats.mmap_err);
	}

	rdma_user_mmap_entry_put(rdma_entry);
	return err;
}

int efa_mmap(struct ib_ucontext *ibucontext,
	     struct vm_area_struct *vma)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	size_t length = vma->vm_end - vma->vm_start;

	ibdev_dbg(&dev->ibdev,
		  "start %#lx, end %#lx, length = %#zx, pgoff = %#lx\n",
		  vma->vm_start, vma->vm_end, length, vma->vm_pgoff);

	return __efa_mmap(dev, ucontext, vma);
}

#ifdef HAVE_CREATE_AH_NO_UDATA
struct efa_ah_id {
	struct list_head list;
	/* dest_addr */
	u8 id[EFA_GID_SIZE];
	unsigned int ref_count;
	u16 ah;
};

static inline bool efa_ah_id_equal(u8 *id1, u8 *id2)
{
	return !memcmp(id1, id2, EFA_GID_SIZE);
}

/* Must be called with dev->ah_list_lock held */
static int efa_get_ah_id(struct efa_dev *dev, u8 *id, bool ref_update, u16 *ah)
{
	struct efa_ah_id *ah_id;

	list_for_each_entry(ah_id, &dev->efa_ah_list, list) {
		if (efa_ah_id_equal(ah_id->id, id)) {
			if (ref_update)
				ah_id->ref_count++;
			if (ah)
				*ah = ah_id->ah;
			return 0;
		}
	}

	return -EINVAL;
}

static void efa_put_ah_id(struct efa_dev *dev, u8 *id)
{
	struct efa_ah_id *ah_id, *tmp;

	mutex_lock(&dev->ah_list_lock);
	list_for_each_entry_safe(ah_id, tmp, &dev->efa_ah_list, list) {
		if (efa_ah_id_equal(ah_id->id, id)) {
			ah_id->ref_count--;
			if (!ah_id->ref_count) {
				list_del(&ah_id->list);
				kfree(ah_id);
				mutex_unlock(&dev->ah_list_lock);
				return;
			}
		}
	}
	mutex_unlock(&dev->ah_list_lock);
}

/* Must be called with dev->ah_list_lock held */
static struct efa_ah_id *efa_create_ah_id(struct efa_dev *dev, u8 *id, u16 ah)
{
	struct efa_ah_id *ah_id;

	ah_id = kzalloc(sizeof(*ah_id), GFP_KERNEL);
	if (!ah_id)
		return NULL;

	memcpy(ah_id->id, id, sizeof(ah_id->id));
	ah_id->ref_count = 1;
	ah_id->ah = ah;

	return ah_id;
}

static int efa_add_ah_id(struct efa_dev *dev, u8 *id, u16 ah)
{
	struct efa_ah_id *ah_id;
	int err;

	mutex_lock(&dev->ah_list_lock);
	err = efa_get_ah_id(dev, id, true, NULL);
	if (err) {
		ah_id = efa_create_ah_id(dev, id, ah);
		if (!ah_id) {
			err = -ENOMEM;
			goto err_unlock;
		}

		list_add_tail(&ah_id->list, &dev->efa_ah_list);
	}
	mutex_unlock(&dev->ah_list_lock);

	return 0;

err_unlock:
	mutex_unlock(&dev->ah_list_lock);
	return err;
}
#endif

static int efa_ah_destroy(struct efa_dev *dev, struct efa_ah *ah)
{
	struct efa_com_destroy_ah_params params = {
		.ah = ah->ah,
		.pdn = to_epd(ah->ibah.pd)->pdn,
	};

	return efa_com_destroy_ah(&dev->edev, &params);
}

int efa_create_ah(struct ib_ah *ibah,
#ifdef HAVE_CREATE_AH_INIT_ATTR
		  struct rdma_ah_init_attr *init_attr,
#else
#ifdef HAVE_CREATE_AH_RDMA_ATTR
		  struct rdma_ah_attr *ah_attr,
#else
		  struct ib_ah_attr *ah_attr,
#endif
		  u32 flags,
#endif
		  struct ib_udata *udata)
{
#ifdef HAVE_CREATE_AH_INIT_ATTR
	struct rdma_ah_attr *ah_attr = init_attr->ah_attr;
#endif
	struct efa_dev *dev = to_edev(ibah->device);
	struct efa_com_create_ah_params params = {};
#ifndef HAVE_CREATE_AH_NO_UDATA
	struct efa_ibv_create_ah_resp resp = {};
#endif
	struct efa_com_create_ah_result result;
	struct efa_ah *ah = to_eah(ibah);
	int err;

#if defined(HAVE_CREATE_DESTROY_AH_FLAGS) || defined(HAVE_CREATE_AH_INIT_ATTR)
#ifdef HAVE_CREATE_DESTROY_AH_FLAGS
	if (!(flags & RDMA_CREATE_AH_SLEEPABLE)) {
#else
	if (!(init_attr->flags & RDMA_CREATE_AH_SLEEPABLE)) {
#endif
		ibdev_dbg(&dev->ibdev,
			  "Create address handle is not supported in atomic context\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

#ifndef HAVE_CREATE_AH_NO_UDATA
#ifndef HAVE_NO_KVERBS_DRIVERS
	if (!udata) {
		ibdev_dbg(&dev->ibdev, "udata is NULL\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}
#endif

	if (udata->inlen &&
#ifdef HAVE_UVERBS_CMD_HDR_FIX
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, udata->inlen - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		ibdev_dbg(&dev->ibdev, "Incompatible ABI params\n");
		err = -EINVAL;
		goto err_out;
	}
#endif

	memcpy(params.dest_addr, ah_attr->grh.dgid.raw,
	       sizeof(params.dest_addr));
	params.pdn = to_epd(ibah->pd)->pdn;
	err = efa_com_create_ah(&dev->edev, &params, &result);
	if (err)
		goto err_out;

	memcpy(ah->id, ah_attr->grh.dgid.raw, sizeof(ah->id));
	ah->ah = result.ah;

#ifndef HAVE_CREATE_AH_NO_UDATA
	resp.efa_address_handle = result.ah;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for create_ah response\n");
			goto err_destroy_ah;
		}
	}
#else
	err = efa_add_ah_id(dev, ah_attr->grh.dgid.raw, result.ah);
	if (err) {
		ibdev_dbg(&dev->ibdev, "Failed to add AH id\n");
		goto err_destroy_ah;
	}
#endif
	ibdev_dbg(&dev->ibdev, "Created ah[%d]\n", ah->ah);

	return 0;

err_destroy_ah:
	efa_ah_destroy(dev, ah);
err_out:
	atomic64_inc(&dev->stats.sw_stats.create_ah_err);
	return err;
}

#ifndef HAVE_AH_CORE_ALLOCATION
#ifdef HAVE_CREATE_DESTROY_AH_FLAGS
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct rdma_ah_attr *ah_attr,
			     u32 flags,
			     struct ib_udata *udata)
#elif defined(HAVE_CREATE_AH_RDMA_ATTR)
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct rdma_ah_attr *ah_attr,
			     struct ib_udata *udata)
#elif defined(HAVE_CREATE_AH_UDATA)
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct ib_ah_attr *ah_attr,
			     struct ib_udata *udata)
#else
struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd,
			     struct ib_ah_attr *ah_attr)
#endif
{
	struct efa_ah *ah;
	int err;
#ifndef HAVE_CREATE_DESTROY_AH_FLAGS
	u32 flags = 0;
#endif
#ifdef HAVE_CREATE_AH_NO_UDATA
	void *udata = NULL;
#endif

	ah = kzalloc(sizeof(*ah), GFP_KERNEL);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	ah->ibah.device = ibpd->device;
	ah->ibah.pd = ibpd;
	err = efa_create_ah(&ah->ibah, ah_attr, flags, udata);
	if (err)
		goto err_free;

	return &ah->ibah;

err_free:
	kfree(ah);
	return ERR_PTR(err);
}
#endif

#ifdef HAVE_AH_CORE_ALLOCATION
void efa_destroy_ah(struct ib_ah *ibah, u32 flags)
#elif defined(HAVE_CREATE_DESTROY_AH_FLAGS)
int efa_destroy_ah(struct ib_ah *ibah, u32 flags)
#else
int efa_destroy_ah(struct ib_ah *ibah)
#endif
{
	struct efa_dev *dev = to_edev(ibah->pd->device);
	struct efa_ah *ah = to_eah(ibah);
#ifndef HAVE_AH_CORE_ALLOCATION
	int err;
#endif

	ibdev_dbg(&dev->ibdev, "Destroy ah[%d]\n", ah->ah);

#ifdef HAVE_CREATE_DESTROY_AH_FLAGS
	if (!(flags & RDMA_DESTROY_AH_SLEEPABLE)) {
		ibdev_dbg(&dev->ibdev,
			  "Destroy address handle is not supported in atomic context\n");
#ifdef HAVE_AH_CORE_ALLOCATION
		return;
#else
		return -EOPNOTSUPP;
#endif
	}
#endif

#ifdef HAVE_AH_CORE_ALLOCATION
	efa_ah_destroy(dev, ah);
#else
	err = efa_ah_destroy(dev, ah);
	if (err)
		return err;
#ifdef HAVE_CREATE_AH_NO_UDATA
	efa_put_ah_id(dev, ah->id);
#endif
#ifndef HAVE_AH_CORE_ALLOCATION
	kfree(ah);
	return 0;
#endif
#endif
}

#ifdef HAVE_HW_STATS
struct rdma_hw_stats *efa_alloc_hw_stats(struct ib_device *ibdev, u8 port_num)
{
	return rdma_alloc_hw_stats_struct(efa_stats_names,
					  ARRAY_SIZE(efa_stats_names),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

int efa_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		     u8 port_num, int index)
{
	struct efa_com_get_stats_params params = {};
	union efa_com_get_stats_result result;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_com_basic_stats *bs;
	struct efa_com_stats_admin *as;
	struct efa_stats *s;
	int err;

	params.type = EFA_ADMIN_GET_STATS_TYPE_BASIC;
	params.scope = EFA_ADMIN_GET_STATS_SCOPE_ALL;

	err = efa_com_get_stats(&dev->edev, &params, &result);
	if (err)
		return err;

	bs = &result.basic_stats;
	stats->value[EFA_TX_BYTES] = bs->tx_bytes;
	stats->value[EFA_TX_PKTS] = bs->tx_pkts;
	stats->value[EFA_RX_BYTES] = bs->rx_bytes;
	stats->value[EFA_RX_PKTS] = bs->rx_pkts;
	stats->value[EFA_RX_DROPS] = bs->rx_drops;

	as = &dev->edev.aq.stats;
	stats->value[EFA_SUBMITTED_CMDS] = atomic64_read(&as->submitted_cmd);
	stats->value[EFA_COMPLETED_CMDS] = atomic64_read(&as->completed_cmd);
	stats->value[EFA_CMDS_ERR] = atomic64_read(&as->cmd_err);
	stats->value[EFA_NO_COMPLETION_CMDS] = atomic64_read(&as->no_completion);

	s = &dev->stats;
	stats->value[EFA_KEEP_ALIVE_RCVD] = atomic64_read(&s->keep_alive_rcvd);
	stats->value[EFA_ALLOC_PD_ERR] = atomic64_read(&s->sw_stats.alloc_pd_err);
	stats->value[EFA_CREATE_QP_ERR] = atomic64_read(&s->sw_stats.create_qp_err);
	stats->value[EFA_CREATE_CQ_ERR] = atomic64_read(&s->sw_stats.create_cq_err);
	stats->value[EFA_REG_MR_ERR] = atomic64_read(&s->sw_stats.reg_mr_err);
	stats->value[EFA_ALLOC_UCONTEXT_ERR] = atomic64_read(&s->sw_stats.alloc_ucontext_err);
	stats->value[EFA_CREATE_AH_ERR] = atomic64_read(&s->sw_stats.create_ah_err);
	stats->value[EFA_MMAP_ERR] = atomic64_read(&s->sw_stats.mmap_err);

	return ARRAY_SIZE(efa_stats_names);
}
#endif

#ifndef HAVE_NO_KVERBS_DRIVERS
#ifdef HAVE_POST_CONST_WR
int efa_post_send(struct ib_qp *ibqp,
		  const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr)
#else
int efa_post_send(struct ib_qp *ibqp,
		  struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr)
#endif
{
	struct efa_dev *dev = to_edev(ibqp->device);

	ibdev_warn(&dev->ibdev.dev, "Function not supported\n");
	return -EOPNOTSUPP;
}

#ifdef HAVE_POST_CONST_WR
int efa_post_recv(struct ib_qp *ibqp,
		  const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr)
#else
int efa_post_recv(struct ib_qp *ibqp,
		  struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr)
#endif
{
	struct efa_dev *dev = to_edev(ibqp->device);

	ibdev_warn(&dev->ibdev.dev, "Function not supported\n");
	return -EOPNOTSUPP;
}

int efa_poll_cq(struct ib_cq *ibcq, int num_entries,
		struct ib_wc *wc)
{
	struct efa_dev *dev = to_edev(ibcq->device);

	ibdev_warn(&dev->ibdev.dev, "Function not supported\n");
	return -EOPNOTSUPP;
}

int efa_req_notify_cq(struct ib_cq *ibcq,
		      enum ib_cq_notify_flags flags)
{
	struct efa_dev *dev = to_edev(ibcq->device);

	ibdev_warn(&dev->ibdev.dev, "Function not supported\n");
	return -EOPNOTSUPP;
}

struct ib_mr *efa_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct efa_dev *dev = to_edev(ibpd->device);

	ibdev_warn(&dev->ibdev.dev, "Function not supported\n");
	return ERR_PTR(-EOPNOTSUPP);
}
#endif

enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
					 u8 port_num)
{
	return IB_LINK_LAYER_UNSPECIFIED;
}

#ifdef HAVE_CUSTOM_COMMANDS
#ifdef HAVE_CREATE_AH_NO_UDATA
ssize_t efa_everbs_cmd_get_ah(struct efa_dev *dev,
			      const char __user *buf,
			      int in_len,
			      int out_len)
{
	struct efa_everbs_get_ah_resp resp = {};
	struct efa_everbs_get_ah cmd = {};
	int err;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.comp_mask) {
		ibdev_dbg(&dev->ibdev,
			"Incompatible ABI params, unknown fields in udata\n");
		return -EINVAL;
	}

	mutex_lock(&dev->ah_list_lock);
	err = efa_get_ah_id(dev, cmd.gid, false, &resp.efa_address_handle);
	mutex_unlock(&dev->ah_list_lock);
	if (err) {
		ibdev_dbg(&dev->ibdev,
			"Couldn't find AH with specified GID\n");
		return err;
	}

	if (copy_to_user((void __user *)(unsigned long)cmd.response, &resp,
			 sizeof(resp)))
		return -EFAULT;

	return in_len;
}
#endif

#ifndef HAVE_IB_QUERY_DEVICE_UDATA
ssize_t efa_everbs_cmd_get_ex_dev_attrs(struct efa_dev *dev,
					const char __user *buf,
					int in_len,
					int out_len)
{
	struct efa_com_get_device_attr_result *dev_attr = &dev->dev_attr;
	struct efa_everbs_get_ex_dev_attrs_resp resp = {};
	struct efa_everbs_get_ex_dev_attrs cmd = {};

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.comp_mask || !is_reserved_cleared(cmd.reserved_20)) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		return -EINVAL;
	}

	resp.max_sq_sge = dev_attr->max_sq_sge;
	resp.max_rq_sge = dev_attr->max_rq_sge;
	resp.max_sq_wr = dev_attr->max_sq_depth;
	resp.max_rq_wr = dev_attr->max_rq_depth;

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		return -EFAULT;

	return in_len;
}
#endif
#endif

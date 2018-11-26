// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>

#include "kcompat.h"
#include "efa-abi.h"
#include "efa.h"
#include "efa_com.h"
#include "efa_com_cmd.h"
#include "efa_pci_id_tbl.h"
#include "efa_sysfs.h"

#define DRV_MODULE_VER_MAJOR           0
#define DRV_MODULE_VER_MINOR           9
#define DRV_MODULE_VER_SUBMINOR        0

#ifndef DRV_MODULE_VERSION
#define DRV_MODULE_VERSION \
	__stringify(DRV_MODULE_VER_MAJOR) "."   \
	__stringify(DRV_MODULE_VER_MINOR) "."   \
	__stringify(DRV_MODULE_VER_SUBMINOR) "g"
#endif

MODULE_VERSION(DRV_MODULE_VERSION);

static char version[] = DEVICE_NAME " v" DRV_MODULE_VERSION;

MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DEVICE_NAME);
MODULE_DEVICE_TABLE(pci, efa_pci_tbl);

#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2

#define EFA_MMAP_DB_BAR_MEMORY_FLAG     BIT(61)
#define EFA_MMAP_REG_BAR_MEMORY_FLAG    BIT(62)
#define EFA_MMAP_MEM_BAR_MEMORY_FLAG    BIT(63)
#define EFA_MMAP_BARS_MEMORY_MASK       \
	(EFA_MMAP_REG_BAR_MEMORY_FLAG | EFA_MMAP_MEM_BAR_MEMORY_FLAG | \
	 EFA_MMAP_DB_BAR_MEMORY_FLAG)
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))

struct efa_ucontext {
	struct ib_ucontext      ibucontext;
	/* Protects ucontext state */
	struct mutex            lock;
	struct list_head        link;
	struct list_head        pending_mmaps;
	u64                     mmap_key;
};

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

struct efa_pd {
	struct ib_pd    ibpd;
	u32             pdn;
};

struct efa_mr {
	struct ib_mr     ibmr;
	struct ib_umem  *umem;
	u64 vaddr;
};

struct efa_cq {
	struct ib_cq               ibcq;
	struct efa_ucontext       *ucontext;
	u16                        cq_idx;
	dma_addr_t                 dma_addr;
	void                      *cpu_addr;
	size_t                     size;
};

struct efa_qp {
	struct ib_qp            ibqp;
	enum ib_qp_state        state;
	u32                     qp_handle;
	dma_addr_t              rq_dma_addr;
	void                   *rq_cpu_addr;
	size_t                  rq_size;
};

struct efa_ah {
	struct ib_ah    ibah;
	/* dest_addr */
	u8              id[EFA_GID_SIZE];
};

struct efa_ah_id {
	struct list_head list;
	/* dest_addr */
	u8 id[EFA_GID_SIZE];
	u16 address_handle;
	unsigned int  ref_count;
};

#ifdef HAVE_CUSTOM_COMMANDS
#define EFA_EVERBS_DEVICE_NAME "efa_everbs"
#define EFA_EVERBS_MAX_DEVICES 64

static struct class *efa_everbs_class;
static unsigned int efa_everbs_major;

static int efa_everbs_dev_init(struct efa_dev *dev, int devnum);
static void efa_everbs_dev_destroy(struct efa_dev *dev);
#endif

struct efa_mmap_entry {
	struct list_head list;
	void  *obj;
	u64 address;
	u64 length;
	u64 key;
};

static void mmap_entry_insert(struct efa_ucontext *ucontext,
			      struct efa_mmap_entry *entry,
			      u64 mem_flag);

static void mmap_obj_entries_remove(struct efa_ucontext *ucontext,
				    void *obj);

#define EFA_PAGE_SHIFT       12
#define EFA_PAGE_SIZE        BIT(EFA_PAGE_SHIFT)
#define EFA_PAGE_PTR_SIZE    8

#define EFA_CHUNK_ALLOC_SIZE BIT(EFA_PAGE_SHIFT)
#define EFA_CHUNK_PTR_SIZE   sizeof(struct efa_com_ctrl_buff_info)

#define EFA_PAGE_PTRS_PER_CHUNK  \
	((EFA_CHUNK_ALLOC_SIZE - EFA_CHUNK_PTR_SIZE) / EFA_PAGE_PTR_SIZE)

#define EFA_CHUNK_USED_SIZE  \
	((EFA_PAGE_PTRS_PER_CHUNK * EFA_PAGE_PTR_SIZE) + EFA_CHUNK_PTR_SIZE)

#define EFA_SUPPORTED_ACCESS_FLAGS IB_ACCESS_LOCAL_WRITE

struct pbl_chunk {
	u64 *buf;
	u32 length;
	dma_addr_t dma_addr;
};

struct pbl_chunk_list {
	unsigned int size;
	struct pbl_chunk *chunks;
};

struct pbl_context {
	u64 *pbl_buf;
	u32  pbl_buf_size_in_bytes;
	bool physically_continuous;
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

	struct efa_dev *dev;
	struct device *dmadev;
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

#define field_avail(x, fld, sz) (offsetof(typeof(x), fld) + \
				 sizeof(((typeof(x) *)0)->fld) <= (sz))

#define EFA_IS_RESERVED_CLEARED(reserved) \
	!memchr_inv(reserved, 0, sizeof(reserved))

static void efa_update_network_attr(struct efa_dev *dev,
				    struct efa_com_get_network_attr_result *network_attr)
{
	pr_debug("-->\n");

	memcpy(dev->addr, network_attr->addr, sizeof(network_attr->addr));
	dev->mtu = network_attr->mtu;

	pr_debug("full addr %pI6\n", dev->addr);
}

static void efa_update_dev_cap(struct efa_dev *dev,
			       struct efa_com_get_device_attr_result *device_attr)
{
	dev->caps.max_sq          = device_attr->max_sq;
	dev->caps.max_sq_depth    = device_attr->max_sq_depth;
	dev->caps.max_rq          = device_attr->max_sq;
	dev->caps.max_rq_depth    = device_attr->max_rq_depth;
	dev->caps.max_cq          = device_attr->max_cq;
	dev->caps.max_cq_depth    = device_attr->max_cq_depth;
	dev->caps.inline_buf_size = device_attr->inline_buf_size;
	dev->caps.max_sq_sge      = device_attr->max_sq_sge;
	dev->caps.max_rq_sge      = device_attr->max_rq_sge;
	dev->caps.max_mr          = device_attr->max_mr;
	dev->caps.max_mr_pages    = device_attr->max_mr_pages;
	dev->caps.page_size_cap   = device_attr->page_size_cap;
	dev->caps.max_pd          = device_attr->max_pd;
	dev->caps.max_ah          = device_attr->max_ah;
	dev->caps.sub_cqs_per_cq  = device_attr->sub_cqs_per_cq;
	dev->caps.max_inline_data = device_attr->inline_buf_size;
}

static int efa_get_device_attributes(struct efa_dev *dev,
				     struct efa_com_get_device_attr_result *result)
{
	int err;

	pr_debug("--->\n");
	err = efa_com_get_device_attr(dev->edev, result);
	if (err)
		pr_err("failed to get device_attr err[%d]!\n", err);

	return err;
}

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
static int efa_query_device(struct ib_device *ibdev,
			    struct ib_device_attr *props,
			    struct ib_udata *udata)
#else
static int efa_query_device(struct ib_device *ibdev,
			    struct ib_device_attr *props)
#endif
{
#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	struct efa_ibv_ex_query_device_resp resp = {};
#endif
	struct efa_com_get_device_attr_result result;
	struct efa_dev *dev = to_edev(ibdev);
	int err;

	pr_debug("--->\n");
	memset(props, 0, sizeof(*props));

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	if (udata && udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		pr_err_ratelimited("Incompatible ABI params, udata not cleared\n");
		return -EINVAL;
	}
#endif

	err = efa_get_device_attributes(dev, &result);
	if (err) {
		pr_err("failed to get device_attr err[%d]!\n", err);
		return err;
	}

	props->max_mr_size              = result.max_mr_pages * PAGE_SIZE;
	props->page_size_cap            = result.page_size_cap;
	props->vendor_id                = result.vendor_id;
	props->vendor_part_id           = result.vendor_part_id;
	props->hw_ver                   = dev->pdev->subsystem_device;
	props->max_qp                   = result.max_sq;
	props->device_cap_flags         = IB_DEVICE_PORT_ACTIVE_EVENT |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
					  IB_DEVICE_VIRTUAL_FUNCTION |
#endif
					  IB_DEVICE_BLOCK_MULTICAST_LOOPBACK;
	props->max_cq                   = result.max_cq;
	props->max_pd                   = result.max_pd;
	props->max_mr                   = result.max_mr;
	props->max_ah                   = result.max_ah;
	props->max_cqe                  = result.max_cq_depth;
	props->max_qp_wr                = min_t(u16, result.max_sq_depth,
						result.max_rq_depth);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
	props->max_sge                  = min_t(u16, result.max_sq_sge,
						result.max_rq_sge);
#else
	props->max_send_sge             = result.max_sq_sge;
	props->max_recv_sge             = result.max_rq_sge;
#endif

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	if (udata && udata->outlen) {
		resp.sub_cqs_per_cq = result.sub_cqs_per_cq;
		resp.max_sq_sge = result.max_sq_sge;
		resp.max_rq_sge = result.max_rq_sge;
		resp.max_sq_wr  = result.max_sq_depth;
		resp.max_rq_wr  = result.max_rq_depth;
		resp.max_inline_data = result.inline_buf_size;

		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			pr_err_ratelimited("failed to copy udata for query_device.\n");
			return err;
		}
	}
#endif

	return err;
}

static int efa_query_port(struct ib_device *ibdev, u8 port,
			  struct ib_port_attr *props)
{
	struct efa_dev *dev = to_edev(ibdev);

	pr_debug("--->\n");

	mutex_lock(&dev->efa_dev_lock);
	memset(props, 0, sizeof(*props));

	props->lid = 0;
	props->lmc = 1;
	props->sm_lid = 0;
	props->sm_sl = 0;

	props->state = IB_PORT_ACTIVE;
	props->phys_state = 5;
	props->port_cap_flags = 0;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->bad_pkey_cntr = 0;
	props->qkey_viol_cntr = 0;
	props->active_speed = IB_SPEED_EDR;
	props->active_width = IB_WIDTH_4X;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	props->max_mtu = ib_mtu_int_to_enum(dev->mtu);
	props->active_mtu = ib_mtu_int_to_enum(dev->mtu);
#else
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = IB_MTU_4096;
#endif
	props->max_msg_sz = dev->mtu;
	props->max_vl_num = 1;
	mutex_unlock(&dev->efa_dev_lock);
	return 0;
}

static int efa_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
			int qp_attr_mask,
			struct ib_qp_init_attr *qp_init_attr)
{
	struct efa_qp *qp = to_eqp(ibqp);

	pr_debug("--->\n");

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	qp_attr->qp_state = qp->state;
	qp_attr->cur_qp_state = qp->state;
	qp_attr->port_num = 1;

	qp_init_attr->qp_type = ibqp->qp_type;
	qp_init_attr->recv_cq = ibqp->recv_cq;
	qp_init_attr->send_cq = ibqp->send_cq;

	return 0;
}

static int efa_query_gid(struct ib_device *ibdev, u8 port, int index,
			 union ib_gid *gid)
{
	struct efa_dev *dev = to_edev(ibdev);

	pr_debug("port %d gid index %d\n", port, index);

	if (index > 1)
		return -EINVAL;

	mutex_lock(&dev->efa_dev_lock);
	memcpy(gid->raw, dev->addr, sizeof(dev->addr));
	mutex_unlock(&dev->efa_dev_lock);

	return 0;
}

static int efa_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			  u16 *pkey)
{
	pr_debug("--->\n");
	if (index > 1)
		return -EINVAL;

	*pkey = 0xffff;
	return 0;
}

static struct ib_pd *efa_alloc_pd(struct ib_device *ibdev,
				  struct ib_ucontext *ibucontext,
				  struct ib_udata *udata)
{
	struct efa_ibv_alloc_pd_resp resp = {};
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_pd *pd;
	int err;

	pr_debug("--->\n");

	if (!ibucontext) {
		pr_err("ibucontext is not valid\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (udata && udata->inlen &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, udata->inlen - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		pr_err_ratelimited("Incompatible ABI params, udata not cleared\n");
		return ERR_PTR(-EINVAL);
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev->stats.sw_stats.alloc_pd_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	pd->pdn = efa_bitmap_alloc(&dev->pd_bitmap);
	if (pd->pdn == EFA_BITMAP_INVAL) {
		pr_err("Failed to alloc PD (max_pd %u)\n", dev->caps.max_pd);
		dev->stats.sw_stats.alloc_pd_bitmap_full_err++;
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	resp.pdn = pd->pdn;

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			pr_err_ratelimited("failed to copy udata for alloc_pd\n");
			efa_bitmap_free(&dev->pd_bitmap, pd->pdn);
			kfree(pd);
			return ERR_PTR(err);
		}
	}

	pr_debug("Allocated pd[%d]\n", pd->pdn);

	return &pd->ibpd;
}

static int efa_dealloc_pd(struct ib_pd *ibpd)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_pd *pd = to_epd(ibpd);

	pr_debug("Dealloc pd[%d]\n", pd->pdn);
	efa_bitmap_free(&dev->pd_bitmap, pd->pdn);
	kfree(pd);

	return 0;
}

static int efa_destroy_qp_handle(struct efa_dev *dev, u32 qp_handle)
{
	struct efa_com_destroy_qp_params params = { .qp_handle = qp_handle };

	return efa_com_destroy_qp(dev->edev, &params);
}

static int efa_destroy_qp(struct ib_qp *ibqp)
{
	struct efa_dev *dev = to_edev(ibqp->pd->device);
	struct efa_qp *qp = to_eqp(ibqp);
	struct efa_ucontext *ucontext;

	pr_debug("Destroy qp[%u]\n", ibqp->qp_num);
	ucontext = ibqp->pd->uobject ?
			to_eucontext(ibqp->pd->uobject->context) :
			NULL;

	if (!ucontext)
		return -EOPNOTSUPP;

	efa_destroy_qp_handle(dev, qp->qp_handle);
	mmap_obj_entries_remove(ucontext, qp);

	if (qp->rq_cpu_addr) {
		pr_debug("qp->cpu_addr[%p] freed: size[%lu], dma[%pad]\n",
			 qp->rq_cpu_addr, qp->rq_size,
			 &qp->rq_dma_addr);
		dma_free_coherent(&dev->pdev->dev, qp->rq_size,
				  qp->rq_cpu_addr, qp->rq_dma_addr);
	}

	kfree(qp);
	return 0;
}

static int qp_mmap_entries_setup(struct efa_qp *qp,
				 struct efa_dev *dev,
				 struct efa_ucontext *ucontext,
				 struct efa_com_create_qp_params *params,
				 struct efa_ibv_create_qp_resp *resp)
{
	struct efa_mmap_entry *rq_db_entry;
	struct efa_mmap_entry *sq_db_entry;
	struct efa_mmap_entry *rq_entry;
	struct efa_mmap_entry *sq_entry;

	sq_db_entry = kzalloc(sizeof(*sq_db_entry), GFP_KERNEL);
	sq_entry = kzalloc(sizeof(*sq_entry), GFP_KERNEL);
	if (!sq_db_entry || !sq_entry) {
		dev->stats.sw_stats.mmap_entry_alloc_err++;
		goto err_alloc;
	}

	if (qp->rq_size) {
		rq_entry = kzalloc(sizeof(*rq_entry), GFP_KERNEL);
		rq_db_entry = kzalloc(sizeof(*rq_db_entry), GFP_KERNEL);
		if (!rq_entry || !rq_db_entry) {
			dev->stats.sw_stats.mmap_entry_alloc_err++;
			goto err_alloc_rq;
		}

		rq_db_entry->obj = qp;
		rq_entry->obj    = qp;

		rq_entry->address = virt_to_phys(qp->rq_cpu_addr);
		rq_entry->length = qp->rq_size;
		mmap_entry_insert(ucontext, rq_entry, 0);
		resp->rq_mmap_key = rq_entry->key;
		resp->rq_mmap_size = qp->rq_size;

		rq_db_entry->address = dev->db_bar_addr +
				       resp->rq_db_offset;
		rq_db_entry->length = PAGE_SIZE;
		mmap_entry_insert(ucontext, rq_db_entry,
				  EFA_MMAP_DB_BAR_MEMORY_FLAG);
		resp->rq_db_mmap_key = rq_db_entry->key;
		resp->rq_db_offset &= ~PAGE_MASK;
	}

	sq_db_entry->obj = qp;
	sq_entry->obj    = qp;

	sq_db_entry->address = dev->db_bar_addr + resp->sq_db_offset;
	resp->sq_db_offset &= ~PAGE_MASK;
	sq_db_entry->length = PAGE_SIZE;
	mmap_entry_insert(ucontext, sq_db_entry, EFA_MMAP_DB_BAR_MEMORY_FLAG);
	resp->sq_db_mmap_key = sq_db_entry->key;

	sq_entry->address = dev->mem_bar_addr + resp->llq_desc_offset;
	resp->llq_desc_offset &= ~PAGE_MASK;
	sq_entry->length = PAGE_ALIGN(params->sq_ring_size_in_bytes +
				      resp->llq_desc_offset);
	mmap_entry_insert(ucontext, sq_entry, EFA_MMAP_MEM_BAR_MEMORY_FLAG);
	resp->llq_desc_mmap_key = sq_entry->key;

	return 0;

err_alloc_rq:
	kfree(rq_entry);
	kfree(rq_db_entry);
err_alloc:
	kfree(sq_entry);
	kfree(sq_db_entry);
	return -ENOMEM;
}

static int efa_qp_validate_cap(struct efa_dev *dev,
			       struct ib_qp_init_attr *init_attr)
{
	if (init_attr->cap.max_send_wr > dev->caps.max_sq_depth) {
		pr_err("qp: requested send wr[%u] exceeds the max[%u]\n",
		       init_attr->cap.max_send_wr,
		       dev->caps.max_sq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_wr > dev->caps.max_rq_depth) {
		pr_err("qp: requested receive wr[%u] exceeds the max[%u]\n",
		       init_attr->cap.max_recv_wr,
		       dev->caps.max_rq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_send_sge > dev->caps.max_sq_sge) {
		pr_err("qp: requested sge send[%u] exceeds the max[%u]\n",
		       init_attr->cap.max_send_sge, dev->caps.max_sq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_sge > dev->caps.max_rq_sge) {
		pr_err("qp: requested sge recv[%u] exceeds the max[%u]\n",
		       init_attr->cap.max_recv_sge, dev->caps.max_rq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_inline_data > dev->caps.inline_buf_size) {
		pr_warn("requested inline data[%u] exceeds the max[%u]\n",
			init_attr->cap.max_inline_data,
			dev->caps.inline_buf_size);
		return -EINVAL;
	}

	return 0;
}

static struct ib_qp *efa_create_qp(struct ib_pd *ibpd,
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

	ucontext = ibpd->uobject ? to_eucontext(ibpd->uobject->context) :
				   NULL;

	err = efa_qp_validate_cap(dev, init_attr);
	if (err)
		return ERR_PTR(err);

	if (!ucontext)
		return ERR_PTR(-EOPNOTSUPP);

	if (init_attr->qp_type != IB_QPT_UD) {
		pr_err("unsupported qp type %d\n", init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}

	if (!udata || !field_avail(cmd, srd_qp, udata->inlen)) {
		pr_err_ratelimited("Incompatible ABI params, no input udata\n");
		return ERR_PTR(-EINVAL);
	}

	if (udata->inlen > sizeof(cmd) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return ERR_PTR(-EINVAL);
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		pr_err_ratelimited("%s: cannot copy udata for create_qp\n",
				   dev_name(&dev->ibdev.dev));
		return ERR_PTR(err);
	}

	if (cmd.comp_mask) {
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return ERR_PTR(-EINVAL);
	}

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		dev->stats.sw_stats.create_qp_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	create_qp_params.pd = to_epd(ibpd)->pdn;
	if (cmd.srd_qp)
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_SRD;
	else
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_UD;

	pr_debug("create QP, qp type %d srd qp %d\n",
		 init_attr->qp_type, cmd.srd_qp);
	create_qp_params.send_cq_idx = to_ecq(init_attr->send_cq)->cq_idx;
	create_qp_params.recv_cq_idx = to_ecq(init_attr->recv_cq)->cq_idx;
	create_qp_params.sq_depth = cmd.sq_depth;
	create_qp_params.sq_ring_size_in_bytes = cmd.sq_ring_size;

	create_qp_params.rq_ring_size_in_bytes = cmd.rq_entries *
						 cmd.rq_entry_size;
	qp->rq_size = PAGE_ALIGN(create_qp_params.rq_ring_size_in_bytes);
	if (qp->rq_size) {
		qp->rq_cpu_addr = dma_zalloc_coherent(&dev->pdev->dev,
						      qp->rq_size,
						      &qp->rq_dma_addr,
						      GFP_KERNEL);
		if (!qp->rq_cpu_addr) {
			dev->stats.sw_stats.create_qp_alloc_err++;
			err = -ENOMEM;
			goto err_free_qp;
		}
		pr_debug("qp->cpu_addr[%p] allocated: size[%lu], dma[%pad]\n",
			 qp->rq_cpu_addr, qp->rq_size, &qp->rq_dma_addr);
		create_qp_params.rq_base_addr = qp->rq_dma_addr;
	}

	memset(&resp, 0, sizeof(resp));
	err = efa_com_create_qp(dev->edev, &create_qp_params,
				&create_qp_resp);
	if (err) {
		pr_err("failed to create qp %d\n", err);
		err = -EINVAL;
		goto err_free_dma;
	}

	WARN_ON_ONCE(create_qp_resp.sq_db_offset > dev->db_bar_len);
	WARN_ON_ONCE(create_qp_resp.rq_db_offset > dev->db_bar_len);
	WARN_ON_ONCE(create_qp_resp.llq_descriptors_offset >
		     dev->mem_bar_len);

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

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			pr_err_ratelimited("failed to copy udata for qp[%u]",
					   create_qp_resp.qp_num);
			goto err_mmap_remove;
		}
	}

	pr_debug("Created qp[%d]\n", qp->ibqp.qp_num);

	return &qp->ibqp;

err_mmap_remove:
	mmap_obj_entries_remove(ucontext, qp);
err_destroy_qp:
	efa_destroy_qp_handle(dev, create_qp_resp.qp_handle);
err_free_dma:
	if (qp->rq_size) {
		pr_debug("qp->cpu_addr[%p] freed: size[%lu], dma[%pad]\n",
			 qp->rq_cpu_addr, qp->rq_size, &qp->rq_dma_addr);
		dma_free_coherent(&dev->pdev->dev, qp->rq_size,
				  qp->rq_cpu_addr, qp->rq_dma_addr);
	}
err_free_qp:
	kfree(qp);
	return ERR_PTR(err);
}

static int efa_destroy_cq_idx(struct efa_dev *dev, int cq_idx)
{
	struct efa_com_destroy_cq_params params = { .cq_idx = cq_idx };

	return efa_com_destroy_cq(dev->edev, &params);
}

static int efa_destroy_cq(struct ib_cq *ibcq)
{
	struct efa_dev *dev = to_edev(ibcq->device);
	struct efa_cq *cq = to_ecq(ibcq);

	pr_debug("Destroy cq[%d] virt[%p] freed: size[%lu], dma[%pad]\n",
		 cq->cq_idx, cq->cpu_addr, cq->size, &cq->dma_addr);
	if (!cq->ucontext)
		return -EOPNOTSUPP;

	efa_destroy_cq_idx(dev, cq->cq_idx);

	mmap_obj_entries_remove(cq->ucontext, cq);
	dma_free_coherent(&dev->pdev->dev, cq->size,
			  cq->cpu_addr, cq->dma_addr);

	kfree(cq);
	return 0;
}

static int cq_mmap_entries_setup(struct efa_cq *cq,
				 struct efa_ibv_create_cq_resp *resp)
{
	struct efa_mmap_entry *cq_entry;

	cq_entry = kzalloc(sizeof(*cq_entry), GFP_KERNEL);
	if (!cq_entry)
		return -ENOMEM;

	cq_entry->obj = cq;

	cq_entry->address = virt_to_phys(cq->cpu_addr);
	cq_entry->length = cq->size;
	mmap_entry_insert(cq->ucontext, cq_entry, 0);
	resp->q_mmap_key = cq_entry->key;
	resp->q_mmap_size = cq_entry->length;

	return 0;
}

static struct ib_cq *do_create_cq(struct ib_device *ibdev, int entries,
				  int vector, struct ib_ucontext *ibucontext,
				  struct ib_udata *udata)
{
	struct efa_ibv_create_cq_resp resp = {};
	struct efa_com_create_cq_params params;
	struct efa_com_create_cq_result result;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ibv_create_cq cmd = {};
	struct efa_cq *cq;
	int err;

	pr_debug("entries %d udata %p\n", entries, udata);

	if (entries < 1 || entries > dev->caps.max_cq_depth) {
		pr_err("cq: requested entries[%u] non-positive or greater than max[%u]\n",
		       entries, dev->caps.max_cq_depth);
		return ERR_PTR(-EINVAL);
	}

	if (!ibucontext) {
		pr_err("context is not valid ");
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (!udata || !field_avail(cmd, num_sub_cqs, udata->inlen)) {
		pr_err_ratelimited("Incompatible ABI params, no input udata\n");
		return ERR_PTR(-EINVAL);
	}

	if (udata->inlen > sizeof(cmd) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return ERR_PTR(-EINVAL);
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		pr_err_ratelimited("%s: cannot copy udata for create_cq\n",
				   dev_name(&dev->ibdev.dev));
		return ERR_PTR(err);
	}

	if (cmd.comp_mask || !EFA_IS_RESERVED_CLEARED(cmd.reserved_50)) {
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return ERR_PTR(-EINVAL);
	}

	if (!cmd.cq_entry_size) {
		pr_err("invalid entry size [%u]\n", cmd.cq_entry_size);
		return ERR_PTR(-EINVAL);
	}

	if (cmd.num_sub_cqs != dev->caps.sub_cqs_per_cq) {
		pr_err("invalid number of sub cqs[%u] expected[%u]\n",
		       cmd.num_sub_cqs, dev->caps.sub_cqs_per_cq);
		return ERR_PTR(-EINVAL);
	}

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		dev->stats.sw_stats.create_cq_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	memset(&resp, 0, sizeof(resp));
	cq->ucontext = to_eucontext(ibucontext);
	cq->size = PAGE_ALIGN(cmd.cq_entry_size * entries * cmd.num_sub_cqs);
	cq->cpu_addr = dma_zalloc_coherent(&dev->pdev->dev,
					   cq->size, &cq->dma_addr,
					   GFP_KERNEL);
	if (!cq->cpu_addr) {
		dev->stats.sw_stats.create_cq_alloc_err++;
		err = -ENOMEM;
		goto err_free_cq;
	}
	pr_debug("cq->cpu_addr[%p] allocated: size[%lu], dma[%pad]\n",
		 cq->cpu_addr, cq->size, &cq->dma_addr);

	params.cq_depth = entries;
	params.dma_addr = cq->dma_addr;
	params.entry_size_in_bytes = cmd.cq_entry_size;
	params.num_sub_cqs = cmd.num_sub_cqs;
	err = efa_com_create_cq(dev->edev, &params, &result);
	if (err) {
		pr_err("failed to create cq [%d]!\n", err);
		goto err_free_dma;
	}

	resp.cq_idx = result.cq_idx;
	cq->cq_idx  = result.cq_idx;
	cq->ibcq.cqe = result.actual_depth;
	WARN_ON_ONCE(entries != result.actual_depth);

	err = cq_mmap_entries_setup(cq, &resp);
	if (err) {
		pr_err("could not setup cq[%u] mmap entries!\n", cq->cq_idx);
		goto err_destroy_cq;
	}

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			pr_err_ratelimited("failed to copy udata for %s",
					   dev_name(&dev->ibdev.dev));
			goto err_mmap_remove;
		}
	}

	pr_debug("Created cq[%d], cq depth[%u]. dma[%pad] virt[%p]\n",
		 cq->cq_idx, result.actual_depth, &cq->dma_addr, cq->cpu_addr);

	return &cq->ibcq;

err_mmap_remove:
	mmap_obj_entries_remove(to_eucontext(ibucontext), cq);
err_destroy_cq:
	efa_destroy_cq_idx(dev, cq->cq_idx);
err_free_dma:
	pr_debug("cq->cpu_addr[%p] freed: size[%lu], dma[%pad]\n",
		 cq->cpu_addr, cq->size, &cq->dma_addr);
	dma_free_coherent(&dev->pdev->dev, cq->size, cq->cpu_addr,
			  cq->dma_addr);
err_free_cq:
	kfree(cq);
	return ERR_PTR(err);
}

#ifdef HAVE_CREATE_CQ_ATTR
static struct ib_cq *efa_create_cq(struct ib_device *ibdev,
				   const struct ib_cq_init_attr *attr,
				   struct ib_ucontext *ibucontext,
				   struct ib_udata *udata)
{
	pr_debug("--->\n");
	return do_create_cq(ibdev, attr->cqe, attr->comp_vector, ibucontext,
			    udata);
}
#else
static struct ib_cq *efa_create_cq(struct ib_device *ibdev, int entries,
				   int vector,
				   struct ib_ucontext *ibucontext,
				   struct ib_udata *udata)
{
	return do_create_cq(ibdev, entries, vector, ibucontext, udata);
}
#endif

static int umem_to_page_list(struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	unsigned int page_idx = 0;
	unsigned int hp_idx = 0;
#ifndef HAVE_UMEM_SCATTERLIST_IF
	struct ib_umem_chunk *chunk;
#else
	struct scatterlist *sg;
#endif
	unsigned int entry;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	if (umem->page_shift != PAGE_SHIFT)
#else
	if (umem->page_size != PAGE_SIZE)
#endif
		return -EINVAL;

	pr_debug("hp_cnt[%u], pages_in_hp[%u]\n", hp_cnt, pages_in_hp);

#ifndef HAVE_UMEM_SCATTERLIST_IF
	list_for_each_entry(chunk, &umem->chunk_list, list) {
		for (entry = 0; entry < chunk->nents; entry++) {
			if (unlikely(sg_dma_len(&chunk->page_list[entry]) != PAGE_SIZE)) {
				pr_err("sg_dma_len[%u] != PAGE_SIZE[%lu]\n",
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
#else /* HAVE_UMEM_SCATTERLIST_IF */
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		if (unlikely(sg_dma_len(sg) != PAGE_SIZE)) {
			pr_err("sg_dma_len[%u] != PAGE_SIZE[%lu]\n",
			       sg_dma_len(sg), PAGE_SIZE);
			return -EINVAL;
		}

		if (page_idx % pages_in_hp == 0) {
			page_list[hp_idx] = sg_dma_address(sg);
			hp_idx++;
		}
		page_idx++;
	}
#endif /* HAVE_UMEM_SCATTERLIST_IF */

	return 0;
}

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
		WARN_ON_ONCE(PageHighMem(pg));
		sg_set_page(&sglist[i], pg, EFA_PAGE_SIZE, 0);
		buf = (u64 *)((u8 *)buf + EFA_PAGE_SIZE);
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
static int pbl_chunk_list_create(struct pbl_context *pbl)
{
	unsigned int entry, npg_in_sg, chunk_list_size, chunk_idx, page_idx;
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int page_cnt = pbl->phys.indirect.pbl_buf_size_in_pages;
	struct scatterlist *pages_sgl = pbl->phys.indirect.sgl;
	int sg_dma_cnt = pbl->phys.indirect.sg_dma_cnt;
	struct efa_com_ctrl_buff_info *ctrl_buf;
	u64 *cur_chunk_buf, *prev_chunk_buf;
	struct scatterlist *sg;
	dma_addr_t dma_addr;
	int i;

	/* allocate a chunk list that consists of 4KB chunks */
	chunk_list_size = DIV_ROUND_UP(page_cnt, EFA_PAGE_PTRS_PER_CHUNK);

	chunk_list->size = chunk_list_size;
	chunk_list->chunks = kcalloc(chunk_list_size,
				     sizeof(*chunk_list->chunks),
				     GFP_KERNEL);
	if (!chunk_list->chunks)
		return -ENOMEM;

	pr_debug("chunk_list_size[%u] - pages[%u]\n", chunk_list_size,
		 page_cnt);

	/* allocate chunk buffers: */
	for (i = 0; i < chunk_list_size; i++) {
		chunk_list->chunks[i].buf = kzalloc(EFA_CHUNK_ALLOC_SIZE,
						    GFP_KERNEL);
		if (!chunk_list->chunks[i].buf)
			goto chunk_list_dealloc;

		chunk_list->chunks[i].length = EFA_CHUNK_USED_SIZE;
	}
	chunk_list->chunks[chunk_list_size - 1].length =
		((page_cnt % EFA_PAGE_PTRS_PER_CHUNK) * EFA_PAGE_PTR_SIZE) +
			EFA_CHUNK_PTR_SIZE;

	/* fill the dma addresses of sg list pages to chunks: */
	chunk_idx = 0;
	page_idx  = 0;
	cur_chunk_buf = chunk_list->chunks[0].buf;
	for_each_sg(pages_sgl, sg, sg_dma_cnt, entry) {
		npg_in_sg = sg_dma_len(sg) >> EFA_PAGE_SHIFT;
		for (i = 0; i < npg_in_sg; i++) {
			cur_chunk_buf[page_idx++] = sg_dma_address(sg) +
						    (EFA_PAGE_SIZE * i);

			if (page_idx == EFA_PAGE_PTRS_PER_CHUNK) {
				chunk_idx++;
				cur_chunk_buf = chunk_list->chunks[chunk_idx].buf;
				page_idx = 0;
			}
		}
	}

	/* map chunks to dma and fill chunks next ptrs */
	for (i = chunk_list_size - 1; i >= 0; i--) {
		dma_addr = dma_map_single(pbl->dmadev,
					  chunk_list->chunks[i].buf,
					  chunk_list->chunks[i].length,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(pbl->dmadev, dma_addr)) {
			pr_err("chunk[%u] dma_map_failed\n", i);
			goto chunk_list_unmap;
		}

		chunk_list->chunks[i].dma_addr = dma_addr;
		pr_debug("chunk[%u] mapped at [%pad]\n", i, &dma_addr);

		if (!i)
			break;

		prev_chunk_buf = chunk_list->chunks[i - 1].buf;

		ctrl_buf = (struct efa_com_ctrl_buff_info *)
				&prev_chunk_buf[EFA_PAGE_PTRS_PER_CHUNK];
		ctrl_buf->length = chunk_list->chunks[i].length;

		efa_com_set_dma_addr(dma_addr,
				     &ctrl_buf->address.mem_addr_high,
				     &ctrl_buf->address.mem_addr_low);
	}

	return 0;

chunk_list_unmap:
	for (; i < chunk_list_size; i++) {
		dma_unmap_single(pbl->dmadev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
	}
chunk_list_dealloc:
	for (i = 0; i < chunk_list_size; i++)
		kfree(chunk_list->chunks[i].buf);

	kfree(chunk_list->chunks);
	return -ENOMEM;
}

static void pbl_chunk_list_destroy(struct pbl_context *pbl)
{
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int i;

	for (i = 0; i < chunk_list->size; i++) {
		dma_unmap_single(pbl->dmadev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
		kfree(chunk_list->chunks[i].buf);
	}

	kfree(chunk_list->chunks);
}

/* initialize pbl continuous mode: map pbl buffer to a dma address. */
static int pbl_continuous_initialize(struct pbl_context *pbl)
{
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(pbl->dmadev, pbl->pbl_buf,
				  pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(pbl->dmadev, dma_addr)) {
		pr_err("Unable to map pbl to DMA address");
		return -ENOMEM;
	}

	pbl->phys.continuous.dma_addr = dma_addr;
	pr_debug("pbl continuous - dma_addr = %pad, size[%u]\n",
		 &dma_addr, pbl->pbl_buf_size_in_bytes);

	return 0;
}

/*
 * initialize pbl indirect mode:
 * create a chunk list out of the dma addresses of the physical pages of
 * pbl buffer.
 */
static int pbl_indirect_initialize(struct pbl_context *pbl)
{
	u32 size_in_pages = DIV_ROUND_UP(pbl->pbl_buf_size_in_bytes,
					 EFA_PAGE_SIZE);
	struct scatterlist *sgl;
	int sg_dma_cnt, err;

	sgl = efa_vmalloc_buf_to_sg(pbl->pbl_buf, size_in_pages);
	if (!sgl)
		return -ENOMEM;

	sg_dma_cnt = dma_map_sg(pbl->dmadev, sgl, size_in_pages, DMA_TO_DEVICE);
	if (!sg_dma_cnt) {
		err = -EINVAL;
		goto err_map;
	}

	pbl->phys.indirect.pbl_buf_size_in_pages = size_in_pages;
	pbl->phys.indirect.sgl = sgl;
	pbl->phys.indirect.sg_dma_cnt = sg_dma_cnt;
	err = pbl_chunk_list_create(pbl);
	if (err) {
		pr_err("chunk_list creation failed[%d]!\n", err);
		goto err_chunk;
	}

	pr_debug("pbl indirect - size[%u], chunks[%u]\n",
		 pbl->pbl_buf_size_in_bytes,
		 pbl->phys.indirect.chunk_list.size);

	return 0;

err_chunk:
	dma_unmap_sg(pbl->dmadev, sgl, size_in_pages, DMA_TO_DEVICE);
err_map:
	kfree(sgl);
	return err;
}

static void pbl_indirect_terminate(struct pbl_context *pbl)
{
	pbl_chunk_list_destroy(pbl);
	dma_unmap_sg(pbl->dmadev, pbl->phys.indirect.sgl,
		     pbl->phys.indirect.pbl_buf_size_in_pages, DMA_TO_DEVICE);
	kfree(pbl->phys.indirect.sgl);
}

/* create a page buffer list from a mapped user memory region */
static int pbl_create(struct pbl_context *pbl,
		      struct efa_dev *dev,
		      struct ib_umem *umem,
		      int hp_cnt,
		      u8 hp_shift)
{
	int err;

	pbl->dev = dev;
	pbl->dmadev = &dev->pdev->dev;
	pbl->pbl_buf_size_in_bytes = hp_cnt * EFA_PAGE_PTR_SIZE;
	pbl->pbl_buf = kzalloc(pbl->pbl_buf_size_in_bytes,
			       GFP_KERNEL | __GFP_NOWARN);
	if (pbl->pbl_buf) {
		pbl->physically_continuous = true;
		err = umem_to_page_list(umem, pbl->pbl_buf, hp_cnt, hp_shift);
		if (err)
			goto err_continuous;
		err = pbl_continuous_initialize(pbl);
		if (err)
			goto err_continuous;
	} else {
		pbl->physically_continuous = false;
		pbl->pbl_buf = vzalloc(pbl->pbl_buf_size_in_bytes);
		if (!pbl->pbl_buf)
			return -ENOMEM;

		err = umem_to_page_list(umem, pbl->pbl_buf, hp_cnt, hp_shift);
		if (err)
			goto err_indirect;
		err = pbl_indirect_initialize(pbl);
		if (err)
			goto err_indirect;
	}

	pr_debug("user_pbl_created: user_pages[%u], continuous[%u]\n",
		 hp_cnt, pbl->physically_continuous);

	return 0;

err_continuous:
	kfree(pbl->pbl_buf);
	return err;
err_indirect:
	vfree(pbl->pbl_buf);
	return err;
}

static void pbl_destroy(struct pbl_context *pbl)
{
	if (pbl->physically_continuous) {
		dma_unmap_single(pbl->dmadev, pbl->phys.continuous.dma_addr,
				 pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
		kfree(pbl->pbl_buf);
	} else {
		pbl_indirect_terminate(pbl);
		vfree(pbl->pbl_buf);
	}
}

static int efa_create_inline_pbl(struct efa_mr *mr,
				 struct efa_com_reg_mr_params *params)
{
	int err;

	params->inline_pbl = true;
	err = umem_to_page_list(mr->umem, params->pbl.inline_pbl_array,
				params->page_num, params->page_shift);
	if (err) {
		pr_err("failed to create inline pbl[%d]\n", err);
		return err;
	}

	pr_debug("inline_pbl_array - pages[%u]\n", params->page_num);

	return 0;
}

static int efa_create_pbl(struct efa_dev *dev,
			  struct pbl_context *pbl,
			  struct efa_mr *mr,
			  struct efa_com_reg_mr_params *params)
{
	int err;

	err = pbl_create(pbl, dev, mr->umem, params->page_num,
			 params->page_shift);
	if (err) {
		pr_err("failed to create pbl[%d]\n", err);
		return err;
	}

	params->inline_pbl = false;
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

static void efa_cont_pages(struct ib_umem *umem, u64 addr,
			   unsigned long max_page_shift,
			   int *count, u8 *shift, u32 *ncont)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	unsigned long page_shift = umem->page_shift;
#else
	unsigned long page_shift = ilog2(umem->page_size);
#endif
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

	addr = addr >> page_shift;
	tmp = (unsigned long)addr;
	m = find_first_bit(&tmp, BITS_PER_LONG);
	if (max_page_shift)
		m = min_t(unsigned long, max_page_shift - page_shift, m);

#ifndef HAVE_UMEM_SCATTERLIST_IF
	list_for_each_entry(chunk, &umem->chunk_list, list) {
		for (entry = 0; entry < chunk->nents; entry++) {
			len = sg_dma_len(&chunk->page_list[entry]) >> page_shift;
			pfn = sg_dma_address(&chunk->page_list[entry]) >> page_shift;
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
		len = sg_dma_len(sg) >> page_shift;
		pfn = sg_dma_address(sg) >> page_shift;
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

	if (i) {
		m = min_t(unsigned long, ilog2(roundup_pow_of_two(i)), m);
		*ncont = DIV_ROUND_UP(i, (1 << m));
	} else {
		m = 0;
		*ncont = 0;
	}

	*shift = page_shift + m;
	*count = i;
}

static struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
				u64 virt_addr, int access_flags,
				struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_com_reg_mr_params params = {};
	struct efa_com_reg_mr_result result = {};
	struct pbl_context pbl;
	struct efa_mr *mr;
	int inline_size;
	int npages;
	int err;

	if (udata && udata->inlen &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen))) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen) - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		pr_err_ratelimited("Incompatible ABI params, udata not cleared\n");
		return ERR_PTR(-EINVAL);
	}

	if (access_flags & ~EFA_SUPPORTED_ACCESS_FLAGS) {
		pr_err("Unsupported access flags[%#x], supported[%#x]\n",
		       access_flags, EFA_SUPPORTED_ACCESS_FLAGS);
		return ERR_PTR(-EOPNOTSUPP);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		dev->stats.sw_stats.reg_mr_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	mr->umem = ib_umem_get(ibpd->uobject->context, start, length,
			       access_flags, 0);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		pr_err("failed to pin and map user space memory[%d]\n", err);
		goto err;
	}

	params.pd = to_epd(ibpd)->pdn;
	params.iova = virt_addr;
	params.mr_length_in_bytes = length;
	params.permissions = access_flags & 0x1;

	efa_cont_pages(mr->umem, start,
		       EFA_ADMIN_REG_MR_CMD_PHYS_PAGE_SIZE_SHIFT_MASK, &npages,
		       &params.page_shift,  &params.page_num);
	pr_debug("start %#llx length %#llx npages %d params.page_shift %u params.page_num %u\n",
		 start, length, npages, params.page_shift, params.page_num);

	inline_size = ARRAY_SIZE(params.pbl.inline_pbl_array);
	if (params.page_num <= inline_size) {
		err = efa_create_inline_pbl(mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(dev->edev, &params, &result);
		if (err) {
			pr_err("efa_com_register_mr failed - %d!\n", err);
			goto err_unmap;
		}
	} else {
		err = efa_create_pbl(dev, &pbl, mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(dev->edev, &params, &result);
		pbl_destroy(&pbl);

		if (err) {
			pr_err("efa_com_register_mr failed - %d!\n", err);
			goto err_unmap;
		}
	}

	mr->vaddr = virt_addr;
	mr->ibmr.lkey = result.l_key;
	mr->ibmr.rkey = result.r_key;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	mr->ibmr.length = length;
#endif
	pr_debug("Registered mr[%d]\n", mr->ibmr.lkey);

	return &mr->ibmr;

err_unmap:
	ib_umem_release(mr->umem);
err:
	kfree(mr);
	return ERR_PTR(err);
}

static int efa_dereg_mr(struct ib_mr *ibmr)
{
	struct efa_dev *dev = to_edev(ibmr->device);
	struct efa_com_dereg_mr_params params;
	struct efa_mr *mr = to_emr(ibmr);

	pr_debug("Deregister mr[%d]\n", ibmr->lkey);

	if (mr->umem) {
		params.l_key = mr->ibmr.lkey;
		efa_com_dereg_mr(dev->edev, &params);
		ib_umem_release(mr->umem);
	}

	kfree(mr);

	return 0;
}

#ifdef HAVE_GET_PORT_IMMUTABLE
static int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num,
				  struct ib_port_immutable *immutable)
{
	pr_debug("--->\n");
	immutable->core_cap_flags = RDMA_CORE_CAP_PROT_IB;
	immutable->gid_tbl_len    = 1;

	return 0;
}
#endif

static struct ib_ucontext *efa_alloc_ucontext(struct ib_device *ibdev,
					      struct ib_udata *udata)
{
	struct efa_ibv_alloc_ucontext_resp resp = {};
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ucontext *ucontext;
	int err;

	pr_debug("--->\n");
	/*
	 * it's fine if the driver does not know all request fields,
	 * we will ack input fields in our response.
	 */

	ucontext = kzalloc(sizeof(*ucontext), GFP_KERNEL);
	if (!ucontext) {
		dev->stats.sw_stats.alloc_ucontext_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&ucontext->lock);
	INIT_LIST_HEAD(&ucontext->pending_mmaps);

	mutex_lock(&dev->efa_dev_lock);

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE;
#endif
#ifdef HAVE_CREATE_AH_UDATA
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_CREATE_AH;
#endif

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err)
			goto err_resp;
	}

	list_add_tail(&ucontext->link, &dev->ctx_list);
	mutex_unlock(&dev->efa_dev_lock);
	return &ucontext->ibucontext;

err_resp:
	mutex_unlock(&dev->efa_dev_lock);
	kfree(ucontext);
	return ERR_PTR(err);
}

static int efa_dealloc_ucontext(struct ib_ucontext *ibucontext)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);

	pr_debug("--->\n");

	WARN_ON(!list_empty(&ucontext->pending_mmaps));

	mutex_lock(&dev->efa_dev_lock);
	list_del(&ucontext->link);
	mutex_unlock(&dev->efa_dev_lock);
	kfree(ucontext);
	return 0;
}

static void mmap_obj_entries_remove(struct efa_ucontext *ucontext, void *obj)
{
	struct efa_mmap_entry *entry, *tmp;

	pr_debug("--->\n");

	mutex_lock(&ucontext->lock);
	list_for_each_entry_safe(entry, tmp, &ucontext->pending_mmaps, list) {
		if (entry->obj == obj) {
			list_del(&entry->list);
			pr_debug("mmap: obj[%p] key[0x%llx] addr[0x%llX] len[0x%llX] removed\n",
				 entry->obj, entry->key, entry->address,
				 entry->length);
			kfree(entry);
		}
	}
	mutex_unlock(&ucontext->lock);
}

static struct efa_mmap_entry *mmap_entry_remove(struct efa_ucontext *ucontext,
						u64 key,
						u64 len)
{
	struct efa_mmap_entry *entry, *tmp;

	mutex_lock(&ucontext->lock);
	list_for_each_entry_safe(entry, tmp, &ucontext->pending_mmaps, list) {
		if (entry->key == key && entry->length == len) {
			list_del_init(&entry->list);
			pr_debug("mmap: obj[%p] key[0x%llx] addr[0x%llX] len[0x%llX] removed\n",
				 entry->obj, key, entry->address,
				 entry->length);
			mutex_unlock(&ucontext->lock);
			return entry;
		}
	}
	mutex_unlock(&ucontext->lock);

	return NULL;
}

static void mmap_entry_insert(struct efa_ucontext *ucontext,
			      struct efa_mmap_entry *entry,
			      u64 mem_flag)
{
	mutex_lock(&ucontext->lock);
	entry->key = ucontext->mmap_key | mem_flag;
	ucontext->mmap_key += PAGE_SIZE;
	list_add_tail(&entry->list, &ucontext->pending_mmaps);
	pr_debug("mmap: obj[%p] addr[0x%llx], len[0x%llx], key[0x%llx] inserted\n",
		 entry->obj, entry->address, entry->length, entry->key);
	mutex_unlock(&ucontext->lock);
}

static int __efa_mmap(struct efa_dev *dev,
		      struct vm_area_struct *vma,
		      u64 mmap_flag,
		      u64 address,
		      u64 length)
{
	u64 pfn = address >> PAGE_SHIFT;
	int err;

	switch (mmap_flag) {
	case EFA_MMAP_REG_BAR_MEMORY_FLAG:
		pr_debug("mapping address[0x%llX], length[0x%llX] on register BAR!",
			 address, length);
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, pfn, length,
					 vma->vm_page_prot);
		break;
	case EFA_MMAP_MEM_BAR_MEMORY_FLAG:
		pr_debug("mapping address 0x%llX, length[0x%llX] on memory BAR!",
			 address, length);
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, pfn, length,
					 vma->vm_page_prot);
		break;
	case EFA_MMAP_DB_BAR_MEMORY_FLAG:
		pr_debug("mapping address 0x%llX, length[0x%llX] on DB BAR!",
			 address, length);
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, pfn, length,
					 vma->vm_page_prot);
		break;
	default:
		pr_debug("mapping address[0x%llX], length[0x%llX] of dma buffer!\n",
			 address, length);
		err = remap_pfn_range(vma, vma->vm_start, pfn, length,
				      vma->vm_page_prot);
	}

	return err;
}

static int efa_mmap(struct ib_ucontext *ibucontext,
		    struct vm_area_struct *vma)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	u64 length = vma->vm_end - vma->vm_start;
	u64 key = vma->vm_pgoff << PAGE_SHIFT;
	struct efa_mmap_entry *entry;
	u64 mmap_flag;
	u64 address;

	pr_debug("start 0x%lx, end 0x%lx, length = 0x%llx, key = 0x%llx\n",
		 vma->vm_start, vma->vm_end, length, key);

	if (length % PAGE_SIZE != 0) {
		pr_err("length[0x%llX] is not page size aligned[0x%lX]!",
		       length, PAGE_SIZE);
		return -EINVAL;
	}

	entry = mmap_entry_remove(ucontext, key, length);
	if (!entry) {
		pr_err("key[0x%llX] does not have valid entry!", key);
		return -EINVAL;
	}
	address = entry->address;
	kfree(entry);

	mmap_flag = key & EFA_MMAP_BARS_MEMORY_MASK;
	return __efa_mmap(dev, vma, mmap_flag, address, length);
}

static inline bool efa_ah_id_equal(u8 *id1, u8 *id2)
{
	return !memcmp(id1, id2, EFA_GID_SIZE);
}

static int efa_get_ah_by_id(struct efa_dev *dev, u8 *id,
			    u16 *ah_res, bool ref_update)
{
	struct efa_ah_id *ah_id;

	list_for_each_entry(ah_id, &dev->efa_ah_list, list) {
		if (efa_ah_id_equal(ah_id->id, id)) {
			*ah_res =  ah_id->address_handle;
			if (ref_update)
				ah_id->ref_count++;
			return 0;
		}
	}

	return -EINVAL;
}

static int efa_add_ah_id(struct efa_dev *dev, u8 *id,
			 u16 address_handle)
{
	struct efa_ah_id *ah_id;

	ah_id = kzalloc(sizeof(*ah_id), GFP_KERNEL);
	if (!ah_id)
		return -ENOMEM;

	memcpy(ah_id->id, id, sizeof(ah_id->id));
	ah_id->address_handle = address_handle;
	ah_id->ref_count = 1;
	list_add_tail(&ah_id->list, &dev->efa_ah_list);

	return 0;
}

static void efa_remove_ah_id(struct efa_dev *dev, u8 *id, u32 *ref_count)
{
	struct efa_ah_id *ah_id, *tmp;

	list_for_each_entry_safe(ah_id, tmp, &dev->efa_ah_list, list) {
		if (efa_ah_id_equal(ah_id->id, id)) {
			*ref_count = --ah_id->ref_count;
			if (ah_id->ref_count == 0) {
				list_del(&ah_id->list);
				kfree(ah_id);
				return;
			}
		}
	}
}

#ifndef HAVE_CREATE_AH_UDATA
static int efa_get_ah(struct efa_dev *dev,
		      struct ib_ah_attr *ah_attr, u16 *efa_ah_res)
{
	int err;

	mutex_lock(&dev->ah_list_lock);
	err = efa_get_ah_by_id(dev, &ah_attr->grh.dgid.raw[0],
			       efa_ah_res, false);
	mutex_unlock(&dev->ah_list_lock);

	return err;
}
#endif

static void ah_destroy_on_device(struct efa_dev *dev, u16 device_ah)
{
	struct efa_com_destroy_ah_params params;
	int err;

	params.ah = device_ah;
	err = efa_com_destroy_ah(dev->edev, &params);
	if (err)
		pr_err("efa_com_destroy_ah failed (%d)\n", err);
}

static int efa_create_ah_id(struct efa_dev *dev, u8 *id,
			    u16 *efa_address_handle)
{
	struct efa_com_create_ah_params params = {};
	struct efa_com_create_ah_result result = {};
	int err;

	mutex_lock(&dev->ah_list_lock);
	err = efa_get_ah_by_id(dev, id, efa_address_handle, true);
	if (err) {
		memcpy(params.dest_addr, id, sizeof(params.dest_addr));
		err = efa_com_create_ah(dev->edev, &params, &result);
		if (err) {
			pr_err("efa_com_create_ah failed %d\n", err);
			goto err_unlock;
		}

		pr_debug("create address handle %u for address %pI6\n",
			 result.ah, params.dest_addr);

		err = efa_add_ah_id(dev, id, result.ah);
		if (err) {
			pr_err("efa_add_ah_id failed %d\n", err);
			goto err_destroy_ah;
		}

		*efa_address_handle = result.ah;
	}
	mutex_unlock(&dev->ah_list_lock);

	return 0;

err_destroy_ah:
	ah_destroy_on_device(dev, result.ah);
err_unlock:
	mutex_unlock(&dev->ah_list_lock);
	return err;
}

static void efa_destroy_ah_id(struct efa_dev *dev, u8 *id)
{
	u16 device_ah;
	u32 ref_count;
	int err;

	mutex_lock(&dev->ah_list_lock);
	err = efa_get_ah_by_id(dev, id, &device_ah, false);
	if (err) {
		WARN_ON(1);
		goto out_unlock;
	}

	efa_remove_ah_id(dev, id, &ref_count);
	if (!ref_count)
		ah_destroy_on_device(dev, device_ah);

out_unlock:
	mutex_unlock(&dev->ah_list_lock);
}

#ifdef HAVE_CREATE_AH_UDATA
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
static struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
				   struct rdma_ah_attr *ah_attr,
				   struct ib_udata *udata)
#else
static struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
				   struct ib_ah_attr *ah_attr,
				   struct ib_udata *udata)
#endif
#else
static struct ib_ah *efa_create_ah(struct ib_pd *ibpd,
				   struct ib_ah_attr *ah_attr)
#endif
{
	struct efa_dev *dev = to_edev(ibpd->device);
#ifdef HAVE_CREATE_AH_UDATA
	struct efa_ibv_create_ah_resp resp = {};
#endif
	u16 efa_address_handle;
	struct efa_ah *ah;
	int err;

	pr_debug("--->\n");

#ifdef HAVE_CREATE_AH_UDATA
	if (udata && udata->inlen &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
#else
	    /* WA for e093111ddb6c ("IB/core: Fix input len in multiple user verbs") */
	    !ib_is_udata_cleared(udata, 0, udata->inlen - sizeof(struct ib_uverbs_cmd_hdr))) {
#endif
		pr_err_ratelimited("Incompatiable ABI params\n");
		return ERR_PTR(-EINVAL);
	}
#endif

	ah = kzalloc(sizeof(*ah), GFP_KERNEL);
	if (!ah) {
		dev->stats.sw_stats.create_ah_alloc_err++;
		return ERR_PTR(-ENOMEM);
	}

	err = efa_create_ah_id(dev, ah_attr->grh.dgid.raw, &efa_address_handle);
	if (err)
		goto err_free;

#ifdef HAVE_CREATE_AH_UDATA
	resp.efa_address_handle = efa_address_handle;

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			pr_err_ratelimited("failed to copy udata for create_ah response\n");
			goto err_destroy_ah;
		}
	}
#endif

	memcpy(ah->id, ah_attr->grh.dgid.raw, sizeof(ah->id));
	return &ah->ibah;

#ifdef HAVE_CREATE_AH_UDATA
err_destroy_ah:
	efa_destroy_ah_id(dev, ah_attr->grh.dgid.raw);
#endif
err_free:
	kfree(ah);
	return ERR_PTR(err);
}

static int efa_destroy_ah(struct ib_ah *ibah)
{
	struct efa_dev *dev = to_edev(ibah->pd->device);
	struct efa_ah *ah = to_eah(ibah);

	pr_debug("--->\n");
	efa_destroy_ah_id(dev, ah->id);

	kfree(ah);
	return 0;
}

/* In ib callbacks section -  Start of stub funcs */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
static int efa_post_send(struct ib_qp *ibqp,
			 struct ib_send_wr *wr,
			 struct ib_send_wr **bad_wr)
#else
static int efa_post_send(struct ib_qp *ibqp,
			 const struct ib_send_wr *wr,
			 const struct ib_send_wr **bad_wr)
#endif
{
	pr_warn("Function not supported\n");
	return -EOPNOTSUPP;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
static int efa_post_recv(struct ib_qp *ibqp,
			 struct ib_recv_wr *wr,
			 struct ib_recv_wr **bad_wr)
#else
static int efa_post_recv(struct ib_qp *ibqp,
			 const struct ib_recv_wr *wr,
			 const struct ib_recv_wr **bad_wr)
#endif
{
	pr_warn("Function not supported\n");
	return -EOPNOTSUPP;
}

static int efa_poll_cq(struct ib_cq *ibcq, int num_entries,
		       struct ib_wc *wc)
{
	pr_warn("Function not supported\n");
	return -EOPNOTSUPP;
}

static int efa_req_notify_cq(struct ib_cq *ibcq,
			     enum ib_cq_notify_flags flags)
{
	pr_warn("Function not supported\n");
	return -EOPNOTSUPP;
}

static struct ib_mr *efa_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	pr_warn("Function not supported\n");
	return ERR_PTR(-EOPNOTSUPP);
}

static int efa_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata)
{
	pr_warn("Function not supported\n");
	return -EOPNOTSUPP;
}

static enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
						u8 port_num)
{
	pr_debug("--->\n");
	return IB_LINK_LAYER_ETHERNET;
}

#ifdef HAVE_CUSTOM_COMMANDS
#ifndef HAVE_CREATE_AH_UDATA
static ssize_t efa_everbs_cmd_get_ah(struct efa_dev *dev,
				     const char __user *buf,
				     int in_len,
				     int out_len)
{
	struct efa_everbs_get_ah_resp resp = {};
	struct efa_everbs_get_ah cmd = {};
	struct ib_ah_attr attr;
	int err;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.comp_mask) {
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return -EINVAL;
	}

	attr.dlid               = cmd.attr.dlid;
	attr.src_path_bits      = cmd.attr.src_path_bits;
	attr.sl                 = cmd.attr.sl;
	attr.static_rate        = cmd.attr.static_rate;
	attr.ah_flags           = cmd.attr.is_global ? IB_AH_GRH : 0;
	attr.port_num           = cmd.attr.port_num;
	attr.grh.flow_label     = cmd.attr.grh.flow_label;
	attr.grh.sgid_index     = cmd.attr.grh.sgid_index;
	attr.grh.hop_limit      = cmd.attr.grh.hop_limit;
	attr.grh.traffic_class  = cmd.attr.grh.traffic_class;
	memcpy(attr.grh.dgid.raw, cmd.attr.grh.dgid, EFA_GID_SIZE);

	err = efa_get_ah(dev, &attr, &resp.efa_address_handle);
	if (err)
		return err;

	if (copy_to_user((void __user *)(unsigned long)cmd.response, &resp,
			 sizeof(resp)))
		return -EFAULT;

	return in_len;
}
#endif

#ifndef HAVE_IB_QUERY_DEVICE_UDATA
static ssize_t efa_everbs_cmd_get_ex_dev_attrs(struct efa_dev *dev,
					       const char __user *buf,
					       int in_len,
					       int out_len)
{
	struct efa_everbs_get_ex_dev_attrs_resp resp = {};
	struct efa_everbs_get_ex_dev_attrs cmd = {};
	struct efa_caps *caps = &dev->caps;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.comp_mask || !EFA_IS_RESERVED_CLEARED(cmd.reserved_20)) {
		pr_err_ratelimited("Incompatible ABI params, unknown fields in udata\n");
		return -EINVAL;
	}

	resp.sub_cqs_per_cq  = caps->sub_cqs_per_cq;
	resp.max_sq_sge      = caps->max_sq_sge;
	resp.max_rq_sge      = caps->max_rq_sge;
	resp.max_sq_wr       = caps->max_sq_depth;
	resp.max_rq_wr       = caps->max_rq_depth;
	resp.max_inline_data = caps->max_inline_data;

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		return -EFAULT;

	return in_len;
}
#endif

static ssize_t
(*efa_everbs_cmd_table[EFA_EVERBS_CMD_MAX])(struct efa_dev *dev,
					    const char __user *buf, int in_len,
					    int out_len) = {
#ifndef HAVE_CREATE_AH_UDATA
	[EFA_EVERBS_CMD_GET_AH] = efa_everbs_cmd_get_ah,
#endif
#ifndef HAVE_IB_QUERY_DEVICE_UDATA
	[EFA_EVERBS_CMD_GET_EX_DEV_ATTRS] = efa_everbs_cmd_get_ex_dev_attrs,
#endif
};

static ssize_t efa_everbs_write(struct file *filp,
				const char __user *buf,
				size_t count,
				loff_t *pos)
{
	struct efa_dev *dev = filp->private_data;
	struct ib_uverbs_cmd_hdr hdr;

	pr_debug("---> efa ibdev %s\n", dev_name(&dev->ibdev.dev));
	if (count < sizeof(hdr))
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	if (hdr.in_words * 4 != count)
		return -EINVAL;

	if (hdr.command >= EFA_EVERBS_CMD_MAX ||
	    !efa_everbs_cmd_table[hdr.command])
		return -EINVAL;

	return efa_everbs_cmd_table[hdr.command](dev,
						 buf + sizeof(hdr),
						 hdr.in_words * 4,
						 hdr.out_words * 4);
}

static int efa_everbs_open(struct inode *inode, struct file *filp)
{
	struct efa_dev *dev;

	dev = container_of(inode->i_cdev, struct efa_dev, cdev);

	filp->private_data = dev;
	return nonseekable_open(inode, filp);
}

static int efa_everbs_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations efa_everbs_fops = {
	.owner   = THIS_MODULE,
	.write   = efa_everbs_write,
	.open    = efa_everbs_open,
	.release = efa_everbs_close,
	.llseek  = no_llseek,
};

static int efa_everbs_dev_init(struct efa_dev *dev, int devnum)
{
	dev_t devno = MKDEV(efa_everbs_major, devnum);
	int err;

	pr_debug("--->\n");
	WARN_ON(devnum >= EFA_EVERBS_MAX_DEVICES);
	cdev_init(&dev->cdev, &efa_everbs_fops);
	dev->cdev.owner = THIS_MODULE;

	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		goto err;
	dev->everbs_dev = device_create(efa_everbs_class,
					&dev->pdev->dev,
					devno,
					dev,
					EFA_EVERBS_DEVICE_NAME "%d",
					devnum);
	if (IS_ERR(dev->everbs_dev)) {
		pr_err("failed to create device: %s%d",
		       EFA_EVERBS_DEVICE_NAME, devnum);
		goto err;
	}
	return 0;
err:
	cdev_del(&dev->cdev);
	return err;
}

static void efa_everbs_dev_destroy(struct efa_dev *dev)
{
	pr_debug("--->\n");
	if (dev->everbs_dev) {
		device_destroy(efa_everbs_class, dev->cdev.dev);
		cdev_del(&dev->cdev);
		dev->everbs_dev = NULL;
	}
}
#endif /* HAVE_CUSTOM_COMMANDS */

/* This handler will called for unknown event group or unimplemented handlers */
static void unimplemented_aenq_handler(void *data,
				       struct efa_admin_aenq_entry *aenq_e)
{
	pr_err_ratelimited("Unknown event was received or event with unimplemented handler\n");
}

static void efa_keep_alive(void *data, struct efa_admin_aenq_entry *aenq_e)
{
	struct efa_dev *dev = (struct efa_dev *)data;

	dev->stats.keep_alive_rcvd++;
}

static struct efa_aenq_handlers aenq_handlers = {
	.handlers = {
		[EFA_ADMIN_KEEP_ALIVE] = efa_keep_alive,
	},
	.unimplemented_handler = unimplemented_aenq_handler
};

static void efa_release_bars(struct efa_dev *dev, int bars_mask)
{
	struct pci_dev *pdev = dev->pdev;
	int release_bars;

	release_bars = pci_select_bars(pdev, IORESOURCE_MEM) & bars_mask;
	pci_release_selected_regions(pdev, release_bars);
}

static irqreturn_t efa_intr_msix_mgmnt(int irq, void *data)
{
	struct efa_dev *dev = (struct efa_dev *)data;

	efa_com_admin_q_comp_intr_handler(dev->edev);

	/* Don't call the aenq handler before probe is done */
	if (likely(test_bit(EFA_DEVICE_RUNNING_BIT, &dev->state)))
		efa_com_aenq_intr_handler(dev->edev, data);

	return IRQ_HANDLED;
}

static int efa_request_mgmnt_irq(struct efa_dev *dev)
{
	struct efa_irq *irq;
	int err;

	irq = &dev->admin_irq;
	err = request_irq(irq->vector, irq->handler, 0, irq->name,
			  irq->data);
	if (err) {
		dev_err(&dev->pdev->dev, "failed to request admin irq (%d)\n",
			err);
		return err;
	}

	dev_dbg(&dev->pdev->dev, "set affinity hint of mgmnt irq.to 0x%lx (irq vector: %d)\n",
		irq->affinity_hint_mask.bits[0], irq->vector);
	irq_set_affinity_hint(irq->vector, &irq->affinity_hint_mask);

	return err;
}

static void efa_setup_mgmnt_irq(struct efa_dev *dev)
{
	u32 cpu;

	snprintf(dev->admin_irq.name, EFA_IRQNAME_SIZE,
		 "efa-mgmnt@pci:%s", pci_name(dev->pdev));
	dev->admin_irq.handler = efa_intr_msix_mgmnt;
	dev->admin_irq.data = dev;
	dev->admin_irq.vector =
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
		dev->admin_msix_entry.vector;
#else
		pci_irq_vector(dev->pdev, dev->admin_msix_vector_idx);
#endif
	cpu = cpumask_first(cpu_online_mask);
	dev->admin_irq.cpu = cpu;
	cpumask_set_cpu(cpu,
			&dev->admin_irq.affinity_hint_mask);
	pr_info("setup irq:%p vector:%d name:%s\n",
		&dev->admin_irq,
		dev->admin_irq.vector,
		dev->admin_irq.name);
}

static void efa_free_mgmnt_irq(struct efa_dev *dev)
{
	struct efa_irq *irq;

	irq = &dev->admin_irq;
	irq_set_affinity_hint(irq->vector, NULL);
	free_irq(irq->vector, irq->data);
}

static int efa_set_mgmnt_irq(struct efa_dev *dev)
{
	int err;

	efa_setup_mgmnt_irq(dev);

	err = efa_request_mgmnt_irq(dev);
	if (err) {
		dev_err(&dev->pdev->dev, "Can not setup management interrupts\n");
		return err;
	}

	return 0;
}

static int efa_set_doorbell_bar(struct efa_dev *dev, int db_bar_idx)
{
	struct pci_dev *pdev = dev->pdev;
	int bars;
	int err;

	dev->db_bar_idx = db_bar_idx;

	if (!(BIT(db_bar_idx) & EFA_BASE_BAR_MASK)) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM) & BIT(db_bar_idx);

		err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
		if (err) {
			dev_err(&pdev->dev, "pci_request_selected_regions for bar %d failed %d\n",
				db_bar_idx, err);
			return err;
		}
	}

	dev->db_bar_addr = pci_resource_start(dev->pdev, db_bar_idx);
	dev->db_bar_len  = pci_resource_len(dev->pdev, db_bar_idx);

	return 0;
}

static void efa_release_doorbell_bar(struct efa_dev *dev)
{
	int db_bar_idx = dev->db_bar_idx;

	if (!(BIT(db_bar_idx) & EFA_BASE_BAR_MASK))
		efa_release_bars(dev, BIT(db_bar_idx));
}

static void efa_update_hw_hints(struct efa_dev *dev,
				struct efa_com_get_hw_hints_result *hw_hints)
{
	struct efa_com_dev *edev = dev->edev;

	if (hw_hints->mmio_read_timeout)
		edev->mmio_read.mmio_read_timeout =
			hw_hints->mmio_read_timeout * 1000;

	if (hw_hints->poll_interval)
		edev->admin_queue.poll_interval = hw_hints->poll_interval;

	if (hw_hints->admin_completion_timeout)
		edev->admin_queue.completion_timeout =
			hw_hints->admin_completion_timeout;
}

static int efa_ib_device_add(struct efa_dev *dev)
{
	struct efa_com_get_network_attr_result network_attr;
	struct efa_com_get_device_attr_result device_attr;
	struct efa_com_get_hw_hints_result hw_hints;
	struct pci_dev *pdev = dev->pdev;
#ifdef HAVE_CUSTOM_COMMANDS
	int devnum;
#endif
	int err;

	mutex_init(&dev->efa_dev_lock);
	mutex_init(&dev->ah_list_lock);
	INIT_LIST_HEAD(&dev->ctx_list);
	INIT_LIST_HEAD(&dev->efa_ah_list);

	/* init IB device */
	err = efa_get_device_attributes(dev, &device_attr);
	if (err) {
		pr_err("efa_get_device_attr failed (%d)\n", err);
		return err;
	}

	efa_update_dev_cap(dev, &device_attr);

	pr_debug("Doorbells bar (%d)\n", device_attr.db_bar);
	err = efa_set_doorbell_bar(dev, device_attr.db_bar);
	if (err)
		return err;

	err = efa_bitmap_init(&dev->pd_bitmap, dev->caps.max_pd);
	if (err) {
		pr_err("efa_bitmap_init failed (%d)\n", err);
		goto err_free_doorbell_bar;
	}

	err = efa_com_get_network_attr(dev->edev, &network_attr);
	if (err) {
		pr_err("efa_com_get_network_attr failed (%d)\n", err);
		goto err_free_pd_bitmap;
	}

	efa_update_network_attr(dev, &network_attr);

	err = efa_com_get_hw_hints(dev->edev, &hw_hints);
	if (err) {
		pr_err("efa_get_hw_hints failed (%d)\n", err);
		goto err_free_pd_bitmap;
	}

	efa_update_hw_hints(dev, &hw_hints);

	/* Try to enable all the available aenq groups */
	err = efa_com_set_aenq_config(dev->edev, EFA_AENQ_ENABLED_GROUPS);
	if (err) {
		pr_err("efa_aenq_init failed (%d)\n", err);
		goto err_free_pd_bitmap;
	}

	dev->ibdev.owner = THIS_MODULE;
	dev->ibdev.node_type = RDMA_NODE_IB_CA;
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	dev->ibdev.dev.parent = &pdev->dev;
#else
	dev->ibdev.dma_device = &pdev->dev;
#endif
	dev->ibdev.uverbs_abi_ver = 3;

	dev->ibdev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_REG_MR) |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR) |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP) |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
		(1ull << IB_USER_VERBS_CMD_CREATE_AH) |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		(1ull << IB_USER_VERBS_CMD_OPEN_QP) |
#endif
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH);

#ifdef HAVE_IB_QUERY_DEVICE_UDATA
	dev->ibdev.uverbs_ex_cmd_mask =
		(1ull << IB_USER_VERBS_EX_CMD_QUERY_DEVICE);
#endif

	dev->ibdev.query_device = efa_query_device;
	dev->ibdev.query_port = efa_query_port;
	dev->ibdev.query_pkey = efa_query_pkey;
	dev->ibdev.query_gid = efa_query_gid;
	dev->ibdev.get_link_layer = efa_port_link_layer;
	dev->ibdev.alloc_pd = efa_alloc_pd;
	dev->ibdev.dealloc_pd = efa_dealloc_pd;
	dev->ibdev.create_qp = efa_create_qp;
	dev->ibdev.modify_qp = efa_modify_qp;
	dev->ibdev.query_qp = efa_query_qp;
	dev->ibdev.destroy_qp = efa_destroy_qp;
	dev->ibdev.create_cq = efa_create_cq;
	dev->ibdev.destroy_cq = efa_destroy_cq;
	dev->ibdev.reg_user_mr = efa_reg_mr;
	dev->ibdev.dereg_mr = efa_dereg_mr;
#ifdef HAVE_GET_PORT_IMMUTABLE
	dev->ibdev.get_port_immutable = efa_get_port_immutable;
#endif
	dev->ibdev.alloc_ucontext = efa_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = efa_dealloc_ucontext;
	dev->ibdev.mmap = efa_mmap;
	dev->ibdev.create_ah = efa_create_ah;
	dev->ibdev.destroy_ah = efa_destroy_ah;
	dev->ibdev.post_send = efa_post_send;
	dev->ibdev.post_recv = efa_post_recv;
	dev->ibdev.poll_cq = efa_poll_cq;
	dev->ibdev.req_notify_cq = efa_req_notify_cq;
	dev->ibdev.get_dma_mr = efa_get_dma_mr;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	strlcpy(dev->ibdev.name, "efa_%d",
		sizeof(dev->ibdev.name));

	err = ib_register_device(&dev->ibdev, NULL);
#else
	err = ib_register_device(&dev->ibdev, "efa_%d", NULL);
#endif
	if (err)
		goto err_free_pd_bitmap;

	pr_info("Registered ib device %s\n", dev_name(&dev->ibdev.dev));

#ifdef HAVE_CUSTOM_COMMANDS
	sscanf(dev_name(&dev->ibdev.dev), "efa_%d\n", &devnum);
	err = efa_everbs_dev_init(dev, devnum);
	if (err) {
		pr_err("Could not init %s%d device\n",
		       EFA_EVERBS_DEVICE_NAME, devnum);
		goto err_unregister_ibdev;
	}
	pr_info("Created everbs device %s%d",
		EFA_EVERBS_DEVICE_NAME, devnum);
#endif

	set_bit(EFA_DEVICE_RUNNING_BIT, &dev->state);

	return 0;

#ifdef HAVE_CUSTOM_COMMANDS
err_unregister_ibdev:
	ib_unregister_device(&dev->ibdev);
#endif
err_free_pd_bitmap:
	efa_bitmap_cleanup(&dev->pd_bitmap);
err_free_doorbell_bar:
	efa_release_doorbell_bar(dev);
	return err;
}

static void efa_ib_device_remove(struct efa_dev *dev)
{
	pr_debug("--->\n");
	WARN_ON(!list_empty(&dev->efa_ah_list));
	WARN_ON(!list_empty(&dev->ctx_list));
	WARN_ON(efa_bitmap_avail(&dev->pd_bitmap) != dev->caps.max_pd);

	/* Reset the device only if the device is running. */
	if (test_bit(EFA_DEVICE_RUNNING_BIT, &dev->state))
		efa_com_dev_reset(dev->edev, EFA_REGS_RESET_NORMAL);

#ifdef HAVE_CUSTOM_COMMANDS
	efa_everbs_dev_destroy(dev);
#endif
	pr_info("Unregister ib device %s\n", dev_name(&dev->ibdev.dev));
	ib_unregister_device(&dev->ibdev);
	efa_bitmap_cleanup(&dev->pd_bitmap);
	efa_release_doorbell_bar(dev);
	pr_debug("<---\n");
}

#ifdef HAVE_CUSTOM_COMMANDS
static char *efa_everbs_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}
#endif

static void efa_disable_msix(struct efa_dev *dev)
{
	pr_debug("--->\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	if (test_and_clear_bit(EFA_MSIX_ENABLED_BIT, &dev->state))
		pci_disable_msix(dev->pdev);
#else
	if (test_and_clear_bit(EFA_MSIX_ENABLED_BIT, &dev->state))
		pci_free_irq_vectors(dev->pdev);
#endif
}

static int efa_enable_msix(struct efa_dev *dev)
{
	int msix_vecs, irq_num;

	if (test_bit(EFA_MSIX_ENABLED_BIT, &dev->state)) {
		dev_err(&dev->pdev->dev, "Error, MSI-X is already enabled\n");
		return -EPERM;
	}

	/* Reserve the max msix vectors we might need */
	msix_vecs = EFA_NUM_MSIX_VEC;
	dev_dbg(&dev->pdev->dev, "trying to enable MSI-X, vectors %d\n",
		msix_vecs);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	dev->admin_msix_entry.entry = EFA_MGMNT_MSIX_VEC_IDX;
	irq_num = pci_enable_msix_range(dev->pdev,
					&dev->admin_msix_entry,
					msix_vecs, msix_vecs);
#else
	dev->admin_msix_vector_idx = EFA_MGMNT_MSIX_VEC_IDX;
	irq_num = pci_alloc_irq_vectors(dev->pdev, msix_vecs,
					msix_vecs, PCI_IRQ_MSIX);
#endif

	if (irq_num < 0) {
		dev_err(&dev->pdev->dev, "Failed to enable MSI-X. irq_num %d\n",
			irq_num);
		return -ENOSPC;
	}

	if (irq_num != msix_vecs) {
		dev_warn(&dev->pdev->dev,
			 "Allocated %d MSI-X (out of %d requested)\n",
			 irq_num, msix_vecs);
		return -ENOSPC;
	}

	set_bit(EFA_MSIX_ENABLED_BIT, &dev->state);

	return 0;
}

static int efa_device_init(struct efa_com_dev *edev, struct pci_dev *pdev)
{
	int dma_width;
	int err;

	dev_dbg(&pdev->dev, "%s(): ---->\n", __func__);

	err = efa_com_dev_reset(edev, EFA_REGS_RESET_NORMAL);
	if (err) {
		dev_err(&pdev->dev, "Can not reset device\n");
		return err;
	}

	err = efa_com_validate_version(edev);
	if (err) {
		dev_err(&pdev->dev, "device version is too low\n");
		return err;
	}

	dma_width = efa_com_get_dma_width(edev);
	if (dma_width < 0) {
		dev_err(&pdev->dev, "Invalid dma width value %d", dma_width);
		err = dma_width;
		return err;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(dma_width));
	if (err) {
		dev_err(&pdev->dev, "pci_set_dma_mask failed 0x%x\n", err);
		return err;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(dma_width));
	if (err) {
		dev_err(&pdev->dev,
			"err_pci_set_consistent_dma_mask failed 0x%x\n",
			err);
		return err;
	}

	return 0;
}

static int efa_probe_device(struct pci_dev *pdev)
{
	struct efa_com_dev *edev;
	struct efa_dev *dev;
	int bars;
	int err;

	dev_dbg(&pdev->dev, "%s(): --->\n", __func__);

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device_mem() failed!\n");
		return err;
	}

	pci_set_master(pdev);

	dev = (struct efa_dev *)ib_alloc_device(sizeof(struct efa_dev));
	if (IS_ERR_OR_NULL(dev)) {
		dev_err(&pdev->dev, "Device %s alloc failed\n",
			dev_name(&pdev->dev));
		err = dev ? PTR_ERR(dev) : -ENOMEM;
		goto err_disable_device;
	}

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev) {
		err = -ENOMEM;
		goto err_ibdev_destroy;
	}

	pci_set_drvdata(pdev, dev);
	edev->dmadev = &pdev->dev;
	dev->edev = edev;
	dev->pdev = pdev;

	bars = pci_select_bars(pdev, IORESOURCE_MEM) & EFA_BASE_BAR_MASK;
	err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "pci_request_selected_regions failed %d\n",
			err);
		goto err_free_efa_dev;
	}

	dev->reg_bar_addr = pci_resource_start(pdev, EFA_REG_BAR);
	dev->reg_bar_len = pci_resource_len(pdev, EFA_REG_BAR);
	dev->mem_bar_addr = pci_resource_start(pdev, EFA_MEM_BAR);
	dev->mem_bar_len = pci_resource_len(pdev, EFA_MEM_BAR);

	edev->reg_bar = devm_ioremap(&pdev->dev,
				     dev->reg_bar_addr,
				     dev->reg_bar_len);
	if (!edev->reg_bar) {
		dev_err(&pdev->dev, "failed to remap regs bar\n");
		err = -EFAULT;
		goto err_release_bars;
	}

	err = efa_com_mmio_reg_read_init(edev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init readless MMIO\n");
		goto err_iounmap;
	}

	err = efa_device_init(edev, pdev);
	if (err) {
		dev_err(&pdev->dev, "efa device init failed\n");
		if (err == -ETIME)
			err = -EPROBE_DEFER;
		goto err_reg_read_destroy;
	}

	err = efa_enable_msix(dev);
	if (err) {
		dev_err(&pdev->dev, "Can not reserve msix vectors\n");
		goto err_reg_read_destroy;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	edev->admin_queue.msix_vector_idx = dev->admin_msix_vector_idx;
	edev->aenq.msix_vector_idx = dev->admin_msix_vector_idx;
#else
	edev->admin_queue.msix_vector_idx = dev->admin_msix_entry.entry;
	edev->aenq.msix_vector_idx = dev->admin_msix_entry.entry;
#endif

	err = efa_set_mgmnt_irq(dev);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to enable and set the management interrupts\n");
		goto err_disable_msix;
	}

	err = efa_com_admin_init(edev, &aenq_handlers);
	if (err) {
		dev_err(&pdev->dev,
			"Can not initialize efa admin queue with device\n");
		goto err_free_mgmnt_irq;
	}

	err = efa_sysfs_init(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot initialize sysfs\n");
		goto err_admin_destroy;
	}

	dev_dbg(&pdev->dev, "%s(): <---\n", __func__);
	return 0;

err_admin_destroy:
	efa_com_admin_destroy(edev);
err_free_mgmnt_irq:
	efa_free_mgmnt_irq(dev);
err_disable_msix:
	efa_disable_msix(dev);
err_reg_read_destroy:
	efa_com_mmio_reg_read_destroy(edev);
err_iounmap:
	devm_iounmap(&pdev->dev, edev->reg_bar);
err_release_bars:
	efa_release_bars(dev, EFA_BASE_BAR_MASK);
err_free_efa_dev:
	kfree(edev);
err_ibdev_destroy:
	ib_dealloc_device(&dev->ibdev);
err_disable_device:
	pci_disable_device(pdev);
	return err;
}

static void efa_remove_device(struct pci_dev *pdev)
{
	struct efa_dev *dev = pci_get_drvdata(pdev);
	struct efa_com_dev *edev;

	dev_dbg(&pdev->dev, "%s(): --->\n", __func__);
	if (!dev)
		/*
		 * This device didn't load properly and its resources
		 * already released, nothing to do
		 */
		return;

	edev = dev->edev;

	efa_sysfs_destroy(dev);
	efa_com_admin_destroy(edev);
	efa_free_mgmnt_irq(dev);
	efa_disable_msix(dev);
	efa_com_mmio_reg_read_destroy(edev);
	devm_iounmap(&pdev->dev, edev->reg_bar);
	efa_release_bars(dev, EFA_BASE_BAR_MASK);
	kfree(edev);
	ib_dealloc_device(&dev->ibdev);
	pci_disable_device(pdev);
	dev_dbg(&pdev->dev, "%s(): <---\n", __func__);
}

static int efa_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct efa_dev *dev;
	int err;

	dev_dbg(&pdev->dev, "%s(): --->\n", __func__);
	err = efa_probe_device(pdev);
	if (err)
		return err;

	dev = pci_get_drvdata(pdev);
	err = efa_ib_device_add(dev);
	if (err)
		goto err_remove_device;

	return 0;

err_remove_device:
	efa_remove_device(pdev);
	return err;
}

static void efa_remove(struct pci_dev *pdev)
{
	struct efa_dev *dev = (struct efa_dev *)pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s(): --->\n", __func__);
	efa_ib_device_remove(dev);
	efa_remove_device(pdev);
}

static struct pci_driver efa_pci_driver = {
	.name           = DRV_MODULE_NAME,
	.id_table       = efa_pci_tbl,
	.probe          = efa_probe,
	.remove         = efa_remove,
};

static int __init efa_init(void)
{
#ifdef HAVE_CUSTOM_COMMANDS
	dev_t dev;
#endif
	int err;

	pr_info("EFA: version %s\n", version);
#ifdef HAVE_CUSTOM_COMMANDS
	err = alloc_chrdev_region(&dev, 0, EFA_EVERBS_MAX_DEVICES,
				  EFA_EVERBS_DEVICE_NAME);
	if (err) {
		pr_err("couldn't allocate efa_everbs device numbers\n");
		goto out;
	}
	efa_everbs_major = MAJOR(dev);

	efa_everbs_class = class_create(THIS_MODULE, EFA_EVERBS_DEVICE_NAME);
	if (IS_ERR(efa_everbs_class)) {
		err = PTR_ERR(efa_everbs_class);
		pr_err("couldn't create efa_everbs class\n");
		goto err_class;
	}
	efa_everbs_class->devnode = efa_everbs_devnode;
#endif

	err = pci_register_driver(&efa_pci_driver);
	if (err) {
		pr_err("couldn't register efa driver\n");
		goto err_register;
	}

	pr_debug("<---\n");
	return 0;

err_register:
#ifdef HAVE_CUSTOM_COMMANDS
	class_destroy(efa_everbs_class);
err_class:
	unregister_chrdev_region(dev, EFA_EVERBS_MAX_DEVICES);
out:
#endif
	return err;
}

static void __exit efa_exit(void)
{
	pr_debug("--->\n");
	pci_unregister_driver(&efa_pci_driver);
#ifdef HAVE_CUSTOM_COMMANDS
	class_destroy(efa_everbs_class);
	unregister_chrdev_region(MKDEV(efa_everbs_major, 0),
				 EFA_EVERBS_MAX_DEVICES);
#endif
}

module_init(efa_init);
module_exit(efa_exit);

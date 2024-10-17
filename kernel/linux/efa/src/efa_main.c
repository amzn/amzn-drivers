// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "kcompat.h"
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/utsname.h>
#include <linux/version.h>

#include <rdma/ib_user_verbs.h>
#ifdef HAVE_IB_DEVICE_DRIVER_DEF
#include <rdma/uverbs_ioctl.h>
#endif

#include "efa.h"
#include "efa_sysfs.h"

#ifdef HAVE_EFA_P2P
#include "efa_p2p.h"
#endif

#ifndef HAVE_PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif
#define PCI_DEV_ID_EFA0_VF 0xefa0
#define PCI_DEV_ID_EFA1_VF 0xefa1
#define PCI_DEV_ID_EFA2_VF 0xefa2
#define PCI_DEV_ID_EFA3_VF 0xefa3

static const struct pci_device_id efa_pci_tbl[] = {
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA0_VF) },
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA1_VF) },
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA2_VF) },
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA3_VF) },
	{ }
};

#define DRV_MODULE_VER_MAJOR           2
#define DRV_MODULE_VER_MINOR           12
#define DRV_MODULE_VER_SUBMINOR        1

#ifndef DRV_MODULE_VERSION
#define DRV_MODULE_VERSION \
	__stringify(DRV_MODULE_VER_MAJOR) "."   \
	__stringify(DRV_MODULE_VER_MINOR) "."   \
	__stringify(DRV_MODULE_VER_SUBMINOR) "g"
#endif

MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_SOFTDEP("pre: ib_uverbs");

static char version[] = DEVICE_NAME " v" DRV_MODULE_VERSION;

MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DEVICE_NAME);
MODULE_DEVICE_TABLE(pci, efa_pci_tbl);

#if !defined(HAVE_EFA_KVERBS) && !defined(HAVE_NO_KVERBS_DRIVERS)
#error "Build without kernel verbs isn't supported in this kernel"
#endif

#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

#ifdef HAVE_IB_DEVICE_DRIVER_DEF
extern const struct uapi_definition efa_uapi_defs[];
#endif

/* This handler will called for unknown event group or unimplemented handlers */
static void unimplemented_aenq_handler(void *data,
				       struct efa_admin_aenq_entry *aenq_e)
{
	struct efa_dev *dev = (struct efa_dev *)data;

	ibdev_err(&dev->ibdev,
		  "Unknown event was received or event with unimplemented handler\n");
}

static void efa_keep_alive(void *data, struct efa_admin_aenq_entry *aenq_e)
{
	struct efa_dev *dev = (struct efa_dev *)data;

	atomic64_inc(&dev->stats.keep_alive_rcvd);
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

static void efa_process_comp_eqe(struct efa_dev *dev, struct efa_admin_eqe *eqe)
{
	u16 cqn = eqe->u.comp_event.cqn;
	struct efa_cq *cq;

#ifdef HAVE_XARRAY
	/* Safe to load as we're in irq and removal calls synchronize_irq() */
	cq = xa_load(&dev->cqs_xa, cqn);
#else
	cq = dev->cqs_arr[cqn];
#endif
	if (unlikely(!cq)) {
		ibdev_err_ratelimited(&dev->ibdev,
				      "Completion event on non-existent CQ[%u]",
				      cqn);
		return;
	}

#ifdef HAVE_EFA_KVERBS
	cq->cmd_sn++;
#endif
	cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

static void efa_process_eqe(struct efa_com_eq *eeq, struct efa_admin_eqe *eqe)
{
	struct efa_dev *dev = container_of(eeq->edev, struct efa_dev, edev);

	if (likely(EFA_GET(&eqe->common, EFA_ADMIN_EQE_EVENT_TYPE) ==
			   EFA_ADMIN_EQE_EVENT_TYPE_COMPLETION))
		efa_process_comp_eqe(dev, eqe);
	else
		ibdev_err_ratelimited(&dev->ibdev,
				      "Unknown event type received %lu",
				      EFA_GET(&eqe->common,
					      EFA_ADMIN_EQE_EVENT_TYPE));
}

static irqreturn_t efa_intr_msix_comp(int irq, void *data)
{
	struct efa_eq *eq = data;
	struct efa_com_dev *edev = eq->eeq.edev;

	efa_com_eq_comp_intr_handler(edev, &eq->eeq);

	return IRQ_HANDLED;
}

static irqreturn_t efa_intr_msix_mgmnt(int irq, void *data)
{
	struct efa_dev *dev = data;

	efa_com_admin_q_comp_intr_handler(&dev->edev);
	efa_com_aenq_intr_handler(&dev->edev, data);

	return IRQ_HANDLED;
}

static int efa_request_irq(struct efa_dev *dev, struct efa_irq *irq)
{
	int err;

	err = request_irq(irq->irqn, irq->handler, 0, irq->name, irq->data);
	if (err) {
		dev_err(&dev->pdev->dev, "Failed to request irq %s (%d)\n",
			irq->name, err);
		return err;
	}

	irq_set_affinity_hint(irq->irqn, &irq->affinity_hint_mask);

	return 0;
}

static void efa_setup_comp_irq(struct efa_dev *dev, struct efa_eq *eq,
			       int vector)
{
	u32 cpu;

	cpu = vector - EFA_COMP_EQS_VEC_BASE;
	snprintf(eq->irq.name, EFA_IRQNAME_SIZE, "efa-comp%d@pci:%s", cpu,
		 pci_name(dev->pdev));
	eq->irq.handler = efa_intr_msix_comp;
	eq->irq.data = eq;
	eq->irq.vector = vector;
	eq->irq.irqn = pci_irq_vector(dev->pdev, vector);
	cpumask_set_cpu(cpu, &eq->irq.affinity_hint_mask);
}

static void efa_free_irq(struct efa_dev *dev, struct efa_irq *irq)
{
	irq_set_affinity_hint(irq->irqn, NULL);
	free_irq(irq->irqn, irq->data);
}

static void efa_setup_mgmnt_irq(struct efa_dev *dev)
{
	u32 cpu;

	snprintf(dev->admin_irq.name, EFA_IRQNAME_SIZE,
		 "efa-mgmnt@pci:%s", pci_name(dev->pdev));
	dev->admin_irq.handler = efa_intr_msix_mgmnt;
	dev->admin_irq.data = dev;
	dev->admin_irq.vector = dev->admin_msix_vector_idx;
	dev->admin_irq.irqn = pci_irq_vector(dev->pdev,
					     dev->admin_msix_vector_idx);
	cpu = cpumask_first(cpu_online_mask);
	cpumask_set_cpu(cpu,
			&dev->admin_irq.affinity_hint_mask);
	dev_info(&dev->pdev->dev, "Setup irq:%d name:%s\n",
		 dev->admin_irq.irqn,
		 dev->admin_irq.name);
}

static int efa_set_mgmnt_irq(struct efa_dev *dev)
{
	efa_setup_mgmnt_irq(dev);

	return efa_request_irq(dev, &dev->admin_irq);
}

static int efa_request_doorbell_bar(struct efa_dev *dev)
{
	u8 db_bar_idx = dev->dev_attr.db_bar;
	struct pci_dev *pdev = dev->pdev;
	int pci_mem_bars;
	int db_bar;
	int err;

	db_bar = BIT(db_bar_idx);
	if (!(db_bar & EFA_BASE_BAR_MASK)) {
		pci_mem_bars = pci_select_bars(pdev, IORESOURCE_MEM);
		if (db_bar & ~pci_mem_bars) {
			dev_err(&pdev->dev,
				"Doorbells BAR unavailable. Requested %#x, available %#x\n",
				db_bar, pci_mem_bars);
			return -ENODEV;
		}

		err = pci_request_selected_regions(pdev, db_bar, DRV_MODULE_NAME);
		if (err) {
			dev_err(&pdev->dev,
				"pci_request_selected_regions for bar %d failed %d\n",
				db_bar_idx, err);
			return err;
		}
	}

	dev->db_bar_addr = pci_resource_start(dev->pdev, db_bar_idx);
	dev->db_bar_len = pci_resource_len(dev->pdev, db_bar_idx);
#ifdef HAVE_EFA_KVERBS
	dev->db_bar = devm_ioremap(&pdev->dev, dev->db_bar_addr, dev->db_bar_len);
#endif

	return 0;
}

static void efa_release_doorbell_bar(struct efa_dev *dev)
{
	if (!(BIT(dev->dev_attr.db_bar) & EFA_BASE_BAR_MASK))
		efa_release_bars(dev, BIT(dev->dev_attr.db_bar));
}

static void efa_update_hw_hints(struct efa_dev *dev,
				struct efa_com_get_hw_hints_result *hw_hints)
{
	struct efa_com_dev *edev = &dev->edev;

	if (hw_hints->mmio_read_timeout)
		edev->mmio_read.mmio_read_timeout =
			hw_hints->mmio_read_timeout * 1000;

	if (hw_hints->poll_interval)
		edev->aq.poll_interval = hw_hints->poll_interval;

	if (hw_hints->admin_completion_timeout)
		edev->aq.completion_timeout =
			hw_hints->admin_completion_timeout;
}

static void efa_stats_init(struct efa_dev *dev)
{
	atomic64_t *s = (atomic64_t *)&dev->stats;
	int i;

	for (i = 0; i < sizeof(dev->stats) / sizeof(*s); i++, s++)
		atomic64_set(s, 0);
}

static void efa_set_host_info(struct efa_dev *dev)
{
	struct efa_admin_set_feature_resp resp = {};
	struct efa_admin_set_feature_cmd cmd = {};
	struct efa_admin_host_info *hinf;
	u32 bufsz = sizeof(*hinf);
	dma_addr_t hinf_dma;

	if (!efa_com_check_supported_feature_id(&dev->edev,
						EFA_ADMIN_HOST_INFO))
		return;

	/* Failures in host info set shall not disturb probe */
	hinf = dma_alloc_coherent(&dev->pdev->dev, bufsz, &hinf_dma,
				  GFP_KERNEL);
	if (!hinf)
		return;

	strscpy(hinf->os_dist_str, utsname()->release,
		sizeof(hinf->os_dist_str));
	hinf->os_type = EFA_ADMIN_OS_LINUX;
	strscpy(hinf->kernel_ver_str, utsname()->version,
		sizeof(hinf->kernel_ver_str));
	hinf->kernel_ver = LINUX_VERSION_CODE;
	EFA_SET(&hinf->driver_ver, EFA_ADMIN_HOST_INFO_DRIVER_MAJOR,
		DRV_MODULE_VER_MAJOR);
	EFA_SET(&hinf->driver_ver, EFA_ADMIN_HOST_INFO_DRIVER_MINOR,
		DRV_MODULE_VER_MINOR);
	EFA_SET(&hinf->driver_ver, EFA_ADMIN_HOST_INFO_DRIVER_SUB_MINOR,
		DRV_MODULE_VER_SUBMINOR);
	EFA_SET(&hinf->driver_ver, EFA_ADMIN_HOST_INFO_DRIVER_MODULE_TYPE,
		"g"[0]);
	EFA_SET(&hinf->bdf, EFA_ADMIN_HOST_INFO_BUS, dev->pdev->bus->number);
	EFA_SET(&hinf->bdf, EFA_ADMIN_HOST_INFO_DEVICE,
		PCI_SLOT(dev->pdev->devfn));
	EFA_SET(&hinf->bdf, EFA_ADMIN_HOST_INFO_FUNCTION,
		PCI_FUNC(dev->pdev->devfn));
	EFA_SET(&hinf->spec_ver, EFA_ADMIN_HOST_INFO_SPEC_MAJOR,
		EFA_COMMON_SPEC_VERSION_MAJOR);
	EFA_SET(&hinf->spec_ver, EFA_ADMIN_HOST_INFO_SPEC_MINOR,
		EFA_COMMON_SPEC_VERSION_MINOR);
#ifdef HAVE_EFA_P2P
	EFA_SET(&hinf->flags, EFA_ADMIN_HOST_INFO_GDR, 1);
#else
	EFA_SET(&hinf->flags, EFA_ADMIN_HOST_INFO_GDR, 0);
#endif

	efa_com_set_feature_ex(&dev->edev, &resp, &cmd, EFA_ADMIN_HOST_INFO,
			       hinf_dma, bufsz);

	dma_free_coherent(&dev->pdev->dev, bufsz, hinf, hinf_dma);
}

static void efa_destroy_eq(struct efa_dev *dev, struct efa_eq *eq)
{
	efa_com_eq_destroy(&dev->edev, &eq->eeq);
	efa_free_irq(dev, &eq->irq);
}

static int efa_create_eq(struct efa_dev *dev, struct efa_eq *eq, u8 msix_vec)
{
	int err;

	efa_setup_comp_irq(dev, eq, msix_vec);
	err = efa_request_irq(dev, &eq->irq);
	if (err)
		return err;

	err = efa_com_eq_init(&dev->edev, &eq->eeq, efa_process_eqe,
			      dev->dev_attr.max_eq_depth, msix_vec);
	if (err)
		goto err_free_comp_irq;

	return 0;

err_free_comp_irq:
	efa_free_irq(dev, &eq->irq);
	return err;
}

static int efa_create_eqs(struct efa_dev *dev)
{
	unsigned int neqs = dev->dev_attr.max_eq;
	int err;
	int i;

	neqs = min_t(unsigned int, neqs,
		     dev->num_irq_vectors - EFA_COMP_EQS_VEC_BASE);

	dev->neqs = neqs;
	dev->eqs = kcalloc(neqs, sizeof(*dev->eqs), GFP_KERNEL);
	if (!dev->eqs)
		return -ENOMEM;

	for (i = 0; i < neqs; i++) {
		err = efa_create_eq(dev, &dev->eqs[i],
				    i + EFA_COMP_EQS_VEC_BASE);
		if (err)
			goto err_destroy_eqs;
	}

	return 0;

err_destroy_eqs:
	for (i--; i >= 0; i--)
		efa_destroy_eq(dev, &dev->eqs[i]);
	kfree(dev->eqs);

	return err;
}

static void efa_destroy_eqs(struct efa_dev *dev)
{
	int i;

	for (i = 0; i < dev->neqs; i++)
		efa_destroy_eq(dev, &dev->eqs[i]);

	kfree(dev->eqs);
}

#ifdef HAVE_EFA_KVERBS
static int efa_alloc_dev_uar(struct efa_dev *dev)
{
	struct efa_com_alloc_uar_result res = {};
	int err;

	err = efa_com_alloc_uar(&dev->edev, &res);
	if (err) {
		dev_err(&dev->pdev->dev, "Failed to allocate UAR, err %d\n", err);
		return err;
	}

	dev->uarn = res.uarn;
	return 0;
}

static int efa_dealloc_dev_uar(struct efa_dev *dev)
{
	struct efa_com_dealloc_uar_params params = {
		.uarn = dev->uarn,
	};

	return efa_com_dealloc_uar(&dev->edev, &params);
}

static int efa_create_qp_table(struct efa_dev *dev)
{
	u32 qp_table_size;

	qp_table_size = roundup_pow_of_two(dev->dev_attr.max_qp);
	dev->qp_table = kvzalloc(qp_table_size * sizeof(*dev->qp_table), GFP_KERNEL);
	if (!dev->qp_table) {
		dev_err(&dev->pdev->dev, "Failed to allocate QP table\n");
		return -ENOMEM;
	}
	dev->qp_table_mask = qp_table_size - 1;
	spin_lock_init(&dev->qp_table_lock);

	return 0;
}
#endif

#ifdef HAVE_IB_DEV_OPS
static const struct ib_device_ops efa_dev_ops = {
#ifdef HAVE_IB_DEVICE_OPS_COMMON
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_EFA,
	.uverbs_abi_ver = EFA_UVERBS_ABI_VERSION,
#endif

#ifdef HAVE_SPLIT_STATS_ALLOC
	.alloc_hw_port_stats = efa_alloc_hw_port_stats,
	.alloc_hw_device_stats = efa_alloc_hw_device_stats,
#else
	.alloc_hw_stats = efa_alloc_hw_stats,
#endif
#ifdef HAVE_PD_CORE_ALLOCATION
	.alloc_pd = efa_alloc_pd,
#else
	.alloc_pd = efa_kzalloc_pd,
#endif
#ifdef HAVE_UCONTEXT_CORE_ALLOCATION
	.alloc_ucontext = efa_alloc_ucontext,
#else
	.alloc_ucontext = efa_kzalloc_ucontext,
#endif
#if !defined(HAVE_UVERBS_CMD_MASK_NOT_NEEDED) || defined(HAVE_EFA_KVERBS)
#ifdef HAVE_AH_CORE_ALLOCATION
	.create_ah = efa_create_ah,
#else
	.create_ah = efa_kzalloc_ah,
#endif
#endif
#ifdef HAVE_CQ_CORE_ALLOCATION
	.create_cq = efa_create_cq,
#else
	.create_cq = efa_kzalloc_cq,
#endif
#ifdef HAVE_QP_CORE_ALLOCATION
	.create_qp = efa_create_qp,
#else
	.create_qp = efa_kzalloc_qp,
#endif
#ifdef HAVE_UVERBS_CMD_MASK_NOT_NEEDED
	.create_user_ah = efa_create_ah,
#endif
	.dealloc_pd = efa_dealloc_pd,
	.dealloc_ucontext = efa_dealloc_ucontext,
	.dereg_mr = efa_dereg_mr,
	.destroy_ah = efa_destroy_ah,
	.destroy_cq = efa_destroy_cq,
	.destroy_qp = efa_destroy_qp,
	.get_hw_stats = efa_get_hw_stats,
	.get_link_layer = efa_port_link_layer,
	.get_port_immutable = efa_get_port_immutable,
	.mmap = efa_mmap,
#ifdef HAVE_CORE_MMAP_XA
	.mmap_free = efa_mmap_free,
#endif
	.modify_qp = efa_modify_qp,
	.query_device = efa_query_device,
	.query_gid = efa_query_gid,
	.query_pkey = efa_query_pkey,
	.query_port = efa_query_port,
	.query_qp = efa_query_qp,
	.reg_user_mr = efa_reg_mr,
#ifdef HAVE_MR_DMABUF
	.reg_user_mr_dmabuf = efa_reg_user_mr_dmabuf,
#endif
#ifdef HAVE_EFA_KVERBS
	.get_dma_mr = efa_get_dma_mr,
	.alloc_mr = efa_alloc_fast_mr,
	.map_mr_sg = efa_map_mr_sg,
	.post_send = efa_post_send,
	.post_recv = efa_post_recv,
	.poll_cq = efa_poll_cq,
	.req_notify_cq = efa_req_notify_cq,
#endif

#ifdef HAVE_AH_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_ah, efa_ah, ibah),
#endif
#ifdef HAVE_CQ_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_cq, efa_cq, ibcq),
#endif
#ifdef HAVE_PD_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_pd, efa_pd, ibpd),
#endif
#ifdef HAVE_QP_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_qp, efa_qp, ibqp),
#endif
#ifdef HAVE_UCONTEXT_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_ucontext, efa_ucontext, ibucontext),
#endif
};
#endif

static int efa_ib_device_add(struct efa_dev *dev)
{
	struct efa_com_get_hw_hints_result hw_hints;
	struct pci_dev *pdev = dev->pdev;
	int err;

	efa_stats_init(dev);

	err = efa_com_get_device_attr(&dev->edev, &dev->dev_attr);
	if (err)
		return err;

	dev_dbg(&dev->pdev->dev, "Doorbells bar (%d)\n", dev->dev_attr.db_bar);
	err = efa_request_doorbell_bar(dev);
	if (err)
		return err;

	err = efa_com_get_hw_hints(&dev->edev, &hw_hints);
	if (err)
		goto err_release_doorbell_bar;

	efa_update_hw_hints(dev, &hw_hints);

	/* Try to enable all the available aenq groups */
	err = efa_com_set_aenq_config(&dev->edev, EFA_AENQ_ENABLED_GROUPS);
	if (err)
		goto err_release_doorbell_bar;

	err = efa_create_eqs(dev);
	if (err)
		goto err_release_doorbell_bar;

	efa_set_host_info(dev);

	dev->ibdev.node_type = RDMA_NODE_UNSPECIFIED;
	dev->ibdev.node_guid = dev->dev_attr.guid;
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = dev->neqs ?: 1;
#ifdef HAVE_DEV_PARENT
	dev->ibdev.dev.parent = &pdev->dev;
#else
	dev->ibdev.dma_device = &pdev->dev;
#endif

#ifndef HAVE_UVERBS_CMD_MASK_NOT_NEEDED
	dev->ibdev.uverbs_cmd_mask |=
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
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH);
#endif

#ifndef HAVE_UVERBS_CMD_MASK_NOT_NEEDED
	dev->ibdev.uverbs_ex_cmd_mask =
		(1ull << IB_USER_VERBS_EX_CMD_QUERY_DEVICE);
#endif

#ifndef HAVE_IB_DEVICE_OPS_COMMON
#ifdef HAVE_DRIVER_ID
	dev->ibdev.driver_id = RDMA_DRIVER_EFA;
#endif
	dev->ibdev.uverbs_abi_ver = EFA_UVERBS_ABI_VERSION;
	dev->ibdev.owner = THIS_MODULE;
#endif
#ifdef HAVE_IB_DEV_OPS
	ib_set_device_ops(&dev->ibdev, &efa_dev_ops);
#else
	dev->ibdev.alloc_hw_stats = efa_alloc_hw_stats;
	dev->ibdev.alloc_pd = efa_kzalloc_pd;
	dev->ibdev.alloc_ucontext = efa_kzalloc_ucontext;
	dev->ibdev.create_ah = efa_kzalloc_ah;
	dev->ibdev.create_cq = efa_kzalloc_cq;
	dev->ibdev.create_qp = efa_kzalloc_qp;
	dev->ibdev.dealloc_pd = efa_dealloc_pd;
	dev->ibdev.dealloc_ucontext = efa_dealloc_ucontext;
	dev->ibdev.dereg_mr = efa_dereg_mr;
	dev->ibdev.destroy_ah = efa_destroy_ah;
	dev->ibdev.destroy_cq = efa_destroy_cq;
	dev->ibdev.destroy_qp = efa_destroy_qp;
	dev->ibdev.get_hw_stats = efa_get_hw_stats;
	dev->ibdev.get_link_layer = efa_port_link_layer;
	dev->ibdev.get_port_immutable = efa_get_port_immutable;
	dev->ibdev.mmap = efa_mmap;
	dev->ibdev.modify_qp = efa_modify_qp;
	dev->ibdev.query_device = efa_query_device;
	dev->ibdev.query_gid = efa_query_gid;
	dev->ibdev.query_pkey = efa_query_pkey;
	dev->ibdev.query_port = efa_query_port;
	dev->ibdev.query_qp = efa_query_qp;
	dev->ibdev.reg_user_mr = efa_reg_mr;
#ifdef HAVE_EFA_KVERBS
	dev->ibdev.get_dma_mr = efa_get_dma_mr;
	dev->ibdev.alloc_mr = efa_alloc_fast_mr;
	dev->ibdev.map_mr_sg = efa_map_mr_sg;
	dev->ibdev.post_send = efa_post_send;
	dev->ibdev.post_recv = efa_post_recv;
	dev->ibdev.poll_cq = efa_poll_cq;
	dev->ibdev.req_notify_cq = efa_req_notify_cq;
#endif
#endif

#ifdef HAVE_IB_DEVICE_DRIVER_DEF
	dev->ibdev.driver_def = efa_uapi_defs;
#endif

#ifdef HAVE_EFA_KVERBS
	err = efa_alloc_dev_uar(dev);
	if (err)
		goto err_destroy_eqs;

	err = efa_create_qp_table(dev);
	if (err)
		goto err_dealloc_uar;
#endif
#ifdef HAVE_IB_REGISTER_DEVICE_DMA_DEVICE_PARAM
	err = ib_register_device(&dev->ibdev, "efa_%d", &pdev->dev);
#elif defined(HAVE_IB_REGISTER_DEVICE_TWO_PARAMS)
	err = ib_register_device(&dev->ibdev, "efa_%d");
#elif defined(HAVE_IB_REGISTER_DEVICE_NAME_PARAM)
	err = ib_register_device(&dev->ibdev, "efa_%d", NULL);
#else
	strscpy(dev->ibdev.name, "efa_%d",
		sizeof(dev->ibdev.name));

	err = ib_register_device(&dev->ibdev, NULL);
#endif
	if (err)
#ifdef HAVE_EFA_KVERBS
		goto err_free_qp_table;
#else
		goto err_destroy_eqs;
#endif

	ibdev_info(&dev->ibdev, "IB device registered\n");

	return 0;

#ifdef HAVE_EFA_KVERBS
err_free_qp_table:
	kfree(dev->qp_table);
err_dealloc_uar:
	efa_dealloc_dev_uar(dev);
#endif
err_destroy_eqs:
	efa_destroy_eqs(dev);
err_release_doorbell_bar:
	efa_release_doorbell_bar(dev);
	return err;
}

static void efa_ib_device_remove(struct efa_dev *dev)
{
	ibdev_info(&dev->ibdev, "Unregister ib device\n");
	ib_unregister_device(&dev->ibdev);
#ifdef HAVE_EFA_KVERBS
	kfree(dev->qp_table);
	efa_dealloc_dev_uar(dev);
#endif
	efa_destroy_eqs(dev);
	efa_com_dev_reset(&dev->edev, EFA_REGS_RESET_NORMAL);
	efa_release_doorbell_bar(dev);
}

static void efa_disable_msix(struct efa_dev *dev)
{
	pci_free_irq_vectors(dev->pdev);
}

static int efa_enable_msix(struct efa_dev *dev)
{
	int max_vecs, num_vecs;

	/*
	 * Reserve the max msix vectors we might need, one vector is reserved
	 * for admin.
	 */
	max_vecs = min_t(int, pci_msix_vec_count(dev->pdev),
			 num_online_cpus() + 1);
	dev_dbg(&dev->pdev->dev, "Trying to enable MSI-X, vectors %d\n",
		max_vecs);

	dev->admin_msix_vector_idx = EFA_MGMNT_MSIX_VEC_IDX;
	num_vecs = pci_alloc_irq_vectors(dev->pdev, 1,
					 max_vecs, PCI_IRQ_MSIX);

	if (num_vecs < 0) {
		dev_err(&dev->pdev->dev, "Failed to enable MSI-X. error %d\n",
			num_vecs);
		return -ENOSPC;
	}

	dev_dbg(&dev->pdev->dev, "Allocated %d MSI-X vectors\n", num_vecs);

	dev->num_irq_vectors = num_vecs;

	return 0;
}

static int efa_device_init(struct efa_com_dev *edev, struct pci_dev *pdev)
{
	int dma_width;
	int err;

	err = efa_com_dev_reset(edev, EFA_REGS_RESET_NORMAL);
	if (err)
		return err;

	err = efa_com_validate_version(edev);
	if (err)
		return err;

	dma_width = efa_com_get_dma_width(edev);
	if (dma_width < 0) {
		err = dma_width;
		return err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(dma_width));
	if (err) {
		dev_err(&pdev->dev, "dma_set_mask_and_coherent failed %d\n", err);
		return err;
	}

	dma_set_max_seg_size(&pdev->dev, UINT_MAX);
	return 0;
}

static struct efa_dev *efa_probe_device(struct pci_dev *pdev)
{
	struct efa_com_dev *edev;
	struct efa_dev *dev;
	int pci_mem_bars;
	int err;

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device_mem() failed!\n");
		return ERR_PTR(err);
	}

	pci_set_master(pdev);

#ifdef HAVE_SAFE_IB_ALLOC_DEVICE
	dev = ib_alloc_device(efa_dev, ibdev);
#else
	dev = (struct efa_dev *)ib_alloc_device(sizeof(*dev));
#endif
	if (!dev) {
		dev_err(&pdev->dev, "Device alloc failed\n");
		err = -ENOMEM;
		goto err_disable_device;
	}

	pci_set_drvdata(pdev, dev);
	edev = &dev->edev;
	edev->efa_dev = dev;
	edev->dmadev = &pdev->dev;
	dev->pdev = pdev;
#ifdef HAVE_XARRAY
	xa_init(&dev->cqs_xa);
#else
	memset(dev->cqs_arr, 0, sizeof(dev->cqs_arr));
#endif

	pci_mem_bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (EFA_BASE_BAR_MASK & ~pci_mem_bars) {
		dev_err(&pdev->dev, "BARs unavailable. Requested %#x, available %#x\n",
			(int)EFA_BASE_BAR_MASK, pci_mem_bars);
		err = -ENODEV;
		goto err_ibdev_destroy;
	}
	err = pci_request_selected_regions(pdev, EFA_BASE_BAR_MASK, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "pci_request_selected_regions failed %d\n",
			err);
		goto err_ibdev_destroy;
	}

	dev->reg_bar_addr = pci_resource_start(pdev, EFA_REG_BAR);
	dev->reg_bar_len = pci_resource_len(pdev, EFA_REG_BAR);
	dev->mem_bar_addr = pci_resource_start(pdev, EFA_MEM_BAR);
	dev->mem_bar_len = pci_resource_len(pdev, EFA_MEM_BAR);
#ifdef HAVE_EFA_KVERBS
	dev->mem_bar = devm_ioremap_wc(&pdev->dev, dev->mem_bar_addr, dev->mem_bar_len);
#endif

	edev->reg_bar = devm_ioremap(&pdev->dev,
				     dev->reg_bar_addr,
				     dev->reg_bar_len);
	if (!edev->reg_bar) {
		dev_err(&pdev->dev, "Failed to remap register bar\n");
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
		dev_err(&pdev->dev, "EFA device init failed\n");
		if (err == -ETIME)
			err = -EPROBE_DEFER;
		goto err_reg_read_destroy;
	}

	err = efa_enable_msix(dev);
	if (err)
		goto err_reg_read_destroy;

	edev->aq.msix_vector_idx = dev->admin_msix_vector_idx;
	edev->aenq.msix_vector_idx = dev->admin_msix_vector_idx;

	err = efa_set_mgmnt_irq(dev);
	if (err)
		goto err_disable_msix;

	err = efa_com_admin_init(edev, &aenq_handlers);
	if (err)
		goto err_free_mgmnt_irq;

	err = efa_sysfs_init(dev);
	if (err)
		goto err_admin_destroy;

	return dev;

err_admin_destroy:
	efa_com_admin_destroy(edev);
err_free_mgmnt_irq:
	efa_free_irq(dev, &dev->admin_irq);
err_disable_msix:
	efa_disable_msix(dev);
err_reg_read_destroy:
	efa_com_mmio_reg_read_destroy(edev);
err_iounmap:
	devm_iounmap(&pdev->dev, edev->reg_bar);
err_release_bars:
	efa_release_bars(dev, EFA_BASE_BAR_MASK);
err_ibdev_destroy:
	ib_dealloc_device(&dev->ibdev);
err_disable_device:
	pci_disable_device(pdev);
	return ERR_PTR(err);
}

static void efa_remove_device(struct pci_dev *pdev)
{
	struct efa_dev *dev = pci_get_drvdata(pdev);
	struct efa_com_dev *edev;

	edev = &dev->edev;
	efa_sysfs_destroy(dev);
	efa_com_admin_destroy(edev);
	efa_free_irq(dev, &dev->admin_irq);
	efa_disable_msix(dev);
	efa_com_mmio_reg_read_destroy(edev);
	devm_iounmap(&pdev->dev, edev->reg_bar);
	efa_release_bars(dev, EFA_BASE_BAR_MASK);
#ifdef HAVE_XARRAY
	xa_destroy(&dev->cqs_xa);
#endif
	ib_dealloc_device(&dev->ibdev);
	pci_disable_device(pdev);
}

static int efa_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct efa_dev *dev;
	int err;

	dev = efa_probe_device(pdev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

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
	struct efa_dev *dev = pci_get_drvdata(pdev);

	efa_ib_device_remove(dev);
	efa_remove_device(pdev);
}

static void efa_shutdown(struct pci_dev *pdev)
{
	struct efa_dev *dev = pci_get_drvdata(pdev);

	efa_destroy_eqs(dev);
	efa_com_dev_reset(&dev->edev, EFA_REGS_RESET_SHUTDOWN);
	efa_free_irq(dev, &dev->admin_irq);
	efa_disable_msix(dev);
}

static struct pci_driver efa_pci_driver = {
	.name           = DRV_MODULE_NAME,
	.id_table       = efa_pci_tbl,
	.probe          = efa_probe,
	.remove         = efa_remove,
	.shutdown       = efa_shutdown,
};

static int __init efa_init(void)
{
	int err;

	pr_info("%s\n", version);

	err = pci_register_driver(&efa_pci_driver);
	if (err) {
		pr_err("Couldn't register efa driver\n");
		return err;
	}

#ifdef HAVE_EFA_P2P
	efa_p2p_init();
#endif

	return 0;
}

static void __exit efa_exit(void)
{
	pci_unregister_driver(&efa_pci_driver);
}

module_init(efa_init);
module_exit(efa_exit);

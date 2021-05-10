// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/utsname.h>
#include <linux/version.h>

#include <rdma/ib_user_verbs.h>

#include "efa.h"
#include "efa_sysfs.h"

#ifdef HAVE_EFA_GDR
#include "efa_gdr.h"
#endif

#ifndef HAVE_PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif
#define PCI_DEV_ID_EFA0_VF 0xefa0
#define PCI_DEV_ID_EFA1_VF 0xefa1

static const struct pci_device_id efa_pci_tbl[] = {
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA0_VF) },
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA1_VF) },
	{ }
};

#define DRV_MODULE_VER_MAJOR           1
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
#ifdef HAVE_EFA_GDR
MODULE_INFO(gdr, "Y");
#endif

#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

#ifdef HAVE_CUSTOM_COMMANDS
#define EFA_EVERBS_DEVICE_NAME "efa_everbs"
#define EFA_EVERBS_MAX_DEVICES 64

static struct class *efa_everbs_class;
static unsigned int efa_everbs_major;

static int efa_everbs_dev_init(struct efa_dev *dev, int devnum);
static void efa_everbs_dev_destroy(struct efa_dev *dev);
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

static irqreturn_t efa_intr_msix_mgmnt(int irq, void *data)
{
	struct efa_dev *dev = data;

	efa_com_admin_q_comp_intr_handler(&dev->edev);
	efa_com_aenq_intr_handler(&dev->edev, data);

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
		dev_err(&dev->pdev->dev, "Failed to request admin irq (%d)\n",
			err);
		return err;
	}

	dev_dbg(&dev->pdev->dev, "Set affinity hint of mgmnt irq to %*pbl (irq vector: %d)\n",
		nr_cpumask_bits, &irq->affinity_hint_mask, irq->vector);
	irq_set_affinity_hint(irq->vector, &irq->affinity_hint_mask);

	return 0;
}

static void efa_setup_mgmnt_irq(struct efa_dev *dev)
{
	u32 cpu;

	snprintf(dev->admin_irq.name, EFA_IRQNAME_SIZE,
		 "efa-mgmnt@pci:%s", pci_name(dev->pdev));
	dev->admin_irq.handler = efa_intr_msix_mgmnt;
	dev->admin_irq.data = dev;
	dev->admin_irq.vector =
#ifndef HAVE_PCI_IRQ_VECTOR
		dev->admin_msix_entry.vector;
#else
		pci_irq_vector(dev->pdev, dev->admin_msix_vector_idx);
#endif
	cpu = cpumask_first(cpu_online_mask);
	dev->admin_irq.cpu = cpu;
	cpumask_set_cpu(cpu,
			&dev->admin_irq.affinity_hint_mask);
	dev_info(&dev->pdev->dev, "Setup irq:0x%p vector:%d name:%s\n",
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
	efa_setup_mgmnt_irq(dev);

	return efa_request_mgmnt_irq(dev);
}

static int efa_request_doorbell_bar(struct efa_dev *dev)
{
	u8 db_bar_idx = dev->dev_attr.db_bar;
	struct pci_dev *pdev = dev->pdev;
	int bars;
	int err;

	if (!(BIT(db_bar_idx) & EFA_BASE_BAR_MASK)) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM) & BIT(db_bar_idx);

		err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
		if (err) {
			dev_err(&dev->pdev->dev,
				"pci_request_selected_regions for bar %d failed %d\n",
				db_bar_idx, err);
			return err;
		}
	}

	dev->db_bar_addr = pci_resource_start(dev->pdev, db_bar_idx);
	dev->db_bar_len = pci_resource_len(dev->pdev, db_bar_idx);

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

	strlcpy(hinf->os_dist_str, utsname()->release,
		min(sizeof(hinf->os_dist_str), sizeof(utsname()->release)));
	hinf->os_type = EFA_ADMIN_OS_LINUX;
	strlcpy(hinf->kernel_ver_str, utsname()->version,
		min(sizeof(hinf->kernel_ver_str), sizeof(utsname()->version)));
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
#ifdef HAVE_EFA_GDR
	EFA_SET(&hinf->flags, EFA_ADMIN_HOST_INFO_GDR, 1);
#endif

	efa_com_set_feature_ex(&dev->edev, &resp, &cmd, EFA_ADMIN_HOST_INFO,
			       hinf_dma, bufsz);

	dma_free_coherent(&dev->pdev->dev, bufsz, hinf, hinf_dma);
}

#ifdef HAVE_IB_DEV_OPS
static const struct ib_device_ops efa_dev_ops = {
#ifdef HAVE_IB_DEVICE_OPS_COMMON
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_EFA,
	.uverbs_abi_ver = EFA_UVERBS_ABI_VERSION,
#endif

	.alloc_hw_stats = efa_alloc_hw_stats,
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
#ifdef HAVE_AH_CORE_ALLOCATION
	.create_ah = efa_create_ah,
#else
	.create_ah = efa_kzalloc_ah,
#endif
#ifdef HAVE_CQ_CORE_ALLOCATION
	.create_cq = efa_create_cq,
#else
	.create_cq = efa_kzalloc_cq,
#endif
	.create_qp = efa_create_qp,
#ifdef HAVE_UVERBS_CMD_MASK_NOT_NEEDED
	.create_user_ah = efa_create_ah,
#endif
	.dealloc_pd = efa_dealloc_pd,
	.dealloc_ucontext = efa_dealloc_ucontext,
	.dereg_mr = efa_dereg_mr,
	.destroy_ah = efa_destroy_ah,
	.destroy_cq = efa_destroy_cq,
	.destroy_qp = efa_destroy_qp,
#ifndef HAVE_NO_KVERBS_DRIVERS
	.get_dma_mr = efa_get_dma_mr,
#endif
	.get_hw_stats = efa_get_hw_stats,
	.get_link_layer = efa_port_link_layer,
	.get_port_immutable = efa_get_port_immutable,
	.mmap = efa_mmap,
#ifdef HAVE_CORE_MMAP_XA
	.mmap_free = efa_mmap_free,
#endif
	.modify_qp = efa_modify_qp,
#ifndef HAVE_NO_KVERBS_DRIVERS
	.poll_cq = efa_poll_cq,
	.post_recv = efa_post_recv,
	.post_send = efa_post_send,
#endif
	.query_device = efa_query_device,
	.query_gid = efa_query_gid,
	.query_pkey = efa_query_pkey,
	.query_port = efa_query_port,
	.query_qp = efa_query_qp,
	.reg_user_mr = efa_reg_mr,
#ifndef HAVE_NO_KVERBS_DRIVERS
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
#ifdef HAVE_UCONTEXT_CORE_ALLOCATION
	INIT_RDMA_OBJ_SIZE(ib_ucontext, efa_ucontext, ibucontext),
#endif
};
#endif

static int efa_ib_device_add(struct efa_dev *dev)
{
	struct efa_com_get_hw_hints_result hw_hints;
	struct pci_dev *pdev = dev->pdev;
#ifdef HAVE_CUSTOM_COMMANDS
	int devnum;
#endif
	int err;

#ifdef HAVE_CREATE_AH_NO_UDATA
	INIT_LIST_HEAD(&dev->efa_ah_list);
	mutex_init(&dev->ah_list_lock);
#endif

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

	efa_set_host_info(dev);

	dev->ibdev.node_type = RDMA_NODE_UNSPECIFIED;
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = 1;
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

#if defined(HAVE_IB_QUERY_DEVICE_UDATA) && !defined(HAVE_UVERBS_CMD_MASK_NOT_NEEDED)
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
#ifdef HAVE_HW_STATS
	dev->ibdev.alloc_hw_stats = efa_alloc_hw_stats;
#endif
	dev->ibdev.alloc_pd = efa_kzalloc_pd;
	dev->ibdev.alloc_ucontext = efa_kzalloc_ucontext;
	dev->ibdev.create_ah = efa_kzalloc_ah;
	dev->ibdev.create_cq = efa_kzalloc_cq;
	dev->ibdev.create_qp = efa_create_qp;
	dev->ibdev.dealloc_pd = efa_dealloc_pd;
	dev->ibdev.dealloc_ucontext = efa_dealloc_ucontext;
	dev->ibdev.dereg_mr = efa_dereg_mr;
	dev->ibdev.destroy_ah = efa_destroy_ah;
	dev->ibdev.destroy_cq = efa_destroy_cq;
	dev->ibdev.destroy_qp = efa_destroy_qp;
	dev->ibdev.get_dma_mr = efa_get_dma_mr;
#ifdef HAVE_HW_STATS
	dev->ibdev.get_hw_stats = efa_get_hw_stats;
#endif
	dev->ibdev.get_link_layer = efa_port_link_layer;
#ifdef HAVE_GET_PORT_IMMUTABLE
	dev->ibdev.get_port_immutable = efa_get_port_immutable;
#endif
	dev->ibdev.mmap = efa_mmap;
	dev->ibdev.modify_qp = efa_modify_qp;
	dev->ibdev.poll_cq = efa_poll_cq;
	dev->ibdev.post_recv = efa_post_recv;
	dev->ibdev.post_send = efa_post_send;
	dev->ibdev.query_device = efa_query_device;
	dev->ibdev.query_gid = efa_query_gid;
	dev->ibdev.query_pkey = efa_query_pkey;
	dev->ibdev.query_port = efa_query_port;
	dev->ibdev.query_qp = efa_query_qp;
	dev->ibdev.reg_user_mr = efa_reg_mr;
	dev->ibdev.req_notify_cq = efa_req_notify_cq;
#endif

#ifdef HAVE_IB_REGISTER_DEVICE_DMA_DEVICE_PARAM
	err = ib_register_device(&dev->ibdev, "efa_%d", &pdev->dev);
#elif defined(HAVE_IB_REGISTER_DEVICE_TWO_PARAMS)
	err = ib_register_device(&dev->ibdev, "efa_%d");
#elif defined(HAVE_IB_REGISTER_DEVICE_NAME_PARAM)
	err = ib_register_device(&dev->ibdev, "efa_%d", NULL);
#else
	strlcpy(dev->ibdev.name, "efa_%d",
		sizeof(dev->ibdev.name));

	err = ib_register_device(&dev->ibdev, NULL);
#endif
	if (err)
		goto err_release_doorbell_bar;

	ibdev_info(&dev->ibdev, "IB device registered\n");

#ifdef HAVE_CUSTOM_COMMANDS
	if (sscanf(dev_name(&dev->ibdev.dev), "efa_%d\n", &devnum) != 1) {
		err = -EINVAL;
		goto err_unregister_ibdev;
	}

	err = efa_everbs_dev_init(dev, devnum);
	if (err)
		goto err_unregister_ibdev;
	ibdev_info(&dev->ibdev, "Created everbs device %s%d\n",
		   EFA_EVERBS_DEVICE_NAME, devnum);
#endif

	return 0;

#ifdef HAVE_CUSTOM_COMMANDS
err_unregister_ibdev:
	ib_unregister_device(&dev->ibdev);
#endif
err_release_doorbell_bar:
	efa_release_doorbell_bar(dev);
	return err;
}

static void efa_ib_device_remove(struct efa_dev *dev)
{
#ifdef HAVE_CREATE_AH_NO_UDATA
	WARN_ON(!list_empty(&dev->efa_ah_list));
#endif
	efa_com_dev_reset(&dev->edev, EFA_REGS_RESET_NORMAL);
#ifdef HAVE_CUSTOM_COMMANDS
	efa_everbs_dev_destroy(dev);
#endif
	ibdev_info(&dev->ibdev, "Unregister ib device\n");
	ib_unregister_device(&dev->ibdev);
	efa_release_doorbell_bar(dev);
}

static void efa_disable_msix(struct efa_dev *dev)
{
#ifndef HAVE_PCI_IRQ_VECTOR
	pci_disable_msix(dev->pdev);
#else
	pci_free_irq_vectors(dev->pdev);
#endif
}

static int efa_enable_msix(struct efa_dev *dev)
{
	int msix_vecs, irq_num;

	/* Reserve the max msix vectors we might need */
	msix_vecs = EFA_NUM_MSIX_VEC;
	dev_dbg(&dev->pdev->dev, "Trying to enable MSI-X, vectors %d\n",
		msix_vecs);

#ifndef HAVE_PCI_IRQ_VECTOR
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
		dev_err(&dev->pdev->dev,
			"Allocated %d MSI-X (out of %d requested)\n",
			irq_num, msix_vecs);
		return -ENOSPC;
	}

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
	int bars;
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

	bars = pci_select_bars(pdev, IORESOURCE_MEM) & EFA_BASE_BAR_MASK;
	err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "pci_request_selected_regions failed %d\n",
			err);
		goto err_ibdev_destroy;
	}

	dev->reg_bar_addr = pci_resource_start(pdev, EFA_REG_BAR);
	dev->reg_bar_len = pci_resource_len(pdev, EFA_REG_BAR);
	dev->mem_bar_addr = pci_resource_start(pdev, EFA_MEM_BAR);
	dev->mem_bar_len = pci_resource_len(pdev, EFA_MEM_BAR);

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

#ifdef HAVE_PCI_IRQ_VECTOR
	edev->aq.msix_vector_idx = dev->admin_msix_vector_idx;
	edev->aenq.msix_vector_idx = dev->admin_msix_vector_idx;
#else
	edev->aq.msix_vector_idx = dev->admin_msix_entry.entry;
	edev->aenq.msix_vector_idx = dev->admin_msix_entry.entry;
#endif

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
	efa_free_mgmnt_irq(dev);
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
	efa_free_mgmnt_irq(dev);
	efa_disable_msix(dev);
	efa_com_mmio_reg_read_destroy(edev);
	devm_iounmap(&pdev->dev, edev->reg_bar);
	efa_release_bars(dev, EFA_BASE_BAR_MASK);
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

static struct pci_driver efa_pci_driver = {
	.name           = DRV_MODULE_NAME,
	.id_table       = efa_pci_tbl,
	.probe          = efa_probe,
	.remove         = efa_remove,
};

#ifdef HAVE_CUSTOM_COMMANDS
static ssize_t
(*efa_everbs_cmd_table[EFA_EVERBS_CMD_MAX])(struct efa_dev *dev,
					    const char __user *buf, int in_len,
					    int out_len) = {
#ifdef HAVE_CREATE_AH_NO_UDATA
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

	if (count < sizeof(hdr))
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	if (hdr.in_words * 4 != count)
		return -EINVAL;

	if (hdr.command >= ARRAY_SIZE(efa_everbs_cmd_table) ||
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

static char *efa_everbs_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
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

	WARN_ON(devnum >= EFA_EVERBS_MAX_DEVICES);
	cdev_init(&dev->cdev, &efa_everbs_fops);
	dev->cdev.owner = THIS_MODULE;

	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		return err;

	dev->everbs_dev = device_create(efa_everbs_class,
					&dev->pdev->dev,
					devno,
					dev,
					EFA_EVERBS_DEVICE_NAME "%d",
					devnum);
	if (IS_ERR(dev->everbs_dev)) {
		err = PTR_ERR(dev->everbs_dev);
		ibdev_err(&dev->ibdev, "Failed to create device: %s%d [%d]\n",
			  EFA_EVERBS_DEVICE_NAME, devnum, err);
		goto err;
	}

	return 0;

err:
	cdev_del(&dev->cdev);
	return err;
}

static void efa_everbs_dev_destroy(struct efa_dev *dev)
{
	if (!dev->everbs_dev)
		return;

	device_destroy(efa_everbs_class, dev->cdev.dev);
	cdev_del(&dev->cdev);
	dev->everbs_dev = NULL;
}
#endif /* HAVE_CUSTOM_COMMANDS */

static int __init efa_init(void)
{
#ifdef HAVE_CUSTOM_COMMANDS
	dev_t dev;
#endif
	int err;

	pr_info("%s\n", version);
#ifdef HAVE_CUSTOM_COMMANDS
	err = alloc_chrdev_region(&dev, 0, EFA_EVERBS_MAX_DEVICES,
				  EFA_EVERBS_DEVICE_NAME);
	if (err) {
		pr_err("Couldn't allocate efa_everbs device numbers\n");
		goto out;
	}
	efa_everbs_major = MAJOR(dev);

	efa_everbs_class = class_create(THIS_MODULE, EFA_EVERBS_DEVICE_NAME);
	if (IS_ERR(efa_everbs_class)) {
		err = PTR_ERR(efa_everbs_class);
		pr_err("Couldn't create efa_everbs class\n");
		goto err_class;
	}
	efa_everbs_class->devnode = efa_everbs_devnode;
#endif

	err = pci_register_driver(&efa_pci_driver);
	if (err) {
		pr_err("Couldn't register efa driver\n");
		goto err_register;
	}

#ifdef HAVE_EFA_GDR
	nvmem_init();
#endif

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
	pci_unregister_driver(&efa_pci_driver);
#ifdef HAVE_CUSTOM_COMMANDS
	class_destroy(efa_everbs_class);
	unregister_chrdev_region(MKDEV(efa_everbs_major, 0),
				 EFA_EVERBS_MAX_DEVICES);
#endif
}

module_init(efa_init);
module_exit(efa_exit);

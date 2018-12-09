// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include <rdma/ib_user_verbs.h>

#include "efa.h"
#include "efa_sysfs.h"

#ifdef HAVE_CUSTOM_COMMANDS
#include "efa-abi.h"
#endif

#ifndef PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif
#ifndef PCI_DEV_ID_EFA_VF
#define PCI_DEV_ID_EFA_VF 0xefa0
#endif

static const struct pci_device_id efa_pci_tbl[] = {
	{ PCI_VDEVICE(AMAZON, PCI_DEV_ID_EFA_VF) },
	{ }
};

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

int efa_get_device_attributes(struct efa_dev *dev,
			      struct efa_com_get_device_attr_result *result)
{
	int err;

	pr_debug("--->\n");
	err = efa_com_get_device_attr(dev->edev, result);
	if (err)
		pr_err("failed to get device_attr err[%d]!\n", err);

	return err;
}

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

#ifdef HAVE_CUSTOM_COMMANDS
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

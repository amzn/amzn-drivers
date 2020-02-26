// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include <linux/module.h>

#include "gvt_compress.h"
#include "gvt_compress_device.h"

/* Module parameters */
static int compress_level_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops compress_level_ops = {
	.set = compress_level_set,
	.get = param_get_int,
};

static int compress_level = GVT_COMP_LEVEL_4;
module_param_cb(compress_level, &compress_level_ops, &compress_level, 0444);
MODULE_PARM_DESC(compress_level,
	"compression level to use from 1-5 (default: 4)");

static int req_timeout_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops req_timeout_ops = {
	.set = req_timeout_set,
	.get = param_get_int,
};

static int req_timeout_ms = GVT_MAX_TIMEOUT_WAIT_MS;
module_param_cb(req_timeout_ms, &req_timeout_ops, &req_timeout_ms, 0444);
MODULE_PARM_DESC(req_timeout_ms,
	"request timeout value in milliseconds (default: 100)");

static int num_channels_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops num_channels_ops = {
	.set = num_channels_set,
	.get = param_get_int,
};

static int num_channels = GVT_MAX_CHANNELS;
module_param_cb(num_channels, &num_channels_ops, &num_channels, 0444);
MODULE_PARM_DESC(num_channels,
	"number of channels to enable from 1-16 (default: 16)");

static int num_channels_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 1 || n > GVT_MAX_CHANNELS)
		return -EINVAL;

	return param_set_int(val, kp);
}

static int compress_level_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 1 || n > GVT_COMP_LEVEL_5)
		return -EINVAL;

	return param_set_int(val, kp);
}

static int req_timeout_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 1)
		return -EINVAL;

	return param_set_int(val, kp);
}

int gvt_comp_get_compress_level(void)
{
	return compress_level;
}

int gvt_comp_get_req_timeout_ms(void)
{
	return req_timeout_ms;
}

/* Device BARs */
enum {
	GVT_DEVICE_DMA_BAR		= 0,
	GVT_DEVICE_ENGINE_BAR		= 4
};

static int gvt_comp_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct gvt_comp_device *comp_dev;
	void __iomem * const *iomap;
	uint32_t gvt_engine_size;
	u16 vendor_id, dev_id;
	struct device *dev;
	int bar_mask;
	int rc = 0;
	u8 rev_id;

	dev = &pdev->dev;

	comp_dev = devm_kzalloc(dev, sizeof(*comp_dev), GFP_KERNEL);
	if (!comp_dev)
		return -ENOMEM;

	gvt_engine_size = gvt_comp_device_get_gvt_engine_size();
	comp_dev->gvt_engine = devm_kzalloc(dev, gvt_engine_size, GFP_KERNEL);
	if (!comp_dev->gvt_engine) {
		dev_err(dev, "%s: Failed to alloc gvt_engine\n", __func__);
		return -ENOMEM;
	}

	rc = pcim_enable_device(pdev);
	if (rc) {
		dev_err(dev, "%s: pcim_enable_device failed! rc=%d\n",
				__func__, rc);
		return rc;
	}

	bar_mask = BIT(GVT_DEVICE_DMA_BAR) | BIT(GVT_DEVICE_ENGINE_BAR);

	rc = pcim_iomap_regions(pdev, bar_mask, DRV_NAME);
	if (rc) {
		dev_err(dev, "%s: pcim_iomap_regions failed! rc=%d\n",
				__func__, rc);
		goto err_iomap;
	}

	iomap = pcim_iomap_table(pdev);
	if (!iomap) {
		dev_err(dev, "%s: pcim_iomap_table failed!\n", __func__);
		rc = -ENOMEM;
		goto err_iomap;
	}

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (rc) {
		dev_err(dev, "%s: pci_set_dma_mask failed!\n", __func__);
		goto err_iomap;
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (rc) {
		dev_err(dev, "%s: pci_set_consistent_dma_mask failed!\n",
			     __func__);
		goto err_iomap;
	}

	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor_id);
	pci_read_config_word(pdev, PCI_DEVICE_ID, &dev_id);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);

	comp_dev->pdev = pdev;
	comp_dev->dev = &pdev->dev;
	comp_dev->dma_regs_base = iomap[GVT_DEVICE_DMA_BAR],
	comp_dev->engine_regs_base = iomap[GVT_DEVICE_ENGINE_BAR];
	comp_dev->vendor_id = vendor_id;
	comp_dev->dev_id = dev_id;
	comp_dev->rev_id = rev_id;
	comp_dev->num_channels = num_channels;
	comp_dev->hw_descs_num = GVT_DEVICE_MAX_HW_DESCS;
	comp_dev->hw_desc_size = gvt_comp_device_get_hw_desc_size();
	comp_dev->hw_cdesc_size = gvt_comp_device_get_hw_cdesc_size();
	comp_dev->sw_desc_size = gvt_comp_device_get_sw_desc_size();
	comp_dev->alg_desc_size = gvt_comp_device_get_alg_desc_size();

	pci_set_master(pdev);
	pci_set_drvdata(pdev, comp_dev);
	dev_set_drvdata(dev, comp_dev);

	rc = gvt_comp_device_reset(comp_dev);
	if (rc) {
		dev_err(dev, "%s: Device reset failed\n", __func__);
		goto err_reset;
	}

	rc = gvt_comp_core_init(comp_dev);
	if (rc) {
		dev_err(dev, "%s: gvt_comp_core_init failed\n", __func__);
		goto err_reset;
	}

	rc = gvt_comp_sysfs_init(comp_dev);
	if (rc) {
		dev_err(dev, "%s: gvt_comp_sysfs_init failed\n", __func__);
		goto err_sysfs_init;
	}

	rc = gvt_comp_alg_init(comp_dev);
	if (rc) {
		dev_err(dev, "%s: gvt_comp_alg_init failed\n", __func__);
		goto err_alg_init;
	}

	return rc;

err_alg_init:
	gvt_comp_sysfs_terminate(comp_dev);
err_sysfs_init:
	gvt_comp_core_terminate(comp_dev);
err_reset:
err_iomap:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	return rc;
}

static void gvt_comp_pci_remove(struct pci_dev *pdev)
{
	struct gvt_comp_device *comp_dev;

	comp_dev = pci_get_drvdata(pdev);
	if (!comp_dev)
		return;

	gvt_comp_alg_terminate(comp_dev);

	gvt_comp_sysfs_terminate(comp_dev);

	gvt_comp_core_terminate(comp_dev);

	pci_disable_device(pdev);

	pci_set_drvdata(pdev, NULL);
}

#ifndef PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS
#define PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS 0x1c36
#endif

#ifndef PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif

static const struct pci_device_id gvt_comp_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_AMAZON, 0xace0)},
	{PCI_DEVICE(PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS, 0x0022)},
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, gvt_comp_pci_tbl);

static struct pci_driver gvt_comp_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= gvt_comp_pci_tbl,
	.probe		= gvt_comp_pci_probe,
	.remove		= gvt_comp_pci_remove,
};

static int __init gvt_comp_init_module(void)
{
	int rc;

	pr_info("%s Driver v%s\n", DEVICE_NAME, DRV_VERSION);
	pr_info("%s: compress_level=%u num_channels=%u\n",
			DRV_NAME, compress_level, num_channels);

	rc = pci_register_driver(&gvt_comp_pci_driver);

	return rc;
}

static void __exit gvt_comp_exit_module(void)
{
	pr_info("%s: exiting\n", DRV_NAME);
	pci_unregister_driver(&gvt_comp_pci_driver);
}

module_init(gvt_comp_init_module);
module_exit(gvt_comp_exit_module);

MODULE_DESCRIPTION("AWS Graviton2 Compression Engine Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_LICENSE("GPL");

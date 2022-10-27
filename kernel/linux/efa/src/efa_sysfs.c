// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_sysfs.h"
#include "kcompat.h"

#include <linux/device.h>
#include <linux/sysfs.h>

#ifndef HAVE_SYSFS_EMIT
#include <linux/mm.h>

static int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list args;
	int len;

	if (!buf)
		return 0;

	va_start(args, fmt);
	len = vscnprintf(buf, PAGE_SIZE, fmt, args);
	va_end(args);

	return len;
}
#endif

#ifdef HAVE_EFA_P2P
#include "efa_p2p.h"

static ssize_t gdr_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	if (nvmem_is_supported())
		return sysfs_emit(buf, "1\n");

	return sysfs_emit(buf, "0\n");
}

static DEVICE_ATTR_RO(gdr);
#endif

int efa_sysfs_init(struct efa_dev *dev)
{
#ifdef HAVE_EFA_P2P
	struct device *device = &dev->pdev->dev;

	if (device_create_file(device, &dev_attr_gdr))
		dev_err(device, "Failed to create GDR sysfs file\n");
#endif
	return 0;
}

void efa_sysfs_destroy(struct efa_dev *dev)
{
#ifdef HAVE_EFA_P2P
	device_remove_file(&dev->pdev->dev, &dev_attr_gdr);
#endif
}

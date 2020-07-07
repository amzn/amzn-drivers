// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_sysfs.h"
#include "kcompat.h"

#include <linux/device.h>
#include <linux/sysfs.h>

#ifdef HAVE_EFA_GDR
static ssize_t gdr_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "1\n");
}

static DEVICE_ATTR_RO(gdr);
#endif

int efa_sysfs_init(struct efa_dev *dev)
{
#ifdef HAVE_EFA_GDR
	struct device *device = &dev->pdev->dev;

	if (device_create_file(device, &dev_attr_gdr))
		dev_err(device, "Failed to create GDR sysfs file\n");
#endif
	return 0;
}

void efa_sysfs_destroy(struct efa_dev *dev)
{
#ifdef HAVE_EFA_GDR
	device_remove_file(&dev->pdev->dev, &dev_attr_gdr);
#endif
}

// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/sysfs.h>

#include "ena_com.h"
#include "ena_netdev.h"
#include "ena_sysfs.h"


static ssize_t ena_store_rx_copybreak(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	unsigned long rx_copybreak;
	int rc;

	rc = kstrtoul(buf, 10, &rx_copybreak);
	if (rc < 0)
		goto exit;

	rtnl_lock();
	rc = ena_set_rx_copybreak(adapter, rx_copybreak);
	if (rc)
		goto unlock;
	rtnl_unlock();

	return len;
unlock:
	rtnl_unlock();
exit:
	return rc;
}

#define ENA_RX_COPYBREAK_STR_MAX_LEN 7

static ssize_t ena_show_rx_copybreak(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);

	return snprintf(buf, ENA_RX_COPYBREAK_STR_MAX_LEN, "%d\n",
			adapter->rx_copybreak);
}

static DEVICE_ATTR(rx_copybreak, S_IRUGO | S_IWUSR, ena_show_rx_copybreak,
		   ena_store_rx_copybreak);

/******************************************************************************
 *****************************************************************************/
int ena_sysfs_init(struct device *dev)
{

	if (device_create_file(dev, &dev_attr_rx_copybreak))
		dev_err(dev, "Failed to create rx_copybreak sysfs entry");
	return 0;
}

/******************************************************************************
 *****************************************************************************/
void ena_sysfs_terminate(struct device *dev)
{
	device_remove_file(dev, &dev_attr_rx_copybreak);
}

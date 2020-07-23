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
	struct ena_ring *rx_ring;
	int err, i;

	err = kstrtoul(buf, 10, &rx_copybreak);
	if (err < 0)
		return err;

	if (len > adapter->netdev->mtu)
		return -EINVAL;

	rtnl_lock();
	adapter->rx_copybreak = rx_copybreak;

	for (i = 0; i < adapter->num_io_queues; i++) {
		rx_ring = &adapter->rx_ring[i];
		rx_ring->rx_copybreak = rx_copybreak;
	}
	rtnl_unlock();

	return len;
}

static ssize_t ena_show_rx_copybreak(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", adapter->rx_copybreak);
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

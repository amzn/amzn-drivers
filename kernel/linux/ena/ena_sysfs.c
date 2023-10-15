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
#ifdef ENA_PHC_SUPPORT
#include "ena_phc.h"
#endif /* ENA_PHC_SUPPORT */
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
#ifdef ENA_PHC_SUPPORT
/* Max PHC error bound string size takes into account max u32 value, null and new line characters */
#define ENA_PHC_ERROR_BOUND_STR_MAX_LEN 12

static ssize_t ena_show_phc_error_bound(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	u32 error_bound_nsec = 0;
	int rc;

	rc = ena_phc_get_error_bound(adapter, &error_bound_nsec);
	if (rc != 0)
		return rc;

	return snprintf(buf, ENA_PHC_ERROR_BOUND_STR_MAX_LEN, "%u\n", error_bound_nsec);
}

static DEVICE_ATTR(phc_error_bound, S_IRUGO, ena_show_phc_error_bound, NULL);
#endif /* ENA_PHC_SUPPORT */

/******************************************************************************
 *****************************************************************************/
int ena_sysfs_init(struct device *dev)
{

	if (device_create_file(dev, &dev_attr_rx_copybreak))
		dev_err(dev, "Failed to create rx_copybreak sysfs entry");

#ifdef ENA_PHC_SUPPORT
	if (device_create_file(dev, &dev_attr_phc_error_bound))
		dev_err(dev, "Failed to create phc_error_bound sysfs entry");

#endif /* ENA_PHC_SUPPORT */
	return 0;
}

/******************************************************************************
 *****************************************************************************/
void ena_sysfs_terminate(struct device *dev)
{
	device_remove_file(dev, &dev_attr_rx_copybreak);
#ifdef ENA_PHC_SUPPORT
	device_remove_file(dev, &dev_attr_phc_error_bound);
#endif /* ENA_PHC_SUPPORT */
}

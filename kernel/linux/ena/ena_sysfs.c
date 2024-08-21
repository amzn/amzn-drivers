// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
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

static ssize_t ena_large_llq_set(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	enum ena_llq_header_size_policy_t new_llq_policy;
	unsigned long large_llq_enabled;
	int rc;

	rc = kstrtoul(buf, 10, &large_llq_enabled);
	if (rc < 0)
		return rc;

	if (large_llq_enabled != 0 && large_llq_enabled != 1)
		return -EINVAL;

	rtnl_lock();
	new_llq_policy = large_llq_enabled ? ENA_LLQ_HEADER_SIZE_POLICY_LARGE :
					     ENA_LLQ_HEADER_SIZE_POLICY_NORMAL;
	if (adapter->llq_policy == new_llq_policy)
		goto unlock;

	adapter->llq_policy = new_llq_policy;

	ena_destroy_device(adapter, false);
	rc = ena_restore_device(adapter);
unlock:
	rtnl_unlock();

	return rc ? rc : len;
}

#define ENA_LARGE_LLQ_STR_MAX_LEN 3

static ssize_t ena_large_llq_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	bool large_llq_enabled;

	large_llq_enabled = adapter->llq_policy == ENA_LLQ_HEADER_SIZE_POLICY_LARGE;

	return snprintf(buf, ENA_LARGE_LLQ_STR_MAX_LEN, "%d\n",
			large_llq_enabled);
}

static DEVICE_ATTR(large_llq_header, S_IRUGO | S_IWUSR, ena_large_llq_show,
		   ena_large_llq_set);

/******************************************************************************
 *****************************************************************************/
int ena_sysfs_init(struct device *dev)
{

	if (device_create_file(dev, &dev_attr_rx_copybreak))
		dev_err(dev, "Failed to create rx_copybreak sysfs entry");

#ifdef ENA_PHC_SUPPORT
	if (ena_phc_is_active(dev_get_drvdata(dev)))
		if (device_create_file(dev, &dev_attr_phc_error_bound))
			dev_err(dev, "Failed to create phc_error_bound sysfs entry");

#endif /* ENA_PHC_SUPPORT */

	if (device_create_file(dev, &dev_attr_large_llq_header))
		dev_err(dev, "Failed to create large_llq_header sysfs entry");
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
	device_remove_file(dev, &dev_attr_large_llq_header);
}

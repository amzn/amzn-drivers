/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/sysfs.h>

#include "ena_com.h"
#include "ena_netdev.h"
#include "ena_sysfs.h"

#define to_ext_attr(x) container_of(x, struct dev_ext_attribute, attr)
static int ena_validate_small_copy_len(struct ena_adapter *adapter,
				       unsigned long len)
{
	if (len > adapter->netdev->mtu)
		return -EINVAL;

	return 0;
}

static ssize_t ena_store_small_copy_len(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	unsigned long small_copy_len;
	struct ena_ring *rx_ring;
	int err, i;

	err = kstrtoul(buf, 10, &small_copy_len);
	if (err < 0)
		return err;

	err = ena_validate_small_copy_len(adapter, small_copy_len);
	if (err)
		return err;

	rtnl_lock();
	adapter->small_copy_len = small_copy_len;

	for (i = 0; i < adapter->num_queues; i++) {
		rx_ring = &adapter->rx_ring[i];
		rx_ring->rx_small_copy_len = small_copy_len;
	}
	rtnl_unlock();

	return len;
}

static ssize_t ena_show_small_copy_len(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", adapter->small_copy_len);
}

static DEVICE_ATTR(small_copy_len, S_IRUGO | S_IWUSR, ena_show_small_copy_len,
		   ena_store_small_copy_len);

/* adaptive interrupt moderation */
static ssize_t ena_show_intr_moderation(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ena_intr_moder_entry entry;
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	enum ena_intr_moder_level level = (enum ena_intr_moder_level)ea->var;
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	ssize_t rc = 0;

	ena_com_get_intr_moderation_entry(adapter->ena_dev, level, &entry);

	rc = sprintf(buf, "%u %u %u\n",
		     entry.intr_moder_interval,
		     entry.pkts_per_interval,
		     entry.bytes_per_interval);

	return rc;
}

static ssize_t ena_store_intr_moderation(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	struct ena_intr_moder_entry entry;
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	enum ena_intr_moder_level level = (enum ena_intr_moder_level)ea->var;
	int cnt;

	cnt = sscanf(buf, "%u %u %u",
		     &entry.intr_moder_interval,
		     &entry.pkts_per_interval,
		     &entry.bytes_per_interval);

	if (cnt != 3)
		return -EINVAL;

	ena_com_init_intr_moderation_entry(adapter->ena_dev, level, &entry);

	return count;
}

static ssize_t ena_store_intr_moderation_restore_default(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf,
							 size_t len)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	unsigned long restore_default;
	int err;

	err = kstrtoul(buf, 10, &restore_default);
	if (err < 0)
		return err;

	if (ena_com_interrupt_moderation_supported(ena_dev) && restore_default) {
		ena_com_config_default_interrupt_moderation_table(ena_dev);
		ena_com_enable_adaptive_moderation(ena_dev);
	}

	return len;
}

static ssize_t ena_store_enable_adaptive_intr_moderation(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf,
							 size_t len)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	unsigned long enable_moderation;
	int err;

	err = kstrtoul(buf, 10, &enable_moderation);
	if (err < 0)
		return err;

	if (enable_moderation == 0)
		ena_com_disable_adaptive_moderation(adapter->ena_dev);
	else
		ena_com_enable_adaptive_moderation(adapter->ena_dev);

	return len;
}

static ssize_t ena_show_enable_adaptive_intr_moderation(struct device *dev,
							struct device_attribute *attr,
							char *buf)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       ena_com_get_adaptive_moderation_enabled(adapter->ena_dev));
}

static DEVICE_ATTR(enable_adaptive_intr_moderation, S_IRUGO | S_IWUSR,
		ena_show_enable_adaptive_intr_moderation,
		ena_store_enable_adaptive_intr_moderation);

static DEVICE_ATTR(intr_moderation_restore_default, S_IWUSR | S_IWGRP,
		NULL, ena_store_intr_moderation_restore_default);

#define INTR_MODERATION_PREPARE_ATTR(_name, _type) {			\
	__ATTR(intr_moderation_##_name, (S_IRUGO | S_IWUSR | S_IWGRP),	\
		ena_show_intr_moderation, ena_store_intr_moderation), \
		(void *)_type }

/* Device attrs - intr moderation */
static struct dev_ext_attribute dev_attr_intr_moderation[] = {
	INTR_MODERATION_PREPARE_ATTR(lowest, ENA_INTR_MODER_LOWEST),
	INTR_MODERATION_PREPARE_ATTR(low, ENA_INTR_MODER_LOW),
	INTR_MODERATION_PREPARE_ATTR(mid, ENA_INTR_MODER_MID),
	INTR_MODERATION_PREPARE_ATTR(high, ENA_INTR_MODER_HIGH),
	INTR_MODERATION_PREPARE_ATTR(highest, ENA_INTR_MODER_HIGHEST),
};

/******************************************************************************
 *****************************************************************************/
int ena_sysfs_init(struct device *dev)
{
	int i, rc;
	struct ena_adapter *adapter = dev_get_drvdata(dev);

	if (device_create_file(dev, &dev_attr_small_copy_len))
		dev_err(dev, "failed to create small_copy_len sysfs entry");

	if (ena_com_interrupt_moderation_supported(adapter->ena_dev)) {
		if (device_create_file(dev,
				       &dev_attr_intr_moderation_restore_default))
			dev_err(dev,
				"failed to create intr_moderation_restore_default");

		if (device_create_file(dev,
				       &dev_attr_enable_adaptive_intr_moderation))
			dev_err(dev,
				"failed to create adaptive_intr_moderation_enable");

		for (i = 0; i < ARRAY_SIZE(dev_attr_intr_moderation); i++) {
			rc = sysfs_create_file(&dev->kobj,
					       &dev_attr_intr_moderation[i].attr.attr);
			if (rc) {
				dev_err(dev,
					"%s: sysfs_create_file(intr_moderation %d) failed\n",
					__func__, i);
				return rc;
			}
		}
	}

	return 0;
}

/******************************************************************************
 *****************************************************************************/
void ena_sysfs_terminate(struct device *dev)
{
	struct ena_adapter *adapter = dev_get_drvdata(dev);
	int i;

	device_remove_file(dev, &dev_attr_small_copy_len);
	if (ena_com_interrupt_moderation_supported(adapter->ena_dev)) {
		for (i = 0; i < ARRAY_SIZE(dev_attr_intr_moderation); i++)
			sysfs_remove_file(&dev->kobj,
					  &dev_attr_intr_moderation[i].attr.attr);
		device_remove_file(dev,
				   &dev_attr_enable_adaptive_intr_moderation);
		device_remove_file(dev,
				   &dev_attr_intr_moderation_restore_default);
	}
}

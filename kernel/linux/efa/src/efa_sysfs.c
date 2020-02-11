// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_sysfs.h"

#include <linux/device.h>
#include <linux/sysfs.h>

int efa_sysfs_init(struct efa_dev *dev)
{
	return 0;
}

void efa_sysfs_destroy(struct efa_dev *dev)
{
}

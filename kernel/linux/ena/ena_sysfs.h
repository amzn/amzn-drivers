/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef __ENA_SYSFS_H__
#define __ENA_SYSFS_H__

#ifdef CONFIG_SYSFS

int ena_sysfs_init(struct device *dev);

void ena_sysfs_terminate(struct device *dev);

#else /* CONFIG_SYSFS */

static inline int ena_sysfs_init(struct device *dev)
{
	return 0;
}

static inline void ena_sysfs_terminate(struct device *dev)
{
}

#endif /* CONFIG_SYSFS */

#endif /* __ENA_SYSFS_H__ */

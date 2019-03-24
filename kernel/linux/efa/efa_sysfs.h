/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_SYSFS_H_
#define _EFA_SYSFS_H_

#include "efa.h"

int efa_sysfs_init(struct efa_dev *dev);

void efa_sysfs_destroy(struct efa_dev *dev);

#endif /* _EFA_SYSFS_H_ */

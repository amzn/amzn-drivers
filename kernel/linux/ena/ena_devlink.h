/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2023 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef DEVLINK_H
#define DEVLINK_H

#include "ena_netdev.h"
#ifndef ENA_NO_DEVLINK_HEADERS
#include <net/devlink.h>
#endif

#ifdef ENA_DEVLINK_SUPPORT
#define ENA_DEVLINK_PRIV(devlink) \
	(*(struct ena_adapter **)devlink_priv(devlink))

struct devlink *ena_devlink_alloc(struct ena_adapter *adapter);
void ena_devlink_free(struct devlink *devlink);
void ena_devlink_register(struct devlink *devlink, struct device *dev);
void ena_devlink_unregister(struct devlink *devlink);
void ena_devlink_params_get(struct devlink *devlink);
void ena_devlink_disable_large_llq_header_param(struct devlink *devlink);
void ena_devlink_disable_phc_param(struct devlink *devlink);

#else /* ENA_DEVLINK_SUPPORT */
#ifdef ENA_NO_DEVLINK_HEADERS
struct devlink {};
#endif

/* Return a value of 1 so the caller wouldn't think the function failed (returned NULL) */
static inline struct devlink *ena_devlink_alloc(struct ena_adapter *adapter)
{
	return (struct devlink *)1;
}
static inline void ena_devlink_free(struct devlink *devlink) { }
static inline void ena_devlink_register(struct devlink *devlink, struct device *dev) { };
static inline void ena_devlink_unregister(struct devlink *devlink) { }
static inline void ena_devlink_params_get(struct devlink *devlink) { }
static inline void ena_devlink_disable_large_llq_header_param(struct devlink *devlink) { }
static inline void ena_devlink_disable_phc_param(struct devlink *devlink) { }

#endif /* ENA_DEVLINK_SUPPORT */
#endif /* DEVLINK_H */

/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifndef ENA_PHC_H
#define ENA_PHC_H

#ifdef ENA_PHC_SUPPORT
#include <linux/ptp_clock_kernel.h>

struct ena_phc_info {
	/* PTP hardware capabilities */
	struct ptp_clock_info clock_info;

	/* Registered PTP clock device */
	struct ptp_clock *clock;

	/* Adapter specific private data structure */
	struct ena_adapter *adapter;

	/* PHC lock */
	spinlock_t lock;

	/* Enabled by kernel */
	bool enabled;
};

void ena_phc_enable(struct ena_adapter *adapter, bool enable);
bool ena_phc_is_enabled(struct ena_adapter *adapter);
bool ena_phc_is_active(struct ena_adapter *adapter);
int ena_phc_get_index(struct ena_adapter *adapter);
int ena_phc_init(struct ena_adapter *adapter);
void ena_phc_destroy(struct ena_adapter *adapter);
int ena_phc_alloc(struct ena_adapter *adapter);
void ena_phc_free(struct ena_adapter *adapter);
int ena_phc_get_error_bound(struct ena_adapter *adapter, u32 *error_bound);
#else /* ENA_PHC_SUPPORT */

static inline void ena_phc_enable(struct ena_adapter *adapter, bool enable) { }
static inline bool ena_phc_is_enabled(struct ena_adapter *adapter) { return false; }
static inline bool ena_phc_is_active(struct ena_adapter *adapter) { return false; }
static inline int ena_phc_get_index(struct ena_adapter *adapter) { return -1; }
static inline int ena_phc_init(struct ena_adapter *adapter) { return 0; }
static inline void ena_phc_destroy(struct ena_adapter *adapter) { }
static inline int ena_phc_alloc(struct ena_adapter *adapter) { return 0; }
static inline void ena_phc_free(struct ena_adapter *adapter) { }
static inline int ena_phc_get_error_bound(struct ena_adapter *adapter, u32 *error_bound)
{
	return 0;
}
#endif /* ENA_PHC_SUPPORT */

#endif /* ENA_PHC_H */

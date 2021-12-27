/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2019-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_GDR_H_
#define _EFA_GDR_H_

#include "efa.h"

struct efa_p2pmem {
	struct efa_dev *dev;
	u64 ticket;
	u32 lkey;
	bool needs_dereg;
	struct list_head list; /* member of efa_p2p_list */
};

void efa_p2p_init(void);
struct efa_p2pmem *efa_p2p_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			       u64 length, unsigned int *pgsz);
int efa_p2p_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			 u64 *page_list);
int efa_p2p_put(u64 ticket, bool in_cb);

bool nvmem_is_supported(void);

#endif /* _EFA_GDR_H_ */

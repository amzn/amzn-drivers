/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2019-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_P2P_H_
#define _EFA_P2P_H_

#include "efa.h"

struct efa_p2p_ops {
	struct efa_p2pmem *(*try_get)(struct efa_dev *dev, u64 ticket, u64 start,
				      u64 length);
	int (*to_page_list)(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			    u64 *page_list);
	void (*release)(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			bool in_cb);
	unsigned int (*get_page_size)(struct efa_dev *dev,
				      struct efa_p2pmem *p2pmem);
};

enum efa_p2p_prov {
	EFA_P2P_PROVIDER_NVMEM,
	EFA_P2P_PROVIDER_NEURON,
	EFA_P2P_PROVIDER_MAX,
};

struct efa_p2p_provider {
	const struct efa_p2p_ops ops;
	enum efa_p2p_prov type;
};

struct efa_p2pmem {
	struct efa_dev *dev;
	const struct efa_p2p_provider *prov;
	u64 ticket;
	u32 lkey;
	bool needs_dereg;
	struct list_head list; /* member of efa_p2p_list */
};

void efa_p2p_init(void);
struct efa_p2pmem *efa_p2p_get(struct efa_dev *dev, struct efa_mr *mr, u64 start,
			       u64 length);
unsigned int efa_p2p_get_page_size(struct efa_dev *dev,
				   struct efa_p2pmem *p2pmem);
int efa_p2p_to_page_list(struct efa_dev *dev, struct efa_p2pmem *p2pmem,
			 u64 *page_list);
int efa_p2p_put(u64 ticket, bool in_cb);

/* Provider specific stuff go here */
const struct efa_p2p_provider *nvmem_get_provider(void);
bool nvmem_is_supported(void);

const struct efa_p2p_provider *neuronmem_get_provider(void);

#endif /* _EFA_P2P_H_ */

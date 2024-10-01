/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_VERBS_H_
#define _EFA_VERBS_H_

#include <rdma/ib_verbs.h>

#define EFA_VERBS_VERSION 1

struct ib_srd_wr {
	struct ib_send_wr wr;
	struct ib_ah *ah;
	u32 remote_qpn;
	u32 remote_qkey;
};

struct ib_srd_rdma_wr {
	struct ib_srd_wr wr;
	u64 remote_addr;
	u32 rkey;
};

static inline const struct ib_srd_wr *srd_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_srd_wr, wr);
}

static inline const struct ib_srd_rdma_wr *srd_rdma_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_srd_rdma_wr, wr.wr);
}

#define EFA_QPT_SRD IB_QPT_RESERVED1

#define EFA_MR_GEN_SHIFT 20
#define EFA_MR_GEN_MASK 0x00F00000

static inline void efa_inc_fast_reg_key_gen(struct ib_mr *mr)
{
	u32 mr_gen = (mr->rkey + (1 << EFA_MR_GEN_SHIFT)) & EFA_MR_GEN_MASK;

	mr->lkey = (mr->lkey & ~EFA_MR_GEN_MASK) | mr_gen;
	mr->rkey = (mr->rkey & ~EFA_MR_GEN_MASK) | mr_gen;
}

#endif

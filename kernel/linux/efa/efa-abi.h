/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#ifndef _EFA_ABI_H_
#define _EFA_ABI_H_

enum efa_ibv_user_cmds_supp_udata {
	EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE = 1 << 0,
	EFA_USER_CMDS_SUPP_UDATA_CREATE_AH    = 1 << 1,
};

enum efa_ibv_kernel_supp_mask {
	EFA_KERNEL_SUPP_QPT_SRD               = 1 << 0,
};

struct efa_ibv_alloc_ucontext_resp {
	__u32 comp_mask;
	__u32 cmds_supp_udata_mask;
	__u32 kernel_supp_mask;
	__u8 reserved_60[0x4];
};

struct efa_ibv_alloc_pd_resp {
	__u32 comp_mask;
	__u32 pdn;
};

struct efa_ibv_create_cq {
	__u32 comp_mask;
	__u32 cq_entry_size;
	__u16 num_sub_cqs;
	__u8 reserved_50[0x6];
};

struct efa_ibv_create_cq_resp {
	__u32 comp_mask;
	__u8 reserved_20[0x4];
	__aligned_u64 q_mmap_key;
	__aligned_u64 q_mmap_size;
	__u16 cq_idx;
	__u8 reserved_d0[0x6];
};

struct efa_ibv_create_qp {
	__u32 comp_mask;
	__u32 rq_entries;
	__u32 rq_entry_size;
	__u32 sq_depth;
	__u32 sq_ring_size;
	__u32 srd_qp;
};

struct efa_ibv_create_qp_resp {
	__u32 comp_mask;
	/* the offset inside the page of the rq db */
	__u32 rq_db_offset;
	/* the offset inside the page of the sq db */
	__u32 sq_db_offset;
	/* the offset inside the page of descriptors buffer */
	__u32 llq_desc_offset;
	__aligned_u64 rq_mmap_key;
	__aligned_u64 rq_mmap_size;
	__aligned_u64 rq_db_mmap_key;
	__aligned_u64 sq_db_mmap_key;
	__aligned_u64 llq_desc_mmap_key;
	__u16 send_sub_cq_idx;
	__u16 recv_sub_cq_idx;
	__u8 reserved_1e0[0x4];
};

struct efa_ibv_create_ah_resp {
	__u32 comp_mask;
	__u16 efa_address_handle;
	__u8 reserved_30[0x2];
};

struct efa_ibv_ex_query_device_resp {
	__u32 comp_mask;
	__u16 max_sq_sge;
	__u16 max_rq_sge;
	__u16 max_sq_wr;
	__u16 max_rq_wr;
	__u16 sub_cqs_per_cq;
	__u16 max_inline_data;
};

#ifdef HAVE_CUSTOM_COMMANDS
/******************************************************************************/
/*                            EFA CUSTOM COMMANDS                             */
/******************************************************************************/

enum efa_everbs_commands {
	EFA_EVERBS_CMD_GET_AH = 1,
	EFA_EVERBS_CMD_GET_EX_DEV_ATTRS,
	EFA_EVERBS_CMD_MAX,
};

struct efa_everbs_get_ah {
	__u32 comp_mask;
	__u32 pd_handle;
	__aligned_u64 response;
	__aligned_u64 user_handle;
	struct ib_uverbs_ah_attr attr;
};

struct efa_everbs_get_ah_resp {
	__u32 comp_mask;
	__u16 efa_address_handle;
	__u8 reserved_30[0x2];
};

struct efa_everbs_get_ex_dev_attrs {
	__u32 comp_mask;
	__u8 reserved_20[0x4];
	__aligned_u64 response;
};

struct efa_everbs_get_ex_dev_attrs_resp {
	__u32 comp_mask;
	__u16 max_sq_sge;
	__u16 max_rq_sge;
	__u16 max_sq_wr;
	__u16 max_rq_wr;
	__u16 sub_cqs_per_cq;
	__u16 max_inline_data;
};
#endif /* HAVE_CUSTOM_COMMANDS */

#endif /* _EFA_ABI_H_ */

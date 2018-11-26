/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#ifndef _EFA_COM_CMD_H_
#define _EFA_COM_CMD_H_

struct efa_com_create_qp_params {
	u32 pd;
	u8  qp_type;
	u64 rq_base_addr;
	u32 send_cq_idx;
	u32 recv_cq_idx;
	/*
	 * Send descriptor ring size in bytes,
	 * sufficient for user-provided number of WQEs and SGL size
	 */
	u32 sq_ring_size_in_bytes;
	/* Max number of WQEs that will be posted on send queue */
	u32 sq_depth;
	/* Recv descriptor ring size in bytes, sufficient */
	u32 rq_ring_size_in_bytes;
};

struct efa_com_create_qp_result {
	u32 qp_handle;
	u32 qp_num;
	u32 sq_db_offset;
	u32 rq_db_offset;
	u32 llq_descriptors_offset;
	u16 send_sub_cq_idx;
	u16 recv_sub_cq_idx;
};

struct efa_com_destroy_qp_params {
	u32 qp_handle;
};

struct efa_com_create_cq_params {
	/* completion queue depth in # of entries */
	u16 cq_depth;
	u8  entry_size_in_bytes;
	/* cq physical base address in OS memory */
	dma_addr_t dma_addr;
	u16 num_sub_cqs;
};

struct efa_com_create_cq_result {
	/* cq identifier */
	u16 cq_idx;
	/* actual cq depth in # of entries */
	u16 actual_depth;
};

struct efa_com_destroy_cq_params {
	u16 cq_idx;
};

struct efa_com_create_ah_params {
	/* Destination address in network byte order */
	u8 dest_addr[EFA_GID_SIZE];
};

struct efa_com_create_ah_result {
	u16 ah;
};

struct efa_com_destroy_ah_params {
	u16 ah;
};

struct efa_com_get_network_attr_result {
	u8 addr[EFA_GID_SIZE];
	u32 mtu;
};

struct efa_com_get_device_attr_result {
	u32 fw_version;
	u32 admin_api_version;
	u32 vendor_id;
	u32 vendor_part_id;
	u32 device_version;
	u32 supported_features;
	u32 phys_addr_width;
	u32 virt_addr_width;
	u32 max_sq;
	u16 max_sq_depth;
	u32 max_rq;
	u16 max_rq_depth;
	u32 max_cq;
	u32 max_cq_depth;
	u32 inline_buf_size;
	u16 max_sq_sge;
	u16 max_rq_sge;
	u32 max_mr;
	u64 max_mr_pages;
	u64 page_size_cap;
	u32 max_pd;
	u32 max_ah;
	u16 sub_cqs_per_cq;
	u8  db_bar;
};

struct efa_com_get_hw_hints_result {
	u16 mmio_read_timeout;
	u16 driver_watchdog_timeout;
	u16 admin_completion_timeout;
	u16 poll_interval;
	u32 reserved[4];
};

struct efa_com_mem_addr {
	u32 mem_addr_low;
	u32 mem_addr_high;
};

/* Used at indirect mode page list chunks for chaining */
struct efa_com_ctrl_buff_info {
	/* indicates length of the buffer pointed by control_buffer_address. */
	u32 length;
	/* points to control buffer (direct or indirect) */
	struct efa_com_mem_addr address;
};

struct efa_com_reg_mr_params {
	/* Protection Domain */
	u16 pd;
	/* Memory region length, in bytes. */
	u64 mr_length_in_bytes;
	/*
	 * phys_page_size_shift - page size is (1 << phys_page_size_shift)
	 * Page size is used for building the Virtual to Physical
	 * address mapping
	 */
	u8 page_shift;
	/*
	 * permissions
	 * 0: local_write_enable - Write permissions: value of 1 needed
	 * for RQ buffers and for RDMA write:1: reserved1 - remote
	 * access flags, etc
	 */
	u8 permissions;
	bool inline_pbl;
	bool indirect;
	/* number of pages in PBL (redundant, could be calculated) */
	u32 page_num;
	/* IO Virtual Address associated with this MR. */
	u64 iova;
	/* words 8:15: Physical Buffer List, each element is page-aligned. */
	union {
		/*
		 * Inline array of physical addresses of app pages
		 * (optimization for short region reservations)
		 */
		u64 inline_pbl_array[4];
		/*
		 * Describes the next physically contiguous chunk of indirect
		 * page list. A page list contains physical addresses of command
		 * data pages. Data pages are 4KB; page list chunks are
		 * variable-sized.
		 */
		struct efa_com_ctrl_buff_info pbl;
	} pbl;
};

struct efa_com_reg_mr_result {
	/*
	 * To be used in conjunction with local buffers references in SQ and
	 * RQ WQE
	 */
	u32 l_key;
	/*
	 * To be used in incoming RDMA semantics messages to refer to remotely
	 * accessed memory region
	 */
	u32 r_key;
};

struct efa_com_dereg_mr_params {
	u32 l_key;
};

void efa_com_set_dma_addr(dma_addr_t addr, u32 *addr_high, u32 *addr_low);
int efa_com_create_qp(struct efa_com_dev *edev,
		      struct efa_com_create_qp_params *params,
		      struct efa_com_create_qp_result *res);
int efa_com_destroy_qp(struct efa_com_dev *edev,
		       struct efa_com_destroy_qp_params *params);
int efa_com_create_cq(struct efa_com_dev *edev,
		      struct efa_com_create_cq_params *params,
		      struct efa_com_create_cq_result *result);
int efa_com_destroy_cq(struct efa_com_dev *edev,
		       struct efa_com_destroy_cq_params *params);
int efa_com_register_mr(struct efa_com_dev *edev,
			struct efa_com_reg_mr_params *params,
			struct efa_com_reg_mr_result *result);
int efa_com_dereg_mr(struct efa_com_dev *edev,
		     struct efa_com_dereg_mr_params *params);
int efa_com_create_ah(struct efa_com_dev *edev,
		      struct efa_com_create_ah_params *params,
		      struct efa_com_create_ah_result *result);
int efa_com_destroy_ah(struct efa_com_dev *edev,
		       struct efa_com_destroy_ah_params *params);
int efa_com_get_network_attr(struct efa_com_dev *edev,
			     struct efa_com_get_network_attr_result *result);
int efa_com_get_device_attr(struct efa_com_dev *edev,
			    struct efa_com_get_device_attr_result *result);
int efa_com_get_hw_hints(struct efa_com_dev *edev,
			 struct efa_com_get_hw_hints_result *result);
int efa_com_set_aenq_config(struct efa_com_dev *edev, u32 groups);

#endif /* _EFA_COM_CMD_H_ */

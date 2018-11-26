/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */
#ifndef _EFA_ADMIN_CMDS_H_
#define _EFA_ADMIN_CMDS_H_

#define EFA_ADMIN_API_VERSION_MAJOR          0
#define EFA_ADMIN_API_VERSION_MINOR          1

/* EFA admin queue opcodes */
enum efa_admin_aq_opcode {
	/* starting opcode of efa admin commands */
	EFA_ADMIN_START_CMD_RANGE                   = 100,
	/* Query device capabilities */
	EFA_ADMIN_QUERY_DEV                         = EFA_ADMIN_START_CMD_RANGE,
	/* Modify device attributes */
	EFA_ADMIN_MODIFY_DEV                        = 101,
	/* Create QP */
	EFA_ADMIN_CREATE_QP                         = 102,
	/* Modify QP */
	EFA_ADMIN_MODIFY_QP                         = 103,
	/* Query QP */
	EFA_ADMIN_QUERY_QP                          = 104,
	/* Destroy QP */
	EFA_ADMIN_DESTROY_QP                        = 105,
	/* Create Address Handle */
	EFA_ADMIN_CREATE_AH                         = 106,
	/* Destroy Address Handle */
	EFA_ADMIN_DESTROY_AH                        = 107,
	/* Register Memory Region */
	EFA_ADMIN_REG_MR                            = 108,
	/* Deregister Memory Region */
	EFA_ADMIN_DEREG_MR                          = 109,
	/* Create Completion Q */
	EFA_ADMIN_CREATE_CQ                         = 110,
	/* Destroy Completion Q */
	EFA_ADMIN_DESTROY_CQ                        = 111,
	EFA_ADMIN_GET_FEATURE                       = 112,
	EFA_ADMIN_SET_FEATURE                       = 113,
	EFA_ADMIN_GET_STATS                         = 114,
	EFA_ADMIN_MAX_OPCODE                        = 114,
};

enum efa_admin_aq_feature_id {
	EFA_ADMIN_DEVICE_ATTR                       = 1,
	EFA_ADMIN_AENQ_CONFIG                       = 2,
	EFA_ADMIN_NETWORK_ATTR                      = 3,
	EFA_ADMIN_QUEUE_ATTR                        = 4,
	EFA_ADMIN_HW_HINTS                          = 5,
	EFA_ADMIN_FEATURES_OPCODE_NUM               = 8,
};

/* QP transport type */
enum efa_admin_qp_type {
	/* Unreliable Datagram */
	EFA_ADMIN_QP_TYPE_UD                        = 1,
	/* Scalable Reliable Datagram */
	EFA_ADMIN_QP_TYPE_SRD                       = 2,
};

/* QP state */
enum efa_admin_qp_state {
	/* Reset queue */
	EFA_ADMIN_QP_STATE_RESET                    = 0,
	/* Init queue */
	EFA_ADMIN_QP_STATE_INIT                     = 1,
	/* Ready to receive */
	EFA_ADMIN_QP_STATE_RTR                      = 2,
	/* Ready to send */
	EFA_ADMIN_QP_STATE_RTS                      = 3,
	/* Send queue drain */
	EFA_ADMIN_QP_STATE_SQD                      = 4,
	/* Send queue error */
	EFA_ADMIN_QP_STATE_SQE                      = 5,
	/* Queue in error state */
	EFA_ADMIN_QP_STATE_ERR                      = 6,
};

/* Device attributes */
struct efa_admin_dev_attr {
	/* FW version */
	u32 fw_ver;

	u32 max_mr_size;

	u32 max_qp;

	u32 max_cq;

	u32 max_mr;

	u32 max_pd;

	u32 max_ah;

	/* Enable low-latency queues */
	u32 llq_en;
};

/* Query device command */
struct efa_admin_query_dev {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;
};

/* Query device response. */
struct efa_admin_query_dev_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* Device attributes */
	struct efa_admin_dev_attr dev_attr;
};

/* Modify device command */
struct efa_admin_modify_dev {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Device attributes */
	struct efa_admin_dev_attr dev_attr;
};

/* Modify device response. */
struct efa_admin_modify_dev_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * QP allocation sizes, converted by fabric QueuePair (QP) create command
 * from QP capabilities.
 */
struct efa_admin_qp_alloc_size {
	/* Send descriptor ring size in bytes */
	u32 send_queue_ring_size;

	/* Max number of WQEs that can be outstanding on send queue. */
	u32 send_queue_depth;

	/*
	 * Recv descriptor ring size in bytes, sufficient for user-provided
	 * number of WQEs
	 */
	u32 recv_queue_ring_size;

	/* MBZ */
	u32 reserved;
};

/* Create QP command. */
struct efa_admin_create_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Protection Domain associated with this QP */
	u16 pd;

	/* QP type */
	u8 qp_type;

	/*
	 * 0 : sq_virt - If set, SQ ring base address is
	 *    virtual (IOVA returned by MR registration)
	 * 1 : rq_virt - If set, RQ ring base address is
	 *    virtual (IOVA returned by MR registration)
	 * 7:2 : reserved - MBZ
	 */
	u8 flags;

	/*
	 * Send queue (SQ) ring base physical address. This field is not
	 * used if this is a Low Latency Queue(LLQ).
	 */
	u64 sq_base_addr;

	/* Receive queue (RQ) ring base address. */
	u64 rq_base_addr;

	/* Index of CQ to be associated with Send Queue completions */
	u32 send_cq_idx;

	/* Index of CQ to be associated with Recv Queue completions */
	u32 recv_cq_idx;

	/*
	 * Memory registration key for the SQ ring, used only when not in
	 * LLQ mode and base address is virtual
	 */
	u32 sq_l_key;

	/*
	 * Memory registration key for the RQ ring, used only when base
	 * address is virtual
	 */
	u32 rq_l_key;

	/* Requested QP allocation sizes */
	struct efa_admin_qp_alloc_size qp_alloc_size;
};

/* Create QP response. */
struct efa_admin_create_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* Opaque handle to be used for consequent operations on the QP */
	u32 qp_handle;

	/* QP number in the given EFA virtual device */
	u16 qp_num;

	/* MBZ */
	u16 reserved;

	/* Index of sub-CQ for Send Queue completions */
	u16 send_sub_cq_idx;

	/* Index of sub-CQ for Receive Queue completions */
	u16 recv_sub_cq_idx;

	/* SQ doorbell address, as offset to PCIe DB BAR */
	u32 sq_db_offset;

	/* RQ doorbell address, as offset to PCIe DB BAR */
	u32 rq_db_offset;

	/*
	 * low latency send queue ring base address as an offset to PCIe
	 * MMIO LLQ_MEM BAR
	 */
	u32 llq_descriptors_offset;
};

/* Modify QP command */
struct efa_admin_modify_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* QP handle returned by create_qp command */
	u32 qp_handle;

	/* QP state */
	u32 qp_state;

	/* Override current QP state (before applying the transition) */
	u32 cur_qp_state;

	/* QKey */
	u32 qkey;

	/* Enable async notification when SQ is drained */
	u32 sq_drained_async_notify;
};

/* Modify QP response */
struct efa_admin_modify_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/* Query QP command */
struct efa_admin_query_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* QP handle returned by create_qp command */
	u32 qp_handle;
};

/* Query QP response */
struct efa_admin_query_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* QP state */
	u32 qp_state;

	/* QKey */
	u32 qkey;

	/* Indicates that draining is in progress */
	u32 sq_draining;
};

/* Destroy QP command */
struct efa_admin_destroy_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* QP handle returned by create_qp command */
	u32 qp_handle;
};

/* Destroy QP response */
struct efa_admin_destroy_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * Create Address Handle command parameters. Must not be called more than
 * once for the same destination
 */
struct efa_admin_create_ah_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Destination address in network byte order */
	u8 dest_addr[16];
};

/* Create Address Handle response */
struct efa_admin_create_ah_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* Target interface address handle (opaque) */
	u16 ah;

	u16 reserved;
};

/* Destroy Address Handle command parameters. */
struct efa_admin_destroy_ah_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Target interface address handle (opaque) */
	u16 ah;

	u16 reserved;
};

/* Destroy Address Handle response */
struct efa_admin_destroy_ah_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * Registration of MemoryRegion, required for QP working with Virtual
 * Addresses. In standard verbs semantics, region length is limited to 2GB
 * space, but EFA offers larger MR support for large memory space, to ease
 * on users working with very large datasets (i.e. full GPU memory mapping).
 */
struct efa_admin_reg_mr_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Protection Domain */
	u16 pd;

	/* MBZ */
	u16 reserved16_w1;

	/* Physical Buffer List, each element is page-aligned. */
	union {
		/*
		 * Inline array of guest-physical page addresses of user
		 * memory pages (optimization for short region
		 * registrations)
		 */
		u64 inline_pbl_array[4];

		/* points to PBL (direct or indirect, chained if needed) */
		struct efa_admin_ctrl_buff_info pbl;
	} pbl;

	/* Memory region length, in bytes. */
	u64 mr_length;

	/*
	 * flags and page size
	 * 4:0 : phys_page_size_shift - page size is (1 <<
	 *    phys_page_size_shift). Page size is used for
	 *    building the Virtual to Physical address mapping
	 * 6:5 : reserved - MBZ
	 * 7 : mem_addr_phy_mode_en - Enable bit for physical
	 *    memory registration (no translation), can be used
	 *    only by privileged clients. If set, PBL must
	 *    contain a single entry.
	 */
	u8 flags;

	/*
	 * permissions
	 * 0 : local_write_enable - Write permissions: value
	 *    of 1 needed for RQ buffers and for RDMA write
	 * 7:1 : reserved1 - remote access flags, etc
	 */
	u8 permissions;

	u16 reserved16_w5;

	/* number of pages in PBL (redundant, could be calculated) */
	u32 page_num;

	/*
	 * IO Virtual Address associated with this MR. If
	 * mem_addr_phy_mode_en is set, contains the physical address of
	 * the region.
	 */
	u64 iova;
};

struct efa_admin_reg_mr_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/*
	 * L_Key, to be used in conjunction with local buffer references in
	 * SQ and RQ WQE, or with virtual RQ/CQ rings
	 */
	u32 l_key;

	/*
	 * R_Key, to be used in RDMA messages to refer to remotely accessed
	 * memory region
	 */
	u32 r_key;
};

/* Deregister a MemoryRegion */
struct efa_admin_dereg_mr_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* L_Key, memory region's l_key */
	u32 l_key;
};

struct efa_admin_dereg_mr_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/* Create CQ command */
struct efa_admin_create_cq_cmd {
	struct efa_admin_aq_common_desc aq_common_desc;

	/*
	 * 4:0 : reserved5
	 * 5 : interrupt_mode_enabled - if set, cq operates
	 *    in interrupt mode (i.e. CQ events and MSI-X are
	 *    generated), otherwise - polling
	 * 6 : virt - If set, ring base address is virtual
	 *    (IOVA returned by MR registration)
	 * 7 : reserved6
	 */
	u8 cq_caps_1;

	/*
	 * 4:0 : cq_entry_size_words - size of CQ entry in
	 *    32-bit words, valid values: 4, 8.
	 * 7:5 : reserved7
	 */
	u8 cq_caps_2;

	/* completion queue depth in # of entries. must be power of 2 */
	u16 cq_depth;

	/* msix vector assigned to this cq */
	u32 msix_vector_idx;

	/*
	 * CQ ring base address, virtual or physical depending on 'virt'
	 * flag
	 */
	struct efa_common_mem_addr cq_ba;

	/*
	 * Memory registration key for the ring, used only when base
	 * address is virtual
	 */
	u32 l_key;

	/*
	 * number of sub cqs - must be equal to sub_cqs_per_cq of queue
	 *    attributes.
	 */
	u16 num_sub_cqs;

	u16 reserved8;
};

struct efa_admin_create_cq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	u16 cq_idx;

	/* actual cq depth in number of entries */
	u16 cq_actual_depth;
};

/* Destroy CQ command */
struct efa_admin_destroy_cq_cmd {
	struct efa_admin_aq_common_desc aq_common_desc;

	u16 cq_idx;

	u16 reserved1;
};

struct efa_admin_destroy_cq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * EFA AQ Get Statistics command. Extended statistics are placed in control
 * buffer pointed by AQ entry
 */
struct efa_admin_aq_get_stats_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	union {
		/* command specific inline data */
		u32 inline_data_w1[3];

		struct efa_admin_ctrl_buff_info control_buffer;
	} u;

	/* stats type as defined in enum efa_admin_get_stats_type */
	u8 type;

	/* stats scope defined in enum efa_admin_get_stats_scope */
	u8 scope;

	u16 reserved3;

	/* queue id. used when scope is specific_queue */
	u16 queue_idx;

	/*
	 * device id, value 0xFFFF means mine. only privileged device can get
	 *    stats of other device
	 */
	u16 device_id;
};

/* Basic Statistics Command. */
struct efa_admin_basic_stats {
	u64 tx_bytes;

	u64 tx_pkts;

	u64 rx_bytes;

	u64 rx_pkts;

	u64 rx_drops;
};

struct efa_admin_acq_get_stats_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	struct efa_admin_basic_stats basic_stats;
};

struct efa_admin_get_set_feature_common_desc {
	/*
	 * 1:0 : select - 0x1 - current value; 0x3 - default
	 *    value
	 * 7:3 : reserved3
	 */
	u8 flags;

	/* as appears in efa_admin_aq_feature_id */
	u8 feature_id;

	/* MBZ */
	u16 reserved16;
};

struct efa_admin_feature_device_attr_desc {
	u32 fw_version;

	u32 admin_api_version;

	u32 device_version;

	/* MBZ */
	u32 reserved1;

	/* bitmap of efa_admin_aq_feature_id */
	u64 supported_features;

	/* Indicates how many bits are used physical address access. */
	u8 phys_addr_width;

	/* MBZ */
	u8 reserved2;

	/* Indicates how many bits are used virtual address access. */
	u8 virt_addr_width;

	/* MBZ */
	u8 reserved3;

	/* Bar used for SQ and RQ doorbells. */
	u16 db_bar;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_feature_queue_attr_desc {
	/* The maximum number of send queues supported */
	u32 max_sq;

	u32 max_sq_depth;

	/* max send wr used in inline-buf */
	u32 inline_buf_size;

	/* The maximum number of receive queues supported */
	u32 max_rq;

	u32 max_rq_depth;

	/* The maximum number of completion queues supported per VF */
	u32 max_cq;

	u32 max_cq_depth;

	/* Number of sub-CQs to be created for each CQ */
	u16 sub_cqs_per_cq;

	u16 reserved;

	/*
	 * Maximum number of SGEs (buffs) allowed for a single send work
	 *    queue element (WQE)
	 */
	u16 max_wr_send_sges;

	/* Maximum number of SGEs allowed for a single recv WQE */
	u16 max_wr_recv_sges;

	/* The maximum number of memory regions supported */
	u32 max_mr;

	/* The maximum number of pages can be registered */
	u32 max_mr_pages;

	/* The maximum number of protection domains supported */
	u32 max_pd;

	/* The maximum number of address handles supported */
	u32 max_ah;
};

struct efa_admin_feature_aenq_desc {
	/* bitmask for AENQ groups the device can report */
	u32 supported_groups;

	/* bitmask for AENQ groups to report */
	u32 enabled_groups;
};

struct efa_admin_feature_network_attr_desc {
	/* Raw address data in network byte order */
	u8 addr[16];

	u32 mtu;
};

/*
 * When hint value is 0, hints capabilities are not supported or driver
 * should use its own predefined value
 */
struct efa_admin_hw_hints {
	/* value in ms */
	u16 mmio_read_timeout;

	/* value in ms */
	u16 driver_watchdog_timeout;

	/* value in ms */
	u16 admin_completion_timeout;

	/* poll interval in ms */
	u16 poll_interval;
};

struct efa_admin_get_feature_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	struct efa_admin_ctrl_buff_info control_buffer;

	struct efa_admin_get_set_feature_common_desc feature_common;

	u32 raw[11];
};

struct efa_admin_get_feature_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];

		struct efa_admin_feature_device_attr_desc device_attr;

		struct efa_admin_feature_aenq_desc aenq;

		struct efa_admin_feature_network_attr_desc network_attr;

		struct efa_admin_feature_queue_attr_desc queue_attr;

		struct efa_admin_hw_hints hw_hints;
	} u;
};

struct efa_admin_set_feature_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	struct efa_admin_ctrl_buff_info control_buffer;

	struct efa_admin_get_set_feature_common_desc feature_common;

	union {
		u32 raw[11];

		/* AENQ configuration */
		struct efa_admin_feature_aenq_desc aenq;
	} u;
};

struct efa_admin_set_feature_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];
	} u;
};

/* asynchronous event notification groups */
enum efa_admin_aenq_group {
	EFA_ADMIN_FATAL_ERROR                       = 1,
	EFA_ADMIN_WARNING                           = 2,
	EFA_ADMIN_NOTIFICATION                      = 3,
	EFA_ADMIN_KEEP_ALIVE                        = 4,
	EFA_ADMIN_AENQ_GROUPS_NUM                   = 5,
};

enum efa_admin_aenq_notification_syndrom {
	EFA_ADMIN_SUSPEND                           = 0,
	EFA_ADMIN_RESUME                            = 1,
	EFA_ADMIN_UPDATE_HINTS                      = 2,
};

struct efa_admin_mmio_req_read_less_resp {
	u16 req_id;

	u16 reg_off;

	/* value is valid when poll is cleared */
	u32 reg_val;
};

/* create_qp_cmd */
#define EFA_ADMIN_CREATE_QP_CMD_SQ_VIRT_MASK                BIT(0)
#define EFA_ADMIN_CREATE_QP_CMD_RQ_VIRT_SHIFT               1
#define EFA_ADMIN_CREATE_QP_CMD_RQ_VIRT_MASK                BIT(1)

/* reg_mr_cmd */
#define EFA_ADMIN_REG_MR_CMD_PHYS_PAGE_SIZE_SHIFT_MASK      GENMASK(4, 0)
#define EFA_ADMIN_REG_MR_CMD_MEM_ADDR_PHY_MODE_EN_SHIFT     7
#define EFA_ADMIN_REG_MR_CMD_MEM_ADDR_PHY_MODE_EN_MASK      BIT(7)
#define EFA_ADMIN_REG_MR_CMD_LOCAL_WRITE_ENABLE_MASK        BIT(0)

/* create_cq_cmd */
#define EFA_ADMIN_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_SHIFT 5
#define EFA_ADMIN_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_MASK BIT(5)
#define EFA_ADMIN_CREATE_CQ_CMD_VIRT_SHIFT                  6
#define EFA_ADMIN_CREATE_CQ_CMD_VIRT_MASK                   BIT(6)
#define EFA_ADMIN_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK    GENMASK(4, 0)

/* get_set_feature_common_desc */
#define EFA_ADMIN_GET_SET_FEATURE_COMMON_DESC_SELECT_MASK   GENMASK(1, 0)

#endif /*_EFA_ADMIN_CMDS_H_ */

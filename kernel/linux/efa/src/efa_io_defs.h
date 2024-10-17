/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2023 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_IO_H_
#define _EFA_IO_H_

#define EFA_IO_TX_DESC_NUM_BUFS              2
#define EFA_IO_TX_DESC_NUM_RDMA_BUFS         1
#define EFA_IO_TX_DESC_INLINE_MAX_SIZE       32
#define EFA_IO_TX_DESC_IMM_DATA_SIZE         4
#ifdef HAVE_EFA_KVERBS
#define EFA_IO_TX_DESC_INLINE_PBL_SIZE        1
#endif

enum efa_io_queue_type {
	/* send queue (of a QP) */
	EFA_IO_SEND_QUEUE                           = 1,
	/* recv queue (of a QP) */
	EFA_IO_RECV_QUEUE                           = 2,
};

enum efa_io_send_op_type {
	/* send message */
	EFA_IO_SEND                                 = 0,
	/* RDMA read */
	EFA_IO_RDMA_READ                            = 1,
	/* RDMA write */
	EFA_IO_RDMA_WRITE                           = 2,
#ifdef HAVE_EFA_KVERBS
	/* Fast MR registration */
	EFA_IO_FAST_REG                             = 3,
	/* Fast MR invalidation */
	EFA_IO_FAST_INV                             = 4,
#endif
};

enum efa_io_comp_status {
	/* Successful completion */
	EFA_IO_COMP_STATUS_OK                       = 0,
	/* Flushed during QP destroy */
	EFA_IO_COMP_STATUS_FLUSHED                  = 1,
	/* Internal QP error */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_QP_INTERNAL_ERROR = 2,
	/* Bad operation type */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_INVALID_OP_TYPE = 3,
	/* Bad AH */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_INVALID_AH   = 4,
	/* LKEY not registered or does not match IOVA */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_INVALID_LKEY = 5,
	/* Message too long */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_BAD_LENGTH   = 6,
	/* Destination ENI is down or does not run EFA */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_ADDRESS = 7,
	/* Connection was reset by remote side */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_ABORT       = 8,
	/* Bad dest QP number (QP does not exist or is in error state) */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_DEST_QPN = 9,
	/* Destination resource not ready (no WQEs posted on RQ) */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_RNR         = 10,
	/* Receiver SGL too short */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_LENGTH  = 11,
	/* Unexpected status returned by responder */
	EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_STATUS  = 12,
	/* Unresponsive remote - detected locally */
	EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE = 13,
};

#ifdef HAVE_EFA_KVERBS
enum efa_io_frwr_pbl_mode {
	EFA_IO_FRWR_INLINE_PBL                      = 0,
	EFA_IO_FRWR_DIRECT_PBL                      = 1,
};
#endif

struct efa_io_tx_meta_desc {
	/* Verbs-generated Request ID */
	u16 req_id;

	/*
	 * control flags
	 * 3:0 : op_type - enum efa_io_send_op_type
	 * 4 : has_imm - immediate_data field carries valid
	 *    data.
	 * 5 : inline_msg - inline mode - inline message data
	 *    follows this descriptor (no buffer descriptors).
	 *    Note that it is different from immediate data
	 * 6 : meta_extension - Extended metadata. MBZ
	 * 7 : meta_desc - Indicates metadata descriptor.
	 *    Must be set.
	 */
	u8 ctrl1;

	/*
	 * control flags
	 * 0 : phase
	 * 1 : reserved25 - MBZ
	 * 2 : first - Indicates first descriptor in
	 *    transaction. Must be set.
	 * 3 : last - Indicates last descriptor in
	 *    transaction. Must be set.
	 * 4 : comp_req - Indicates whether completion should
	 *    be posted, after packet is transmitted. Valid only
	 *    for the first descriptor
	 * 7:5 : reserved29 - MBZ
	 */
	u8 ctrl2;

	u16 dest_qp_num;

	/*
	 * If inline_msg bit is set, length of inline message in bytes,
	 *    otherwise length of SGL (number of buffers).
	 */
	u16 length;

	/*
	 * immediate data: if has_imm is set, then this field is included
	 *    within Tx message and reported in remote Rx completion.
	 */
	u32 immediate_data;

	u16 ah;

	u16 reserved;

	/* Queue key */
	u32 qkey;

	u8 reserved2[12];
};

/*
 * Tx queue buffer descriptor, for any transport type. Preceded by metadata
 * descriptor.
 */
struct efa_io_tx_buf_desc {
	/* length in bytes */
	u32 length;

	/*
	 * 23:0 : lkey - local memory translation key
	 * 31:24 : reserved - MBZ
	 */
	u32 lkey;

	/* Buffer address bits[31:0] */
	u32 buf_addr_lo;

	/* Buffer address bits[63:32] */
	u32 buf_addr_hi;
};

struct efa_io_remote_mem_addr {
	/* length in bytes */
	u32 length;

	/* remote memory translation key */
	u32 rkey;

	/* Buffer address bits[31:0] */
	u32 buf_addr_lo;

	/* Buffer address bits[63:32] */
	u32 buf_addr_hi;
};

struct efa_io_rdma_req {
	/* Remote memory address */
	struct efa_io_remote_mem_addr remote_mem;

	/* Local memory address */
	struct efa_io_tx_buf_desc local_mem[1];
};

#ifdef HAVE_EFA_KVERBS
struct efa_io_fast_mr_reg_req {
	/* Updated local key of the MR after lkey/rkey increment */
	u32 lkey;

	/*
	 * permissions
	 * 0 : local_write_enable - Local write permissions:
	 *    must be set for RQ buffers and buffers posted for
	 *    RDMA Read requests
	 * 1 : remote_write_enable - Remote write
	 *    permissions: must be set to enable RDMA write to
	 *    the region
	 * 2 : remote_read_enable - Remote read permissions:
	 *    must be set to enable RDMA read from the region
	 * 7:3 : reserved2 - MBZ
	 */
	u8 permissions;

	/*
	 * control flags
	 * 4:0 : phys_page_size_shift - page size is (1 <<
	 *    phys_page_size_shift)
	 * 6:5 : pbl_mode - enum efa_io_frwr_pbl_mode
	 * 7 : reserved - MBZ
	 */
	u8 flags;

	/* MBZ */
	u8 reserved[2];

	/* IO Virtual Address associated with this MR */
	u64 iova;

	/* Memory region length, in bytes */
	u64 mr_length;

	/* Physical Buffer List, each element is page-aligned. */
	union {
		/*
		 * Inline array of physical page addresses (optimization
		 * for short region activation).
		 */
		u64 inline_array[1];

		/* points to PBL (Currently only direct) */
		u64 dma_addr;
	} pbl;
};

struct efa_io_fast_mr_inv_req {
	/* Local key of the MR to invalidate */
	u32 lkey;

	/* MBZ */
	u8 reserved[28];
};
#endif

/*
 * Tx WQE, composed of tx meta descriptors followed by either tx buffer
 * descriptors or inline data
 */
struct efa_io_tx_wqe {
	/* TX meta */
	struct efa_io_tx_meta_desc meta;

	union {
		/* Send buffer descriptors */
		struct efa_io_tx_buf_desc sgl[2];

		u8 inline_data[32];

		/* RDMA local and remote memory addresses */
		struct efa_io_rdma_req rdma_req;
#ifdef HAVE_EFA_KVERBS
		/* Fast registration */
		struct efa_io_fast_mr_reg_req reg_mr_req;

		/* Fast invalidation */
		struct efa_io_fast_mr_inv_req inv_mr_req;
#endif
	} data;
};

/*
 * Rx buffer descriptor; RX WQE is composed of one or more RX buffer
 * descriptors.
 */
struct efa_io_rx_desc {
	/* Buffer address bits[31:0] */
	u32 buf_addr_lo;

	/* Buffer Pointer[63:32] */
	u32 buf_addr_hi;

	/* Verbs-generated request id. */
	u16 req_id;

	/* Length in bytes. */
	u16 length;

	/*
	 * LKey and control flags
	 * 23:0 : lkey
	 * 29:24 : reserved - MBZ
	 * 30 : first - Indicates first descriptor in WQE
	 * 31 : last - Indicates last descriptor in WQE
	 */
	u32 lkey_ctrl;
};

/* Common IO completion descriptor */
struct efa_io_cdesc_common {
	/*
	 * verbs-generated request ID, as provided in the completed tx or rx
	 *    descriptor.
	 */
	u16 req_id;

	u8 status;

	/*
	 * flags
	 * 0 : phase - Phase bit
	 * 2:1 : q_type - enum efa_io_queue_type: send/recv
	 * 3 : has_imm - indicates that immediate data is
	 *    present - for RX completions only
	 * 6:4 : op_type - enum efa_io_send_op_type
	 * 7 : reserved31 - MBZ
	 */
	u8 flags;

	/* local QP number */
	u16 qp_num;
};

/* Tx completion descriptor */
struct efa_io_tx_cdesc {
	/* Common completion info */
	struct efa_io_cdesc_common common;

	/* MBZ */
	u16 reserved16;
};

/* Rx Completion Descriptor */
struct efa_io_rx_cdesc {
	/* Common completion info */
	struct efa_io_cdesc_common common;

	/* Transferred length bits[15:0] */
	u16 length;

	/* Remote Address Handle FW index, 0xFFFF indicates invalid ah */
	u16 ah;

	u16 src_qp_num;

	/* Immediate data */
	u32 imm;
};

/* Rx Completion Descriptor RDMA write info */
struct efa_io_rx_cdesc_rdma_write {
	/* Transferred length bits[31:16] */
	u16 length_hi;
};

/* Extended Rx Completion Descriptor */
struct efa_io_rx_cdesc_ex {
	/* Base RX completion info */
	struct efa_io_rx_cdesc base;

	union {
		struct efa_io_rx_cdesc_rdma_write rdma_write;

		/*
		 * Valid only in case of unknown AH (0xFFFF) and CQ
		 * set_src_addr is enabled.
		 */
		u8 src_addr[16];
	} u;
};

/* tx_meta_desc */
#define EFA_IO_TX_META_DESC_OP_TYPE_MASK                    GENMASK(3, 0)
#define EFA_IO_TX_META_DESC_HAS_IMM_MASK                    BIT(4)
#define EFA_IO_TX_META_DESC_INLINE_MSG_MASK                 BIT(5)
#define EFA_IO_TX_META_DESC_META_EXTENSION_MASK             BIT(6)
#define EFA_IO_TX_META_DESC_META_DESC_MASK                  BIT(7)
#define EFA_IO_TX_META_DESC_PHASE_MASK                      BIT(0)
#define EFA_IO_TX_META_DESC_FIRST_MASK                      BIT(2)
#define EFA_IO_TX_META_DESC_LAST_MASK                       BIT(3)
#define EFA_IO_TX_META_DESC_COMP_REQ_MASK                   BIT(4)

/* tx_buf_desc */
#define EFA_IO_TX_BUF_DESC_LKEY_MASK                        GENMASK(23, 0)

#ifdef HAVE_EFA_KVERBS
/* fast_mr_reg_req */
#define EFA_IO_FAST_MR_REG_REQ_LOCAL_WRITE_ENABLE_MASK      BIT(0)
#define EFA_IO_FAST_MR_REG_REQ_REMOTE_WRITE_ENABLE_MASK     BIT(1)
#define EFA_IO_FAST_MR_REG_REQ_REMOTE_READ_ENABLE_MASK      BIT(2)
#define EFA_IO_FAST_MR_REG_REQ_PHYS_PAGE_SIZE_SHIFT_MASK    GENMASK(4, 0)
#define EFA_IO_FAST_MR_REG_REQ_PBL_MODE_MASK                GENMASK(6, 5)
#endif

/* rx_desc */
#define EFA_IO_RX_DESC_LKEY_MASK                            GENMASK(23, 0)
#define EFA_IO_RX_DESC_FIRST_MASK                           BIT(30)
#define EFA_IO_RX_DESC_LAST_MASK                            BIT(31)

/* cdesc_common */
#define EFA_IO_CDESC_COMMON_PHASE_MASK                      BIT(0)
#define EFA_IO_CDESC_COMMON_Q_TYPE_MASK                     GENMASK(2, 1)
#define EFA_IO_CDESC_COMMON_HAS_IMM_MASK                    BIT(3)
#define EFA_IO_CDESC_COMMON_OP_TYPE_MASK                    GENMASK(6, 4)

#endif /* _EFA_IO_H_ */

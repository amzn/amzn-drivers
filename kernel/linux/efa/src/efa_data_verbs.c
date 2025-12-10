// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2024-2025 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <rdma/ib_verbs.h>

#include "efa.h"
#include "efa_verbs.h"
#include "efa_io_defs.h"

static inline struct efa_dev *to_edev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct efa_dev, ibdev);
}

static inline struct efa_mr *to_emr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct efa_mr, ibmr);
}

static inline struct efa_qp *to_eqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct efa_qp, ibqp);
}

static inline struct efa_cq *to_ecq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct efa_cq, ibcq);
}

static inline struct efa_ah *to_eah(struct ib_ah *ibah)
{
	return container_of(ibah, struct efa_ah, ibah);
}

static u32 efa_sge_total_bytes(const struct ib_sge *sg_list, int num_sge)
{
	u32 bytes = 0;
	int i;

	for (i = 0; i < num_sge; i++)
		bytes += sg_list[i].length;

	return bytes;
}

static int efa_post_send_validate(struct efa_qp *qp, const struct ib_send_wr *wr)
{
	bool send_op = wr->opcode == IB_WR_SEND || wr->opcode == IB_WR_SEND_WITH_IMM;
	u32 max_sge;

	if (unlikely(qp->state != IB_QPS_RTS && qp->state != IB_QPS_SQD))
		return -EINVAL;

	if (unlikely(qp->sq.wq.wqes_posted - qp->sq.wq.wqes_completed == qp->sq.wq.max_wqes))
		return -ENOMEM;

	if (unlikely(wr->send_flags & ~(IB_SEND_SIGNALED | IB_SEND_INLINE)))
		return -EINVAL;

	if (unlikely(!(wr->send_flags & IB_SEND_SIGNALED) && qp->sq.sig_type != IB_SIGNAL_ALL_WR))
		return -EINVAL;

	if (unlikely(qp->ibqp.qp_type == IB_QPT_UD && !send_op))
		return -EINVAL;

	if (wr->send_flags & IB_SEND_INLINE) {
		if (unlikely(efa_sge_total_bytes(wr->sg_list, wr->num_sge) >
			     qp->sq.max_inline_data))
			return -EINVAL;
	} else {
		max_sge = send_op ? qp->sq.wq.max_sge : qp->sq.max_rdma_sges;

		if (unlikely(wr->num_sge > max_sge))
			return -EINVAL;
	}

	return 0;
}

static void efa_wqe_set_send_inline_data(struct efa_io_tx_wqe *wqe, const struct ib_send_wr *wr)
{
	const struct ib_sge *sgl = wr->sg_list;
	u32 total_length = 0;
	u32 length;
	int i;

	for (i = 0; i < wr->num_sge; i++) {
		length = sgl[i].length;

		memcpy(wqe->data.inline_data + total_length, (void *)sgl[i].addr, length);
		total_length += length;
	}

	EFA_SET(&wqe->meta.ctrl1, EFA_IO_TX_META_DESC_INLINE_MSG, 1);
	wqe->meta.length = total_length;
}

static void efa_set_tx_buf(struct efa_io_tx_buf_desc *tx_buf,
			   u64 addr, u32 lkey,
			   u32 length)
{
	tx_buf->length = length;
	EFA_SET(&tx_buf->lkey, EFA_IO_TX_BUF_DESC_LKEY, lkey);
	tx_buf->buf_addr_lo = addr & 0xffffffff;
	tx_buf->buf_addr_hi = addr >> 32;
}

static void efa_wqe_set_send_sgl(struct efa_io_tx_wqe *wqe, const struct ib_send_wr *wr)
{
	struct efa_io_tx_buf_desc *tx_bufs = wqe->data.sgl;
	const struct ib_sge *sg_list = wr->sg_list;
	const struct ib_sge *sge;
	size_t i;

	for (i = 0; i < wr->num_sge; i++) {
		sge = &sg_list[i];
		efa_set_tx_buf(&tx_bufs[i], sge->addr, sge->lkey, sge->length);
	}

	wqe->meta.length = wr->num_sge;
}

static void efa_wqe_set_rdma_info(struct efa_io_tx_wqe *wqe, const struct ib_send_wr *wr)
{
	const struct ib_srd_rdma_wr *srdrdmawr = srd_rdma_wr(wr);

	wqe->data.rdma_req.remote_mem.length = wr->sg_list[0].length;
	wqe->data.rdma_req.remote_mem.rkey = srdrdmawr->rkey;
	wqe->data.rdma_req.remote_mem.buf_addr_hi = srdrdmawr->remote_addr >> 32;
	wqe->data.rdma_req.remote_mem.buf_addr_lo = srdrdmawr->remote_addr & 0xffffffff;

	efa_set_tx_buf(wqe->data.rdma_req.local_mem, wr->sg_list[0].addr,
		       wr->sg_list[0].lkey, wr->sg_list[0].length);

	wqe->meta.length = 1;
}

static void efa_wqe_set_fast_reg_info(struct efa_io_tx_wqe *wqe, const struct ib_send_wr *wr)
{
	const struct ib_reg_wr *fr_wr = reg_wr(wr);
	struct efa_mr *mr = to_emr(fr_wr->mr);
	int page_shift;
	int i;

	page_shift = order_base_2(mr->ibmr.page_size);

	wqe->data.reg_mr_req.iova = mr->ibmr.iova;
	wqe->data.reg_mr_req.mr_length = mr->ibmr.length;
	wqe->data.reg_mr_req.lkey = mr->ibmr.lkey;
	wqe->data.reg_mr_req.permissions = fr_wr->access;
	EFA_SET(&wqe->data.reg_mr_req.flags, EFA_IO_FAST_MR_REG_REQ_PHYS_PAGE_SIZE_SHIFT,
		page_shift);

	if (mr->num_pages <= EFA_IO_TX_DESC_INLINE_PBL_SIZE) {
		for (i = 0; i < mr->num_pages; i++)
			wqe->data.reg_mr_req.pbl.inline_array[i] = mr->pages_list[i];

		EFA_SET(&wqe->data.reg_mr_req.flags, EFA_IO_FAST_MR_REG_REQ_PBL_MODE,
			EFA_IO_FRWR_INLINE_PBL);
	} else {
		wqe->data.reg_mr_req.pbl.dma_addr = mr->pages_list_dma_addr;
		EFA_SET(&wqe->data.reg_mr_req.flags, EFA_IO_FAST_MR_REG_REQ_PBL_MODE,
			EFA_IO_FRWR_DIRECT_PBL);
	}
}

static void efa_wqe_set_fast_inv_info(struct efa_io_tx_wqe *wqe, const struct ib_send_wr *wr)
{
	wqe->data.inv_mr_req.lkey = wr->ex.invalidate_rkey;
}

static void efa_set_common_ctrl_flags(struct efa_io_tx_meta_desc *desc, struct efa_qp *qp,
				      enum efa_io_send_op_type op_type)
{
	EFA_SET(&desc->ctrl1, EFA_IO_TX_META_DESC_META_DESC, 1);
	EFA_SET(&desc->ctrl1, EFA_IO_TX_META_DESC_OP_TYPE, op_type);
	EFA_SET(&desc->ctrl2, EFA_IO_TX_META_DESC_PHASE, qp->sq.wq.phase);
	EFA_SET(&desc->ctrl2, EFA_IO_TX_META_DESC_FIRST, 1);
	EFA_SET(&desc->ctrl2, EFA_IO_TX_META_DESC_LAST, 1);
	EFA_SET(&desc->ctrl2, EFA_IO_TX_META_DESC_COMP_REQ, 1);
}

static void efa_set_dest_info_ud(struct efa_io_tx_meta_desc *desc, const struct ib_send_wr *wr)
{
	const struct ib_ud_wr *udwr = ud_wr(wr);

	desc->dest_qp_num = (u16)udwr->remote_qpn;
	desc->ah = to_eah(udwr->ah)->ah;
	desc->qkey = udwr->remote_qkey;
}

static void efa_set_dest_info_srd(struct efa_io_tx_meta_desc *desc, const struct ib_send_wr *wr)
{
	const struct ib_srd_wr *srdwr = srd_wr(wr);

	desc->dest_qp_num = (u16)srdwr->remote_qpn;
	desc->ah = to_eah(srdwr->ah)->ah;
	desc->qkey = srdwr->remote_qkey;
}

static void efa_set_imm_data(struct efa_io_tx_meta_desc *desc, const struct ib_send_wr *wr)
{
	desc->immediate_data = be32_to_cpu(wr->ex.imm_data);
	EFA_SET(&desc->ctrl1, EFA_IO_TX_META_DESC_HAS_IMM, 1);
}

static inline void efa_rq_ring_doorbell(struct efa_rq *rq, u16 pc)
{
	writel(pc, rq->wq.db);
}

static inline void efa_sq_ring_doorbell(struct efa_sq *sq, u16 pc)
{
	writel(pc, sq->wq.db);
}

static inline void efa_update_cq_doorbell(struct efa_cq *cq, bool arm)
{
	u32 db = 0;

	EFA_SET(&db, EFA_IO_REGS_CQ_DB_CONSUMER_INDEX, cq->cc);
	EFA_SET(&db, EFA_IO_REGS_CQ_DB_CMD_SN, cq->cmd_sn & 0x3);
	EFA_SET(&db, EFA_IO_REGS_CQ_DB_ARM, arm);

	writel(db, cq->db);
}

static u32 efa_wq_get_next_wrid_idx(struct efa_wq *wq, u64 wr_id)
{
	u32 wrid_idx;

	/* Get the next wrid to be used from the index pool */
	wrid_idx = wq->wrid_idx_pool[wq->wrid_idx_pool_next];
	wq->wrid[wrid_idx] = wr_id;

	/* Will never overlap, as validate function succeeded */
	wq->wrid_idx_pool_next++;

	return wrid_idx;
}

static void efa_sq_advance_post_idx(struct efa_qp *qp)
{
	qp->sq.wq.wqes_posted++;
	qp->sq.wq.pc++;

	if (!(qp->sq.wq.pc & qp->sq.wq.queue_mask))
		qp->sq.wq.phase++;
}

#ifdef HAVE_POST_CONST_WR
int efa_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr)
#else
int efa_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr)
#endif
{
	struct efa_qp *qp = to_eqp(ibqp);
	u32 current_batch = 0;
	int err = 0;

	spin_lock(&qp->sq.wq.lock);
	while (wr) {
		struct efa_io_tx_meta_desc *meta_desc;
		enum efa_io_send_op_type opcode;
		struct efa_io_tx_wqe tx_wqe;
		u32 sq_desc_offset;

		err = efa_post_send_validate(qp, wr);
		if (err) {
			*bad_wr = wr;
			goto ring_db;
		}

		memset(&tx_wqe, 0, sizeof(tx_wqe));
		meta_desc = &tx_wqe.meta;

		switch (wr->opcode) {
		case IB_WR_RDMA_READ:
			opcode = EFA_IO_RDMA_READ;
			efa_wqe_set_rdma_info(&tx_wqe, wr);
			efa_set_dest_info_srd(meta_desc, wr);
			break;
		case IB_WR_RDMA_WRITE_WITH_IMM:
			efa_set_imm_data(meta_desc, wr);
			fallthrough;
		case IB_WR_RDMA_WRITE:
			opcode = EFA_IO_RDMA_WRITE;
			efa_wqe_set_rdma_info(&tx_wqe, wr);
			efa_set_dest_info_srd(meta_desc, wr);
			break;
		case IB_WR_SEND_WITH_IMM:
			efa_set_imm_data(meta_desc, wr);
			fallthrough;
		case IB_WR_SEND:
			opcode = EFA_IO_SEND;
			if (wr->send_flags & IB_SEND_INLINE)
				efa_wqe_set_send_inline_data(&tx_wqe, wr);
			else
				efa_wqe_set_send_sgl(&tx_wqe, wr);

			if (ibqp->qp_type == IB_QPT_UD)
				efa_set_dest_info_ud(meta_desc, wr);
			else // EFA_QPT_SRD
				efa_set_dest_info_srd(meta_desc, wr);

			break;
		case IB_WR_REG_MR:
			opcode = EFA_IO_FAST_REG;
			efa_wqe_set_fast_reg_info(&tx_wqe, wr);
			break;
		case IB_WR_LOCAL_INV:
			opcode = EFA_IO_FAST_INV;
			efa_wqe_set_fast_inv_info(&tx_wqe, wr);
			break;
		default:
			err = -EINVAL;
			*bad_wr = wr;
			goto ring_db;
		}

		/* Set rest of the descriptor fields */
		efa_set_common_ctrl_flags(meta_desc, qp, opcode);
		meta_desc->req_id = efa_wq_get_next_wrid_idx(&qp->sq.wq, wr->wr_id);

		/* Copy descriptor */
		sq_desc_offset = (qp->sq.wq.pc & qp->sq.wq.queue_mask) * sizeof(tx_wqe);
		__iowrite64_copy(qp->sq.desc + sq_desc_offset, &tx_wqe, sizeof(tx_wqe) / 8);

		/* advance index and change phase */
		efa_sq_advance_post_idx(qp);

		current_batch++;
		if (current_batch == qp->sq.max_batch_wr) {
			wmb();
			efa_sq_ring_doorbell(&qp->sq, qp->sq.wq.pc);
			current_batch = 0;
			wmb();
		}

		wr = wr->next;
	}

ring_db:
	if (current_batch) {
		wmb();
		efa_sq_ring_doorbell(&qp->sq, qp->sq.wq.pc);
	}

	spin_unlock(&qp->sq.wq.lock);

	return err;
}

static int efa_post_recv_validate(struct efa_qp *qp, const struct ib_recv_wr *wr)
{
	if (unlikely(qp->state == IB_QPS_RESET || qp->state == IB_QPS_ERR))
		return -EINVAL;

	if (unlikely(qp->rq.wq.wqes_posted - qp->rq.wq.wqes_completed == qp->rq.wq.max_wqes))
		return -ENOMEM;

	if (unlikely(wr->num_sge > qp->rq.wq.max_sge))
		return -EINVAL;

	return 0;
}

#ifdef HAVE_POST_CONST_WR
int efa_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr)
#else
int efa_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr)
#endif
{
	struct efa_qp *qp = to_eqp(ibqp);
	struct efa_io_rx_desc rx_buf;
	u32 rq_desc_offset;
	int err = 0;
	u64 addr;
	size_t i;

	spin_lock(&qp->rq.wq.lock);
	while (wr) {
		err = efa_post_recv_validate(qp, wr);
		if (err) {
			*bad_wr = wr;
			goto ring_db;
		}

		memset(&rx_buf, 0, sizeof(rx_buf));

		rx_buf.req_id = efa_wq_get_next_wrid_idx(&qp->rq.wq, wr->wr_id);
		qp->rq.wq.wqes_posted++;

		/* Default init of the rx buffer */
		EFA_SET(&rx_buf.lkey_ctrl, EFA_IO_RX_DESC_FIRST, 1);
		EFA_SET(&rx_buf.lkey_ctrl, EFA_IO_RX_DESC_LAST, 0);

		for (i = 0; i < wr->num_sge; i++) {
			/* Set last indication if need) */
			if (i == wr->num_sge - 1)
				EFA_SET(&rx_buf.lkey_ctrl, EFA_IO_RX_DESC_LAST, 1);

			addr = wr->sg_list[i].addr;

			/* Set RX buffer desc from SGE */
			rx_buf.length = wr->sg_list[i].length;
			EFA_SET(&rx_buf.lkey_ctrl, EFA_IO_RX_DESC_LKEY, wr->sg_list[i].lkey);
			rx_buf.buf_addr_lo = addr;
			rx_buf.buf_addr_hi = addr >> 32;

			/* Copy descriptor to RX ring */
			rq_desc_offset = (qp->rq.wq.pc & qp->rq.wq.queue_mask) * sizeof(rx_buf);
			memcpy(qp->rq.buf + rq_desc_offset, &rx_buf, sizeof(rx_buf));

			/* Wrap rx descriptor index */
			qp->rq.wq.pc++;
			if (!(qp->rq.wq.pc & qp->rq.wq.queue_mask))
				qp->rq.wq.phase++;

			/* reset descriptor for next iov */
			memset(&rx_buf, 0, sizeof(rx_buf));
		}
		wr = wr->next;
	}

ring_db:
	dma_wmb();
	efa_rq_ring_doorbell(&qp->rq, qp->rq.wq.pc);

	spin_unlock(&qp->rq.wq.lock);
	return err;
}

static u32 efa_sub_cq_get_current_index(struct efa_sub_cq *sub_cq)
{
	return sub_cq->consumed_cnt & sub_cq->queue_mask;
}

static int efa_cqe_is_pending(struct efa_io_cdesc_common *cqe_common, int phase)
{
	return EFA_GET(&cqe_common->flags, EFA_IO_CDESC_COMMON_PHASE) == phase;
}

static struct efa_io_cdesc_common *
efa_sub_cq_get_cqe(struct efa_sub_cq *sub_cq, int entry)
{
	return (struct efa_io_cdesc_common *)(sub_cq->buf + (entry * sub_cq->cqe_size));
}

static struct efa_io_cdesc_common *
cq_next_sub_cqe_get(struct efa_sub_cq *sub_cq)
{
	struct efa_io_cdesc_common *cqe;
	u32 current_index;

	current_index = efa_sub_cq_get_current_index(sub_cq);
	cqe = efa_sub_cq_get_cqe(sub_cq, current_index);
	if (efa_cqe_is_pending(cqe, sub_cq->phase)) {
		/* Do not read the rest of the completion entry before the
		 * phase bit has been validated.
		 */
		dma_rmb();
		sub_cq->consumed_cnt++;
		if (!efa_sub_cq_get_current_index(sub_cq))
			sub_cq->phase = 1 - sub_cq->phase;
		return cqe;
	}

	return NULL;
}

static enum ib_wc_status to_ib_status(enum efa_io_comp_status status)
{
	switch (status) {
	case EFA_IO_COMP_STATUS_OK:
		return IB_WC_SUCCESS;
	case EFA_IO_COMP_STATUS_FLUSHED:
		return IB_WC_WR_FLUSH_ERR;
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_QP_INTERNAL_ERROR:
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_UNSUPPORTED_OP:
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_INVALID_AH:
		return IB_WC_LOC_QP_OP_ERR;
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_INVALID_LKEY:
		return IB_WC_LOC_PROT_ERR;
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_BAD_LENGTH:
		return IB_WC_LOC_LEN_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_ABORT:
		return IB_WC_REM_ABORT_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_RNR:
		return IB_WC_RNR_RETRY_EXC_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_DEST_QPN:
		return IB_WC_REM_INV_RD_REQ_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_STATUS:
		return IB_WC_BAD_RESP_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_LENGTH:
		return IB_WC_REM_INV_REQ_ERR;
	case EFA_IO_COMP_STATUS_LOCAL_ERROR_UNRESP_REMOTE:
		return IB_WC_RESP_TIMEOUT_ERR;
	case EFA_IO_COMP_STATUS_REMOTE_ERROR_BAD_ADDRESS:
		return IB_WC_REM_ACCESS_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
}

static int efa_poll_sub_cq(struct efa_cq *cq, struct efa_sub_cq *sub_cq,
			   struct efa_qp **cur_qp, struct ib_wc *wc)
{
	struct efa_dev *dev = to_edev(cq->ibcq.device);
	enum efa_io_send_op_type op_type;
	struct efa_io_cdesc_common *cqe;
	u32 qpn, wrid_idx;
	struct efa_wq *wq;

	cqe = cq_next_sub_cqe_get(sub_cq);
	if (!cqe)
		return 0;

	qpn = cqe->qp_num;
	if (!*cur_qp || qpn != (*cur_qp)->ibqp.qp_num) {
		/* We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		*cur_qp = dev->qp_table[qpn & dev->qp_table_mask];
		if (!*cur_qp || qpn != (*cur_qp)->ibqp.qp_num)
			return -EINVAL;
	}

	wrid_idx = cqe->req_id;
	wc->status = to_ib_status(cqe->status);
	wc->vendor_err = cqe->status;
	wc->wc_flags = 0;
	wc->qp = &(*cur_qp)->ibqp;

	op_type = EFA_GET(&cqe->flags, EFA_IO_CDESC_COMMON_OP_TYPE);

	if (EFA_GET(&cqe->flags, EFA_IO_CDESC_COMMON_Q_TYPE) == EFA_IO_SEND_QUEUE) {
		wq = &(*cur_qp)->sq.wq;
		switch (op_type) {
		case EFA_IO_RDMA_WRITE:
			wc->opcode = IB_WC_RDMA_WRITE;
			break;
		case EFA_IO_RDMA_READ:
			wc->opcode = IB_WC_RDMA_READ;
			break;
		case EFA_IO_FAST_REG:
			wc->opcode = IB_WC_REG_MR;
			break;
		case EFA_IO_FAST_INV:
			wc->opcode = IB_WC_LOCAL_INV;
			break;
		default:
			wc->opcode = IB_WC_SEND;
			break;
		}
	} else {
		struct efa_io_rx_cdesc_ex *rcqe = container_of(cqe, struct efa_io_rx_cdesc_ex,
							       base.common);

		wq = &(*cur_qp)->rq.wq;

		wc->byte_len = rcqe->base.length;

		if (op_type == EFA_IO_RDMA_WRITE) {
			wc->byte_len |= ((u32)rcqe->u.rdma_write.length_hi << 16);
			wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
		} else {
			wc->opcode = IB_WC_RECV;
		}

		wc->src_qp = rcqe->base.src_qp_num;
		wc->sl = 0;
		wc->slid = rcqe->base.ah;

		if (EFA_GET(&cqe->flags, EFA_IO_CDESC_COMMON_HAS_IMM)) {
			wc->ex.imm_data = cpu_to_be32(rcqe->base.imm);
			wc->wc_flags |= IB_WC_WITH_IMM;
		}
	}

	spin_lock(&wq->lock);
	wq->wrid_idx_pool_next--;
	wq->wrid_idx_pool[wq->wrid_idx_pool_next] = wrid_idx;
	wc->wr_id = wq->wrid[wrid_idx];
	wq->wqes_completed++;
	spin_unlock(&wq->lock);

	return 1;
}

static int efa_poll_sub_cqs(struct efa_cq *cq, struct ib_wc *wc)
{
	u16 num_sub_cqs = cq->num_sub_cqs;
	struct efa_sub_cq *sub_cq;
	struct efa_qp *qp = NULL;
	u16 sub_cq_idx;
	int ret = 0;

	for (sub_cq_idx = 0; sub_cq_idx < num_sub_cqs; sub_cq_idx++) {
		sub_cq = &cq->sub_cq_arr[cq->next_poll_idx++];
		cq->next_poll_idx %= num_sub_cqs;

		if (!sub_cq->ref_cnt)
			continue;

		ret = efa_poll_sub_cq(cq, sub_cq, &qp, wc);
		if (ret) {
			cq->cc++;
			break;
		}
	}

	return ret;
}

int efa_poll_cq(struct ib_cq *ibcq, int nwc, struct ib_wc *wc)
{
	struct efa_cq *cq = to_ecq(ibcq);
	int ret = 0;
	int i;

	spin_lock(&cq->lock);

	for (i = 0; i < nwc; i++) {
		ret = efa_poll_sub_cqs(cq, &wc[i]);
		if (ret <= 0)
			break;
	}

	spin_unlock(&cq->lock);

	return i ?: ret;
}

static bool efa_cq_empty(struct efa_cq *cq)
{
	u16 num_sub_cqs = cq->num_sub_cqs;
	struct efa_io_cdesc_common *cqe;
	struct efa_sub_cq *sub_cq;
	u32 current_index;
	u16 sub_cq_idx;

	spin_lock(&cq->lock);

	for (sub_cq_idx = 0; sub_cq_idx < num_sub_cqs; sub_cq_idx++) {
		sub_cq = &cq->sub_cq_arr[sub_cq_idx];

		if (!sub_cq->ref_cnt)
			continue;

		current_index = efa_sub_cq_get_current_index(sub_cq);
		cqe = efa_sub_cq_get_cqe(sub_cq, current_index);
		if (efa_cqe_is_pending(cqe, sub_cq->phase)) {
			spin_unlock(&cq->lock);
			return false;
		}
	}

	spin_unlock(&cq->lock);

	return true;
}

int efa_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct efa_cq *cq = to_ecq(ibcq);

	if (unlikely(flags & IB_CQ_SOLICITED))
		return -EOPNOTSUPP;

	efa_update_cq_doorbell(cq, true);

	if (flags & IB_CQ_REPORT_MISSED_EVENTS)
		return efa_cq_empty(cq) ? 0 : 1;

	return 0;
}

static int efa_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct efa_mr *mr = to_emr(ibmr);

	if (unlikely(mr->num_pages == mr->pages_list_length))
		return -ENOMEM;

	mr->pages_list[mr->num_pages++] = addr;

	return 0;
}

int efa_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		  unsigned int *sg_offset)
{
	struct efa_mr *mr = to_emr(ibmr);
	int mapped_sge;

	mr->num_pages = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, mr->pages_list_dma_addr,
				   mr->pages_list_length * sizeof(*mr->pages_list),
				   DMA_TO_DEVICE);

	mapped_sge = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, efa_set_page);

	ib_dma_sync_single_for_device(ibmr->device, mr->pages_list_dma_addr,
				      mr->pages_list_length * sizeof(*mr->pages_list),
				      DMA_TO_DEVICE);

	return mapped_sge;
}

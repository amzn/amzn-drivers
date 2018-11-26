// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#include "efa.h"
#include "efa_com.h"
#include "efa_regs_defs.h"

#define ADMIN_CMD_TIMEOUT_US 30000000 /* usecs */

#define EFA_REG_READ_TIMEOUT_US 50000 /* usecs */
#define EFA_MMIO_READ_INVALID 0xffffffff

#define EFA_POLL_INTERVAL_MS 100 /* msecs */

#define EFA_ASYNC_QUEUE_DEPTH 16
#define EFA_ADMIN_QUEUE_DEPTH 32

#define MIN_EFA_VER\
	((EFA_ADMIN_API_VERSION_MAJOR << EFA_REGS_VERSION_MAJOR_VERSION_SHIFT) | \
	 (EFA_ADMIN_API_VERSION_MINOR & EFA_REGS_VERSION_MINOR_VERSION_MASK))

#define EFA_CTRL_MAJOR          0
#define EFA_CTRL_MINOR          0
#define EFA_CTRL_SUB_MINOR      1

#define MIN_EFA_CTRL_VER \
	(((EFA_CTRL_MAJOR) << \
	(EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT)) | \
	((EFA_CTRL_MINOR) << \
	(EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT)) | \
	(EFA_CTRL_SUB_MINOR))

#define EFA_DMA_ADDR_TO_UINT32_LOW(x)   ((u32)((u64)(x)))
#define EFA_DMA_ADDR_TO_UINT32_HIGH(x)  ((u32)(((u64)(x)) >> 32))

#define EFA_REGS_ADMIN_INTR_MASK 1

enum efa_cmd_status {
	EFA_CMD_SUBMITTED,
	EFA_CMD_COMPLETED,
	/* Abort - canceled by the driver */
	EFA_CMD_ABORTED,
};

struct efa_comp_ctx {
	struct completion wait_event;
	struct efa_admin_acq_entry *user_cqe;
	u32 comp_size;
	enum efa_cmd_status status;
	/* status from the device */
	u8 comp_status;
	u8 cmd_opcode;
	bool occupied;
};

static u32 efa_com_reg_read32(struct efa_com_dev *edev, u16 offset)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;
	struct efa_admin_mmio_req_read_less_resp *read_resp;
	unsigned long exp_time;
	u32 mmio_read_reg;
	u32 err;

	read_resp = mmio_read->read_resp;

	spin_lock(&mmio_read->lock);
	mmio_read->seq_num++;

	/* trash DMA req_id to identify when hardware is done */
	read_resp->req_id = mmio_read->seq_num + 0x9aL;
	mmio_read_reg  = (offset << EFA_REGS_MMIO_REG_READ_REG_OFF_SHIFT) &
			 EFA_REGS_MMIO_REG_READ_REG_OFF_MASK;
	mmio_read_reg |= mmio_read->seq_num &
			 EFA_REGS_MMIO_REG_READ_REQ_ID_MASK;

	writel(mmio_read_reg, edev->reg_bar + EFA_REGS_MMIO_REG_READ_OFF);

	exp_time = jiffies + usecs_to_jiffies(mmio_read->mmio_read_timeout);
	do {
		if (READ_ONCE(read_resp->req_id) == mmio_read->seq_num)
			break;
		udelay(1);
	} while (time_is_after_jiffies(exp_time));

	if (unlikely(read_resp->req_id != mmio_read->seq_num)) {
		pr_err("Reading register timed out. expected: req id[%u] offset[%#x] actual: req id[%u] offset[%#x]\n",
		       mmio_read->seq_num, offset, read_resp->req_id,
		       read_resp->reg_off);
		err = EFA_MMIO_READ_INVALID;
		goto out;
	}

	if (read_resp->reg_off != offset) {
		pr_err("Reading register failed: wrong offset provided");
		err = EFA_MMIO_READ_INVALID;
		goto out;
	}

	err = read_resp->reg_val;
out:
	spin_unlock(&mmio_read->lock);
	return err;
}

static int efa_com_admin_init_sq(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *queue = &edev->admin_queue;
	struct efa_com_admin_sq *sq = &queue->sq;
	u16 size = ADMIN_SQ_SIZE(queue->depth);
	u32 addr_high;
	u32 addr_low;
	u32 aq_caps;

	sq->entries = dma_zalloc_coherent(queue->dmadev, size, &sq->dma_addr,
					  GFP_KERNEL);
	if (!sq->entries)
		return -ENOMEM;

	spin_lock_init(&sq->lock);

	sq->cc = 0;
	sq->pc = 0;
	sq->phase = 1;

	sq->db_addr = (u32 __iomem *)(edev->reg_bar + EFA_REGS_AQ_PROD_DB_OFF);

	pr_debug("init sq dma_addr....\n");
	addr_high = EFA_DMA_ADDR_TO_UINT32_HIGH(sq->dma_addr);
	addr_low  = EFA_DMA_ADDR_TO_UINT32_LOW(sq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_AQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_AQ_BASE_HI_OFF);

	aq_caps = 0;
	aq_caps |= queue->depth & EFA_REGS_AQ_CAPS_AQ_DEPTH_MASK;
	aq_caps |= (sizeof(struct efa_admin_aq_entry) <<
			EFA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_SHIFT) &
			EFA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_MASK;

	writel(aq_caps, edev->reg_bar + EFA_REGS_AQ_CAPS_OFF);

	return 0;
}

static int efa_com_admin_init_cq(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *queue = &edev->admin_queue;
	struct efa_com_admin_cq *cq = &queue->cq;
	u16 size = ADMIN_CQ_SIZE(queue->depth);
	u32 addr_high;
	u32 addr_low;
	u32 acq_caps;

	cq->entries = dma_zalloc_coherent(queue->dmadev, size, &cq->dma_addr,
					  GFP_KERNEL);
	if (!cq->entries)
		return -ENOMEM;

	spin_lock_init(&cq->lock);

	cq->cc = 0;
	cq->phase = 1;

	pr_debug("init cq dma_addr....\n");
	addr_high = EFA_DMA_ADDR_TO_UINT32_HIGH(cq->dma_addr);
	addr_low  = EFA_DMA_ADDR_TO_UINT32_LOW(cq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_ACQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_ACQ_BASE_HI_OFF);

	acq_caps = 0;
	acq_caps |= queue->depth & EFA_REGS_ACQ_CAPS_ACQ_DEPTH_MASK;
	acq_caps |= (sizeof(struct efa_admin_acq_entry) <<
			EFA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_SHIFT) &
			EFA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_MASK;
	acq_caps |= (queue->msix_vector_idx <<
			EFA_REGS_ACQ_CAPS_ACQ_MSIX_VECTOR_SHIFT) &
			EFA_REGS_ACQ_CAPS_ACQ_MSIX_VECTOR_MASK;

	writel(acq_caps, edev->reg_bar + EFA_REGS_ACQ_CAPS_OFF);

	return 0;
}

static int efa_com_admin_init_aenq(struct efa_com_dev *edev,
				   struct efa_aenq_handlers *aenq_handlers)
{
	struct efa_com_aenq *aenq = &edev->aenq;
	u32 addr_low, addr_high, aenq_caps;
	u16 size;

	pr_debug("init aenq...\n");

	if (unlikely(!aenq_handlers)) {
		pr_err("aenq handlers pointer is NULL\n");
		return -EINVAL;
	}

	size = ADMIN_AENQ_SIZE(EFA_ASYNC_QUEUE_DEPTH);
	aenq->entries = dma_zalloc_coherent(edev->dmadev, size, &aenq->dma_addr,
					    GFP_KERNEL);
	if (!aenq->entries)
		return -ENOMEM;

	aenq->aenq_handlers = aenq_handlers;
	aenq->depth = EFA_ASYNC_QUEUE_DEPTH;
	aenq->cc = 0;
	aenq->phase = 1;

	addr_low = EFA_DMA_ADDR_TO_UINT32_LOW(aenq->dma_addr);
	addr_high = EFA_DMA_ADDR_TO_UINT32_HIGH(aenq->dma_addr);

	writel(addr_low, edev->reg_bar + EFA_REGS_AENQ_BASE_LO_OFF);
	writel(addr_high, edev->reg_bar + EFA_REGS_AENQ_BASE_HI_OFF);

	aenq_caps = 0;
	aenq_caps |= aenq->depth & EFA_REGS_AENQ_CAPS_AENQ_DEPTH_MASK;
	aenq_caps |= (sizeof(struct efa_admin_aenq_entry) <<
		EFA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_SHIFT) &
		EFA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_MASK;
	aenq_caps |= (aenq->msix_vector_idx
		      << EFA_REGS_AENQ_CAPS_AENQ_MSIX_VECTOR_SHIFT) &
		     EFA_REGS_AENQ_CAPS_AENQ_MSIX_VECTOR_MASK;
	writel(aenq_caps, edev->reg_bar + EFA_REGS_AENQ_CAPS_OFF);

	/*
	 * Init cons_db to mark that all entries in the queue
	 * are initially available
	 */
	writel(edev->aenq.cc, edev->reg_bar + EFA_REGS_AENQ_CONS_DB_OFF);

	pr_debug("init aenq succeeded\n");

	return 0;
}

/* ID to be used with efa_com_get_comp_ctx */
static u16 efa_com_alloc_ctx_id(struct efa_com_admin_queue *admin_queue)
{
	u16 ctx_id;

	spin_lock(&admin_queue->comp_ctx_lock);
	ctx_id = admin_queue->comp_ctx_pool[admin_queue->comp_ctx_pool_next];
	admin_queue->comp_ctx_pool_next++;
	spin_unlock(&admin_queue->comp_ctx_lock);

	return ctx_id;
}

static inline void efa_com_put_comp_ctx(struct efa_com_admin_queue *queue,
					struct efa_comp_ctx *comp_ctx)
{
	u16 comp_id = comp_ctx->user_cqe->acq_common_descriptor.command &
		      EFA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK;

	pr_debug("Putting completion command_id %d", comp_id);
	comp_ctx->occupied = false;
	spin_lock(&queue->comp_ctx_lock);
	queue->comp_ctx_pool_next--;
	queue->comp_ctx_pool[queue->comp_ctx_pool_next] = comp_id;
	spin_unlock(&queue->comp_ctx_lock);
	up(&queue->avail_cmds);
}

static struct efa_comp_ctx *efa_com_get_comp_ctx(struct efa_com_admin_queue *queue,
						 u16 command_id, bool capture)
{
	if (unlikely(command_id >= queue->depth)) {
		pr_err("command id is larger than the queue size. cmd_id: %u queue size %d\n",
		       command_id, queue->depth);
		return NULL;
	}

	if (unlikely(queue->comp_ctx[command_id].occupied && capture)) {
		pr_err("Completion context is occupied\n");
		return NULL;
	}

	if (capture) {
		queue->comp_ctx[command_id].occupied = true;
		pr_debug("Taking completion ctxt command_id %d", command_id);
	}

	return &queue->comp_ctx[command_id];
}

static struct efa_comp_ctx *__efa_com_submit_admin_cmd(struct efa_com_admin_queue *admin_queue,
						       struct efa_admin_aq_entry *cmd,
						       size_t cmd_size_in_bytes,
						       struct efa_admin_acq_entry *comp,
						       size_t comp_size_in_bytes)
{
	struct efa_comp_ctx *comp_ctx;
	u16 queue_size_mask;
	u16 ctx_id;
	u16 pi;

	queue_size_mask = admin_queue->depth - 1;
	pi = admin_queue->sq.pc & queue_size_mask;

	ctx_id = efa_com_alloc_ctx_id(admin_queue);

	cmd->aq_common_descriptor.flags |= admin_queue->sq.phase &
		EFA_ADMIN_AQ_COMMON_DESC_PHASE_MASK;

	cmd->aq_common_descriptor.command_id |= ctx_id &
		EFA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = efa_com_get_comp_ctx(admin_queue, ctx_id, true);
	if (unlikely(!comp_ctx))
		return ERR_PTR(-EINVAL);

	comp_ctx->status = EFA_CMD_SUBMITTED;
	comp_ctx->comp_size = comp_size_in_bytes;
	comp_ctx->user_cqe = comp;
	comp_ctx->cmd_opcode = cmd->aq_common_descriptor.opcode;

	reinit_completion(&comp_ctx->wait_event);

	memcpy(&admin_queue->sq.entries[pi], cmd, cmd_size_in_bytes);

	admin_queue->sq.pc++;
	admin_queue->stats.submitted_cmd++;

	if (unlikely((admin_queue->sq.pc & queue_size_mask) == 0))
		admin_queue->sq.phase = !admin_queue->sq.phase;

	/* barrier not needed in case of writel */
	writel(admin_queue->sq.pc, admin_queue->sq.db_addr);

	return comp_ctx;
}

static inline int efa_com_init_comp_ctxt(struct efa_com_admin_queue *queue)
{
	size_t pool_size = queue->depth * sizeof(*queue->comp_ctx_pool);
	size_t size = queue->depth * sizeof(struct efa_comp_ctx);
	struct efa_comp_ctx *comp_ctx;
	u16 i;

	queue->comp_ctx = devm_kzalloc(queue->dmadev, size, GFP_KERNEL);
	queue->comp_ctx_pool =
		devm_kzalloc(queue->dmadev, pool_size, GFP_KERNEL);
	if (unlikely(!queue->comp_ctx || !queue->comp_ctx_pool)) {
		devm_kfree(queue->dmadev, queue->comp_ctx_pool);
		devm_kfree(queue->dmadev, queue->comp_ctx);
		return -ENOMEM;
	}

	for (i = 0; i < queue->depth; i++) {
		comp_ctx = efa_com_get_comp_ctx(queue, i, false);
		if (comp_ctx)
			init_completion(&comp_ctx->wait_event);

		queue->comp_ctx_pool[i] = i;
	}

	spin_lock_init(&queue->comp_ctx_lock);

	queue->comp_ctx_pool_next = 0;

	return 0;
}

static struct efa_comp_ctx *efa_com_submit_admin_cmd(struct efa_com_admin_queue *admin_queue,
						     struct efa_admin_aq_entry *cmd,
						     size_t cmd_size_in_bytes,
						     struct efa_admin_acq_entry *comp,
						     size_t comp_size_in_bytes)
{
	struct efa_comp_ctx *comp_ctx;
	int err;

	/* In case of queue FULL */
	err = down_interruptible(&admin_queue->avail_cmds);
	if (err) {
		pr_warn("Waiting for admin queue interrupted\n");
		return ERR_PTR(err);
	}

	spin_lock(&admin_queue->sq.lock);
	if (unlikely(!test_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state))) {
		pr_err("Admin queue is closed\n");
		spin_unlock(&admin_queue->sq.lock);
		return ERR_PTR(-ENODEV);
	}

	comp_ctx =
		__efa_com_submit_admin_cmd(admin_queue, cmd, cmd_size_in_bytes,
					   comp, comp_size_in_bytes);
	spin_unlock(&admin_queue->sq.lock);
	if (unlikely(IS_ERR(comp_ctx)))
		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);

	return comp_ctx;
}

static void efa_com_handle_single_admin_completion(struct efa_com_admin_queue *admin_queue,
						   struct efa_admin_acq_entry *cqe)
{
	struct efa_comp_ctx *comp_ctx;
	u16 cmd_id;

	cmd_id = cqe->acq_common_descriptor.command &
		 EFA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = efa_com_get_comp_ctx(admin_queue, cmd_id, false);
	if (unlikely(!comp_ctx)) {
		pr_err("comp_ctx is NULL. Changing the admin queue running state\n");
		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);
		return;
	}

	comp_ctx->status = EFA_CMD_COMPLETED;
	comp_ctx->comp_status = cqe->acq_common_descriptor.status;
	if (comp_ctx->user_cqe)
		memcpy(comp_ctx->user_cqe, cqe, comp_ctx->comp_size);

	if (!admin_queue->polling)
		complete(&comp_ctx->wait_event);
}

static void efa_com_handle_admin_completion(struct efa_com_admin_queue *admin_queue)
{
	struct efa_admin_acq_entry *cqe;
	u16 queue_size_mask;
	u16 comp_num = 0;
	u8 phase;
	u16 ci;

	queue_size_mask = admin_queue->depth - 1;

	ci = admin_queue->cq.cc & queue_size_mask;
	phase = admin_queue->cq.phase;

	cqe = &admin_queue->cq.entries[ci];

	/* Go over all the completions */
	while ((READ_ONCE(cqe->acq_common_descriptor.flags) &
		EFA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK) == phase) {
		/*
		 * Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		dma_rmb();
		efa_com_handle_single_admin_completion(admin_queue, cqe);

		ci++;
		comp_num++;
		if (unlikely(ci == admin_queue->depth)) {
			ci = 0;
			phase = !phase;
		}

		cqe = &admin_queue->cq.entries[ci];
	}

	admin_queue->cq.cc += comp_num;
	admin_queue->cq.phase = phase;
	admin_queue->sq.cc += comp_num;
	admin_queue->stats.completed_cmd += comp_num;
}

static int efa_com_comp_status_to_errno(u8 comp_status)
{
	if (unlikely(comp_status))
		pr_err("admin command failed[%u]\n", comp_status);

	switch (comp_status) {
	case EFA_ADMIN_SUCCESS:
		return 0;
	case EFA_ADMIN_RESOURCE_ALLOCATION_FAILURE:
		return -ENOMEM;
	case EFA_ADMIN_UNSUPPORTED_OPCODE:
		return -EOPNOTSUPP;
	case EFA_ADMIN_BAD_OPCODE:
	case EFA_ADMIN_MALFORMED_REQUEST:
	case EFA_ADMIN_ILLEGAL_PARAMETER:
	case EFA_ADMIN_UNKNOWN_ERROR:
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int efa_com_wait_and_process_admin_cq_polling(struct efa_comp_ctx *comp_ctx,
						     struct efa_com_admin_queue *admin_queue)
{
	unsigned long timeout;
	unsigned long flags;
	int err;

	timeout = jiffies + usecs_to_jiffies(admin_queue->completion_timeout);

	while (1) {
		spin_lock_irqsave(&admin_queue->cq.lock, flags);
		efa_com_handle_admin_completion(admin_queue);
		spin_unlock_irqrestore(&admin_queue->cq.lock, flags);

		if (comp_ctx->status != EFA_CMD_SUBMITTED)
			break;

		if (time_is_before_jiffies(timeout)) {
			pr_err("Wait for completion (polling) timeout\n");
			/* EFA didn't have any completion */
			admin_queue->stats.no_completion++;

			clear_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);
			err = -ETIME;
			goto out;
		}

		msleep(admin_queue->poll_interval);
	}

	if (unlikely(comp_ctx->status == EFA_CMD_ABORTED)) {
		pr_err("Command was aborted\n");
		admin_queue->stats.aborted_cmd++;
		err = -ENODEV;
		goto out;
	}

	WARN(comp_ctx->status != EFA_CMD_COMPLETED, "Invalid comp status %d\n",
	     comp_ctx->status);

	err = efa_com_comp_status_to_errno(comp_ctx->comp_status);
out:
	efa_com_put_comp_ctx(admin_queue, comp_ctx);
	return err;
}

static int efa_com_wait_and_process_admin_cq_interrupts(struct efa_comp_ctx *comp_ctx,
							struct efa_com_admin_queue *admin_queue)
{
	unsigned long flags;
	int err;

	wait_for_completion_timeout(&comp_ctx->wait_event,
				    usecs_to_jiffies(
					    admin_queue->completion_timeout));

	/*
	 * In case the command wasn't completed find out the root cause.
	 * There might be 2 kinds of errors
	 * 1) No completion (timeout reached)
	 * 2) There is completion but the device didn't get any msi-x interrupt.
	 */
	if (unlikely(comp_ctx->status == EFA_CMD_SUBMITTED)) {
		spin_lock_irqsave(&admin_queue->cq.lock, flags);
		efa_com_handle_admin_completion(admin_queue);
		spin_unlock_irqrestore(&admin_queue->cq.lock, flags);

		admin_queue->stats.no_completion++;

		if (comp_ctx->status == EFA_CMD_COMPLETED)
			pr_err("The device sent a completion but the driver didn't receive any MSI-X interrupt for admin cmd %d status %d (ctx: 0x%p, sq producer: %d, sq consumer: %d, cq consumer: %d)\n",
			       comp_ctx->cmd_opcode, comp_ctx->status, comp_ctx,
			       admin_queue->sq.pc, admin_queue->sq.cc,
			       admin_queue->cq.cc);
		else
			pr_err("The device didn't send any completion for admin cmd %d status %d (ctx 0x%p, sq producer: %d, sq consumer: %d, cq consumer: %d)\n",
			       comp_ctx->cmd_opcode, comp_ctx->status, comp_ctx,
			       admin_queue->sq.pc, admin_queue->sq.cc,
			       admin_queue->cq.cc);

		clear_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);
		err = -ETIME;
		goto out;
	}

	err = efa_com_comp_status_to_errno(comp_ctx->comp_status);
out:
	efa_com_put_comp_ctx(admin_queue, comp_ctx);
	return err;
}

/*
 * There are two types to wait for completion.
 * Polling mode - wait until the completion is available.
 * Async mode - wait on wait queue until the completion is ready
 * (or the timeout expired).
 * It is expected that the IRQ called efa_com_handle_admin_completion
 * to mark the completions.
 */
static int efa_com_wait_and_process_admin_cq(struct efa_comp_ctx *comp_ctx,
					     struct efa_com_admin_queue *admin_queue)
{
	if (admin_queue->polling)
		return efa_com_wait_and_process_admin_cq_polling(comp_ctx,
								 admin_queue);

	return efa_com_wait_and_process_admin_cq_interrupts(comp_ctx,
							    admin_queue);
}

/*
 * efa_com_cmd_exec - Execute admin command
 * @admin_queue: admin queue.
 * @cmd: the admin command to execute.
 * @cmd_size: the command size.
 * @comp: command completion return entry.
 * @comp_size: command completion size.
 * Submit an admin command and then wait until the device will return a
 * completion.
 * The completion will be copied into comp.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_cmd_exec(struct efa_com_admin_queue *admin_queue,
		     struct efa_admin_aq_entry *cmd,
		     size_t cmd_size,
		     struct efa_admin_acq_entry *comp,
		     size_t comp_size)
{
	struct efa_comp_ctx *comp_ctx;
	int err;

	pr_debug("opcode %d", cmd->aq_common_descriptor.opcode);
	comp_ctx = efa_com_submit_admin_cmd(admin_queue, cmd, cmd_size, comp,
					    comp_size);
	if (unlikely(IS_ERR(comp_ctx))) {
		pr_err("Failed to submit command opcode %u err %ld\n",
		       cmd->aq_common_descriptor.opcode, PTR_ERR(comp_ctx));

		return PTR_ERR(comp_ctx);
	}

	err = efa_com_wait_and_process_admin_cq(comp_ctx, admin_queue);
	if (unlikely(err))
		pr_err("Failed to process command opcode %u err %d\n",
		       cmd->aq_common_descriptor.opcode, err);

	return err;
}

/*
 * efa_com_abort_admin_commands - Abort all the outstanding admin commands.
 * @edev: EFA communication layer struct
 *
 * This method aborts all the outstanding admin commands.
 * The caller should then call efa_com_wait_for_abort_completion to make sure
 * all the commands were completed.
 */
static void efa_com_abort_admin_commands(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_comp_ctx *comp_ctx;
	unsigned long flags;
	u16 i;

	spin_lock(&admin_queue->sq.lock);
	spin_lock_irqsave(&admin_queue->cq.lock, flags);
	for (i = 0; i < admin_queue->depth; i++) {
		comp_ctx = efa_com_get_comp_ctx(admin_queue, i, false);
		if (unlikely(!comp_ctx))
			break;

		comp_ctx->status = EFA_CMD_ABORTED;

		complete(&comp_ctx->wait_event);
	}
	spin_unlock_irqrestore(&admin_queue->cq.lock, flags);
	spin_unlock(&admin_queue->sq.lock);
}

/*
 * efa_com_wait_for_abort_completion - Wait for admin commands abort.
 * @edev: EFA communication layer struct
 *
 * This method wait until all the outstanding admin commands will be completed.
 */
static void efa_com_wait_for_abort_completion(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	int i;

	/* all mine */
	for (i = 0; i < admin_queue->depth; i++) {
		if (down_interruptible(&admin_queue->avail_cmds))
			pr_warn("Interrupted while waiting for admin abort[%d]\n",
				i);
	}

	/* let it go */
	for (i = 0; i < admin_queue->depth; i++)
		up(&admin_queue->avail_cmds);
}

static void efa_com_admin_flush(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;

	clear_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);

	efa_com_abort_admin_commands(edev);
	efa_com_wait_for_abort_completion(edev);
}

/*
 * efa_com_admin_destroy - Destroy the admin and the async events queues.
 * @edev: EFA communication layer struct
 */
void efa_com_admin_destroy(struct efa_com_dev *edev)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_com_admin_cq *cq = &admin_queue->cq;
	struct efa_com_admin_sq *sq = &admin_queue->sq;
	struct efa_com_aenq *aenq = &edev->aenq;
	u16 size;

	efa_com_admin_flush(edev);

	devm_kfree(edev->dmadev, admin_queue->comp_ctx_pool);
	devm_kfree(edev->dmadev, admin_queue->comp_ctx);

	size = ADMIN_SQ_SIZE(admin_queue->depth);
	dma_free_coherent(edev->dmadev, size, sq->entries, sq->dma_addr);

	pr_debug("destroyed SQ");
	size = ADMIN_CQ_SIZE(admin_queue->depth);
	dma_free_coherent(edev->dmadev, size, cq->entries, cq->dma_addr);
	pr_debug("destroyed CQ");

	size = ADMIN_AENQ_SIZE(aenq->depth);
	dma_free_coherent(edev->dmadev, size, aenq->entries, aenq->dma_addr);
	pr_debug("destroyed AENQ");
}

/*
 * efa_com_set_admin_polling_mode - Set the admin completion queue polling mode
 * @edev: EFA communication layer struct
 * @polling: Enable/Disable polling mode
 *
 * Set the admin completion mode.
 */
void efa_com_set_admin_polling_mode(struct efa_com_dev *edev, bool polling)
{
	u32 mask_value = 0;

	if (polling)
		mask_value = EFA_REGS_ADMIN_INTR_MASK;

	writel(mask_value, edev->reg_bar + EFA_REGS_INTR_MASK_OFF);
	edev->admin_queue.polling = polling;
}

/*
 * efa_com_admin_init - Init the admin and the async queues
 * @edev: EFA communication layer struct
 * @aenq_handlers: Those handlers to be called upon event.
 *
 * Initialize the admin submission and completion queues.
 * Initialize the asynchronous events notification queues.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_admin_init(struct efa_com_dev *edev,
		       struct efa_aenq_handlers *aenq_handlers)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	u32 timeout;
	u32 dev_sts;
	u32 cap;
	int err;

	dev_sts = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);
	if (!(dev_sts & EFA_REGS_DEV_STS_READY_MASK)) {
		pr_err("Device isn't ready, abort com init 0x%08x\n", dev_sts);
		return -ENODEV;
	}

	admin_queue->depth = EFA_ADMIN_QUEUE_DEPTH;

	admin_queue->bus = edev->bus;
	admin_queue->dmadev = edev->dmadev;
	admin_queue->polling = true;

	sema_init(&admin_queue->avail_cmds, admin_queue->depth);

	err = efa_com_init_comp_ctxt(admin_queue);
	if (err)
		return err;

	err = efa_com_admin_init_sq(edev);
	if (err)
		goto err_destroy_comp_ctxt;

	err = efa_com_admin_init_cq(edev);
	if (err)
		goto err_destroy_sq;

	efa_com_set_admin_polling_mode(edev, false);

	err = efa_com_admin_init_aenq(edev, aenq_handlers);
	if (err)
		goto err_destroy_cq;

	cap = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);
	timeout = (cap & EFA_REGS_CAPS_ADMIN_CMD_TO_MASK) >>
		  EFA_REGS_CAPS_ADMIN_CMD_TO_SHIFT;
	if (timeout)
		/* the resolution of timeout reg is 100ms */
		admin_queue->completion_timeout = timeout * 100000;
	else
		admin_queue->completion_timeout = ADMIN_CMD_TIMEOUT_US;

	admin_queue->poll_interval = EFA_POLL_INTERVAL_MS;

	set_bit(EFA_AQ_STATE_RUNNING_BIT, &admin_queue->state);

	return 0;

err_destroy_cq:
	dma_free_coherent(edev->dmadev, ADMIN_CQ_SIZE(admin_queue->depth),
			  admin_queue->cq.entries, admin_queue->cq.dma_addr);
err_destroy_sq:
	dma_free_coherent(edev->dmadev, ADMIN_SQ_SIZE(admin_queue->depth),
			  admin_queue->sq.entries, admin_queue->sq.dma_addr);
err_destroy_comp_ctxt:
	devm_kfree(edev->dmadev, admin_queue->comp_ctx);

	return err;
}

/*
 * efa_com_admin_q_comp_intr_handler - admin queue interrupt handler
 * @edev: EFA communication layer struct
 *
 * This method go over the admin completion queue and wake up all the pending
 * threads that wait on the commands wait event.
 *
 * @note: Should be called after MSI-X interrupt.
 */
void efa_com_admin_q_comp_intr_handler(struct efa_com_dev *edev)
{
	unsigned long flags;

	spin_lock_irqsave(&edev->admin_queue.cq.lock, flags);
	efa_com_handle_admin_completion(&edev->admin_queue);
	spin_unlock_irqrestore(&edev->admin_queue.cq.lock, flags);
}

/*
 * efa_handle_specific_aenq_event:
 * return the handler that is relevant to the specific event group
 */
static efa_aenq_handler efa_com_get_specific_aenq_cb(struct efa_com_dev *edev,
						     u16 group)
{
	struct efa_aenq_handlers *aenq_handlers = edev->aenq.aenq_handlers;

	if (group < EFA_MAX_HANDLERS && aenq_handlers->handlers[group])
		return aenq_handlers->handlers[group];

	return aenq_handlers->unimplemented_handler;
}

/*
 * efa_com_aenq_intr_handler - AENQ interrupt handler
 * @edev: EFA communication layer struct
 *
 * Go over the async event notification queue and call the proper aenq handler.
 */
void efa_com_aenq_intr_handler(struct efa_com_dev *edev, void *data)
{
	struct efa_admin_aenq_common_desc *aenq_common;
	struct efa_com_aenq *aenq  = &edev->aenq;
	struct efa_admin_aenq_entry *aenq_e;
	efa_aenq_handler handler_cb;
	u32 processed = 0;
	u8 phase;
	u32 ci;

	ci = aenq->cc & (aenq->depth - 1);
	phase = aenq->phase;
	aenq_e = &aenq->entries[ci]; /* Get first entry */
	aenq_common = &aenq_e->aenq_common_desc;

	/* Go over all the events */
	while ((READ_ONCE(aenq_common->flags) &
		EFA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK) == phase) {
		/*
		 * Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		dma_rmb();

		/* Handle specific event*/
		handler_cb = efa_com_get_specific_aenq_cb(edev,
							  aenq_common->group);
		handler_cb(data, aenq_e); /* call the actual event handler*/

		/* Get next event entry */
		ci++;
		processed++;

		if (unlikely(ci == aenq->depth)) {
			ci = 0;
			phase = !phase;
		}
		aenq_e = &aenq->entries[ci];
		aenq_common = &aenq_e->aenq_common_desc;
	}

	aenq->cc += processed;
	aenq->phase = phase;

	/* Don't update aenq doorbell if there weren't any processed events */
	if (!processed)
		return;

	/* barrier not needed in case of writel */
	writel(aenq->cc, edev->reg_bar + EFA_REGS_AENQ_CONS_DB_OFF);
}

static void efa_com_mmio_reg_read_resp_addr_init(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;
	u32 addr_high;
	u32 addr_low;

	/* dma_addr_bits is unknown at this point */
	addr_high = (mmio_read->read_resp_dma_addr >> 32) & GENMASK(31, 0);
	addr_low  = mmio_read->read_resp_dma_addr & GENMASK(31, 0);

	writel(addr_high, edev->reg_bar + EFA_REGS_MMIO_RESP_HI_OFF);
	writel(addr_low, edev->reg_bar + EFA_REGS_MMIO_RESP_LO_OFF);
}

int efa_com_mmio_reg_read_init(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;

	spin_lock_init(&mmio_read->lock);
	mmio_read->read_resp =
		dma_zalloc_coherent(edev->dmadev, sizeof(*mmio_read->read_resp),
				    &mmio_read->read_resp_dma_addr, GFP_KERNEL);
	if (unlikely(!mmio_read->read_resp))
		return -ENOMEM;

	efa_com_mmio_reg_read_resp_addr_init(edev);

	mmio_read->read_resp->req_id = 0;
	mmio_read->seq_num = 0;
	mmio_read->mmio_read_timeout = EFA_REG_READ_TIMEOUT_US;

	return 0;
}

void efa_com_mmio_reg_read_destroy(struct efa_com_dev *edev)
{
	struct efa_com_mmio_read *mmio_read = &edev->mmio_read;

	/* just in case someone is still spinning on a read */
	spin_lock(&mmio_read->lock);
	dma_free_coherent(edev->dmadev, sizeof(*mmio_read->read_resp),
			  mmio_read->read_resp, mmio_read->read_resp_dma_addr);
	spin_unlock(&mmio_read->lock);
}

/*
 * efa_com_validate_version - Validate the device parameters
 * @edev: EFA communication layer struct
 *
 * This method validate the device parameters are the same as the saved
 * parameters in edev.
 * This method is useful after device reset, to validate the device mac address
 * and the device offloads are the same as before the reset.
 *
 * @return - 0 on success negative value otherwise.
 */
int efa_com_validate_version(struct efa_com_dev *edev)
{
	u32 ctrl_ver_masked;
	u32 ctrl_ver;
	u32 ver;

	/*
	 * Make sure the EFA version and the controller version are at least
	 * as the driver expects
	 */
	ver = efa_com_reg_read32(edev, EFA_REGS_VERSION_OFF);
	ctrl_ver = efa_com_reg_read32(edev,
				      EFA_REGS_CONTROLLER_VERSION_OFF);

	pr_info("efa device version: %d.%d\n",
		(ver & EFA_REGS_VERSION_MAJOR_VERSION_MASK) >>
			EFA_REGS_VERSION_MAJOR_VERSION_SHIFT,
		ver & EFA_REGS_VERSION_MINOR_VERSION_MASK);

	if (ver < MIN_EFA_VER) {
		pr_err("EFA version is lower than the minimal version the driver supports\n");
		return -1;
	}

	pr_info("efa controller version: %d.%d.%d implementation version %d\n",
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) >>
			EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT,
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) >>
			EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT,
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK),
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_IMPL_ID_MASK) >>
			EFA_REGS_CONTROLLER_VERSION_IMPL_ID_SHIFT);

	ctrl_ver_masked =
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) |
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) |
		(ctrl_ver & EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK);

	/* Validate the ctrl version without the implementation ID */
	if (ctrl_ver_masked < MIN_EFA_CTRL_VER) {
		pr_err("EFA ctrl version is lower than the minimal ctrl version the driver supports\n");
		return -1;
	}

	return 0;
}

/*
 * efa_com_get_dma_width - Retrieve physical dma address width the device
 * supports.
 * @edev: EFA communication layer struct
 *
 * Retrieve the maximum physical address bits the device can handle.
 *
 * @return: > 0 on Success and negative value otherwise.
 */
int efa_com_get_dma_width(struct efa_com_dev *edev)
{
	u32 caps = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);
	int width;

	width = (caps & EFA_REGS_CAPS_DMA_ADDR_WIDTH_MASK) >>
		EFA_REGS_CAPS_DMA_ADDR_WIDTH_SHIFT;

	pr_debug("EFA dma width: %d\n", width);

	if (width < 32 || width > 64) {
		pr_err("DMA width illegal value: %d\n", width);
		return -EINVAL;
	}

	edev->dma_addr_bits = width;

	return width;
}

static int wait_for_reset_state(struct efa_com_dev *edev, u32 timeout,
				u16 exp_state)
{
	u32 val, i;

	for (i = 0; i < timeout; i++) {
		val = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);

		if ((val & EFA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK) ==
		    exp_state)
			return 0;

		pr_debug("reset indication val %d\n", val);
		msleep(EFA_POLL_INTERVAL_MS);
	}

	return -ETIME;
}

/*
 * efa_com_dev_reset - Perform device FLR to the device.
 * @edev: EFA communication layer struct
 * @reset_reason: Specify what is the trigger for the reset in case of an error.
 *
 * @return - 0 on success, negative value on failure.
 */
int efa_com_dev_reset(struct efa_com_dev *edev,
		      enum efa_regs_reset_reason_types reset_reason)
{
	u32 stat, timeout, cap, reset_val;
	int err;

	stat = efa_com_reg_read32(edev, EFA_REGS_DEV_STS_OFF);
	cap = efa_com_reg_read32(edev, EFA_REGS_CAPS_OFF);

	if (!(stat & EFA_REGS_DEV_STS_READY_MASK)) {
		pr_err("Device isn't ready, can't reset device\n");
		return -EINVAL;
	}

	timeout = (cap & EFA_REGS_CAPS_RESET_TIMEOUT_MASK) >>
		  EFA_REGS_CAPS_RESET_TIMEOUT_SHIFT;
	if (!timeout) {
		pr_err("Invalid timeout value\n");
		return -EINVAL;
	}

	/* start reset */
	reset_val = EFA_REGS_DEV_CTL_DEV_RESET_MASK;
	reset_val |= (reset_reason << EFA_REGS_DEV_CTL_RESET_REASON_SHIFT) &
		     EFA_REGS_DEV_CTL_RESET_REASON_MASK;
	writel(reset_val, edev->reg_bar + EFA_REGS_DEV_CTL_OFF);

	/* reset clears the mmio readless address, restore it */
	efa_com_mmio_reg_read_resp_addr_init(edev);

	err = wait_for_reset_state(edev, timeout,
				   EFA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK);
	if (err) {
		pr_err("Reset indication didn't turn on\n");
		return err;
	}

	/* reset done */
	writel(0, edev->reg_bar + EFA_REGS_DEV_CTL_OFF);
	err = wait_for_reset_state(edev, timeout, 0);
	if (err) {
		pr_err("Reset indication didn't turn off\n");
		return err;
	}

	timeout = (cap & EFA_REGS_CAPS_ADMIN_CMD_TO_MASK) >>
		  EFA_REGS_CAPS_ADMIN_CMD_TO_SHIFT;
	if (timeout)
		/* the resolution of timeout reg is 100ms */
		edev->admin_queue.completion_timeout = timeout * 100000;
	else
		edev->admin_queue.completion_timeout = ADMIN_CMD_TIMEOUT_US;

	return 0;
}


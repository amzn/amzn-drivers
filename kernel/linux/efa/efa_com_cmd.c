// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#include "efa.h"
#include "efa_com.h"
#include "efa_com_cmd.h"

void efa_com_set_dma_addr(dma_addr_t addr, u32 *addr_high, u32 *addr_low)
{
	*addr_low  = addr & GENMASK(31, 0);
	*addr_high = (addr >> 32) & GENMASK(31, 0);
}

int efa_com_create_qp(struct efa_com_dev *edev,
		      struct efa_com_create_qp_params *params,
		      struct efa_com_create_qp_result *res)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_create_qp_resp cmd_completion;
	struct efa_admin_create_qp_cmd create_qp_cmd;
	int err;

	memset(&create_qp_cmd, 0x0, sizeof(create_qp_cmd));

	create_qp_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_QP;

	create_qp_cmd.pd = params->pd;
	create_qp_cmd.qp_type = params->qp_type;
	create_qp_cmd.flags = 0;
	create_qp_cmd.sq_base_addr = 0;
	create_qp_cmd.rq_base_addr = params->rq_base_addr;
	create_qp_cmd.send_cq_idx = params->send_cq_idx;
	create_qp_cmd.recv_cq_idx = params->recv_cq_idx;
	create_qp_cmd.qp_alloc_size.send_queue_ring_size =
		params->sq_ring_size_in_bytes;
	create_qp_cmd.qp_alloc_size.send_queue_depth =
			params->sq_depth;
	create_qp_cmd.qp_alloc_size.recv_queue_ring_size =
			params->rq_ring_size_in_bytes;

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&create_qp_cmd,
			       sizeof(create_qp_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err)) {
		pr_err("Failed to create qp [%d]\n", err);
		return err;
	}

	res->qp_handle = cmd_completion.qp_handle;
	res->qp_num = cmd_completion.qp_num;
	res->sq_db_offset = cmd_completion.sq_db_offset;
	res->rq_db_offset = cmd_completion.rq_db_offset;
	res->llq_descriptors_offset = cmd_completion.llq_descriptors_offset;
	res->send_sub_cq_idx = cmd_completion.send_sub_cq_idx;
	res->recv_sub_cq_idx = cmd_completion.recv_sub_cq_idx;

	return err;
}

int efa_com_destroy_qp(struct efa_com_dev *edev,
		       struct efa_com_destroy_qp_params *params)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_destroy_qp_resp cmd_completion;
	struct efa_admin_destroy_qp_cmd qp_cmd;
	int err;

	memset(&qp_cmd, 0x0, sizeof(qp_cmd));

	qp_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_QP;

	qp_cmd.qp_handle = params->qp_handle;

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&qp_cmd,
			       sizeof(qp_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err))
		pr_err("failed to destroy qp-0x%08x [%d]\n", qp_cmd.qp_handle,
		       err);

	return err;
}

int efa_com_create_cq(struct efa_com_dev *edev,
		      struct efa_com_create_cq_params *params,
		      struct efa_com_create_cq_result *result)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_create_cq_resp cmd_completion;
	struct efa_admin_create_cq_cmd create_cmd;
	int err;

	memset(&create_cmd, 0x0, sizeof(create_cmd));
	create_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_CQ;
	create_cmd.cq_caps_2 = (params->entry_size_in_bytes / 4) &
				EFA_ADMIN_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK;
	create_cmd.msix_vector_idx = 0;
	create_cmd.cq_depth = params->cq_depth;
	create_cmd.num_sub_cqs = params->num_sub_cqs;

	efa_com_set_dma_addr(params->dma_addr,
			     &create_cmd.cq_ba.mem_addr_high,
			     &create_cmd.cq_ba.mem_addr_low);

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&create_cmd,
			       sizeof(create_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err)) {
		pr_err("failed to create cq[%d]\n", err);
		return err;
	}

	pr_debug("created cq[%u], depth[%u]\n", cmd_completion.cq_idx,
		 params->cq_depth);
	result->cq_idx = cmd_completion.cq_idx;
	result->actual_depth = params->cq_depth;

	return err;
}

int efa_com_destroy_cq(struct efa_com_dev *edev,
		       struct efa_com_destroy_cq_params *params)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_destroy_cq_resp destroy_resp;
	struct efa_admin_destroy_cq_cmd destroy_cmd;
	int err;

	memset(&destroy_cmd, 0x0, sizeof(destroy_cmd));

	destroy_cmd.cq_idx = params->cq_idx;
	destroy_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_CQ;

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&destroy_cmd,
			       sizeof(destroy_cmd),
			       (struct efa_admin_acq_entry *)&destroy_resp,
			       sizeof(destroy_resp));

	if (unlikely(err))
		pr_err("Failed to destroy CQ. error: %d\n", err);

	return err;
}

int efa_com_register_mr(struct efa_com_dev *edev,
			struct efa_com_reg_mr_params *params,
			struct efa_com_reg_mr_result *result)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_reg_mr_resp cmd_completion;
	struct efa_admin_reg_mr_cmd mr_cmd;
	int err;

	memset(&mr_cmd, 0x0, sizeof(mr_cmd));

	mr_cmd.aq_common_desc.opcode = EFA_ADMIN_REG_MR;
	mr_cmd.aq_common_desc.flags = 0;

	mr_cmd.pd = params->pd;
	mr_cmd.mr_length = params->mr_length_in_bytes;

	mr_cmd.flags |= 0 &
		EFA_ADMIN_REG_MR_CMD_MEM_ADDR_PHY_MODE_EN_MASK;
	mr_cmd.flags |= params->page_shift &
		EFA_ADMIN_REG_MR_CMD_PHYS_PAGE_SIZE_SHIFT_MASK;
	mr_cmd.iova = params->iova;
	mr_cmd.permissions |= params->permissions &
			      EFA_ADMIN_REG_MR_CMD_LOCAL_WRITE_ENABLE_MASK;

	if (params->inline_pbl) {
		memcpy(mr_cmd.pbl.inline_pbl_array,
		       params->pbl.inline_pbl_array,
		       sizeof(mr_cmd.pbl.inline_pbl_array));
	} else {
		mr_cmd.pbl.pbl.length = params->pbl.pbl.length;
		mr_cmd.pbl.pbl.address.mem_addr_low =
			params->pbl.pbl.address.mem_addr_low;
		mr_cmd.pbl.pbl.address.mem_addr_high =
			params->pbl.pbl.address.mem_addr_high;
		mr_cmd.aq_common_desc.flags |=
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_MASK;
		if (params->indirect)
			mr_cmd.aq_common_desc.flags |=
				EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	}

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&mr_cmd,
			       sizeof(mr_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err)) {
		pr_err("failed to register mr [%d]\n", err);
		return err;
	}

	result->l_key = cmd_completion.l_key;
	result->r_key = cmd_completion.r_key;

	return err;
}

int efa_com_dereg_mr(struct efa_com_dev *edev,
		     struct efa_com_dereg_mr_params *params)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_dereg_mr_resp cmd_completion;
	struct efa_admin_dereg_mr_cmd mr_cmd;
	int err;

	memset(&mr_cmd, 0x0, sizeof(mr_cmd));

	mr_cmd.aq_common_desc.opcode = EFA_ADMIN_DEREG_MR;
	mr_cmd.l_key = params->l_key;

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&mr_cmd,
			       sizeof(mr_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err))
		pr_err("failed to de-register mr(lkey-0x%08X) [%d]\n",
		       mr_cmd.l_key, err);

	return err;
}

int efa_com_create_ah(struct efa_com_dev *edev,
		      struct efa_com_create_ah_params *params,
		      struct efa_com_create_ah_result *result)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_create_ah_resp cmd_completion;
	struct efa_admin_create_ah_cmd ah_cmd;
	int err;

	memset(&ah_cmd, 0x0, sizeof(ah_cmd));

	ah_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_AH;

	memcpy(ah_cmd.dest_addr, params->dest_addr, sizeof(ah_cmd.dest_addr));

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&ah_cmd,
			       sizeof(ah_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err)) {
		pr_err("failed to create ah [%d]\n", err);
		return err;
	}

	result->ah = cmd_completion.ah;

	return err;
}

int efa_com_destroy_ah(struct efa_com_dev *edev,
		       struct efa_com_destroy_ah_params *params)
{
	struct efa_com_admin_queue *admin_queue = &edev->admin_queue;
	struct efa_admin_destroy_ah_resp cmd_completion;
	struct efa_admin_destroy_ah_cmd ah_cmd;
	int err;

	memset(&ah_cmd, 0x0, sizeof(ah_cmd));

	ah_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_AH;
	ah_cmd.ah = params->ah;

	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)&ah_cmd,
			       sizeof(ah_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (unlikely(err))
		pr_err("failed to destroy ah-%#x [%d]\n", ah_cmd.ah, err);

	return err;
}

static bool
efa_com_check_supported_feature_id(struct efa_com_dev *edev,
				   enum efa_admin_aq_feature_id feature_id)
{
	u32 feature_mask = 1 << feature_id;

	/* Device attributes is always supported */
	if (feature_id != EFA_ADMIN_DEVICE_ATTR &&
	    !(edev->supported_features & feature_mask))
		return false;

	return true;
}

static int efa_com_get_feature_ex(struct efa_com_dev *edev,
				  struct efa_admin_get_feature_resp *get_resp,
				  enum efa_admin_aq_feature_id feature_id,
				  dma_addr_t control_buf_dma_addr,
				  u32 control_buff_size)
{
	struct efa_admin_get_feature_cmd get_cmd;
	struct efa_com_admin_queue *admin_queue;
	int err;

	if (!efa_com_check_supported_feature_id(edev, feature_id)) {
		pr_err("Feature %d isn't supported\n", feature_id);
		return -EOPNOTSUPP;
	}

	memset(&get_cmd, 0x0, sizeof(get_cmd));
	admin_queue = &edev->admin_queue;

	get_cmd.aq_common_descriptor.opcode = EFA_ADMIN_GET_FEATURE;

	if (control_buff_size)
		get_cmd.aq_common_descriptor.flags =
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	else
		get_cmd.aq_common_descriptor.flags = 0;

	efa_com_set_dma_addr(control_buf_dma_addr,
			     &get_cmd.control_buffer.address.mem_addr_high,
			     &get_cmd.control_buffer.address.mem_addr_low);

	get_cmd.control_buffer.length = control_buff_size;
	get_cmd.feature_common.feature_id = feature_id;
	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)
			       &get_cmd,
			       sizeof(get_cmd),
			       (struct efa_admin_acq_entry *)
			       get_resp,
			       sizeof(*get_resp));

	if (unlikely(err))
		pr_err("Failed to submit get_feature command %d error: %d\n",
		       feature_id, err);

	return err;
}

static int efa_com_get_feature(struct efa_com_dev *edev,
			       struct efa_admin_get_feature_resp *get_resp,
			       enum efa_admin_aq_feature_id feature_id)
{
	return efa_com_get_feature_ex(edev, get_resp, feature_id, 0, 0);
}

int efa_com_get_network_attr(struct efa_com_dev *edev,
			     struct efa_com_get_network_attr_result *result)
{
	struct efa_admin_get_feature_resp resp;
	int err;

	err = efa_com_get_feature(edev, &resp,
				  EFA_ADMIN_NETWORK_ATTR);
	if (unlikely(err)) {
		pr_err("Failed to get network attributes %d\n", err);
		return err;
	}

	memcpy(result->addr, resp.u.network_attr.addr,
	       sizeof(resp.u.network_attr.addr));
	result->mtu = resp.u.network_attr.mtu;

	return 0;
}

int efa_com_get_device_attr(struct efa_com_dev *edev,
			    struct efa_com_get_device_attr_result *result)
{
	struct pci_dev *pdev = container_of(edev->dmadev, struct pci_dev, dev);
	struct efa_admin_get_feature_resp resp;
	int err;

	err = efa_com_get_feature(edev, &resp, EFA_ADMIN_DEVICE_ATTR);
	if (unlikely(err)) {
		pr_err("Failed to get network attributes %d\n", err);
		return err;
	}

	result->fw_version              = resp.u.device_attr.fw_version;
	result->admin_api_version       = resp.u.device_attr.admin_api_version;
	result->vendor_id               = pdev->vendor;
	result->vendor_part_id          = pdev->device;
	result->device_version          = resp.u.device_attr.device_version;
	result->supported_features      = resp.u.device_attr.supported_features;
	result->phys_addr_width         = resp.u.device_attr.phys_addr_width;
	result->virt_addr_width         = resp.u.device_attr.virt_addr_width;
	result->db_bar                  = resp.u.device_attr.db_bar;

	if (unlikely(result->admin_api_version < 1)) {
		pr_err("Failed to get device attr api version [%u < 1]\n",
		       result->admin_api_version);
		return -EINVAL;
	}

	edev->supported_features = resp.u.device_attr.supported_features;
	err = efa_com_get_feature(edev, &resp,
				  EFA_ADMIN_QUEUE_ATTR);
	if (unlikely(err)) {
		pr_err("Failed to get network attributes %d\n", err);
		return err;
	}

	result->max_sq          = resp.u.queue_attr.max_sq;
	result->max_sq_depth    = min_t(u32, resp.u.queue_attr.max_sq_depth,
					U16_MAX);
	result->max_rq          = resp.u.queue_attr.max_sq;
	result->max_rq_depth    = min_t(u32, resp.u.queue_attr.max_rq_depth,
					U16_MAX);
	result->max_cq          = resp.u.queue_attr.max_cq;
	result->max_cq_depth    = resp.u.queue_attr.max_cq_depth;
	result->inline_buf_size = resp.u.queue_attr.inline_buf_size;
	result->max_sq_sge      = resp.u.queue_attr.max_wr_send_sges;
	result->max_rq_sge      = resp.u.queue_attr.max_wr_recv_sges;
	result->max_mr          = resp.u.queue_attr.max_mr;
	result->max_mr_pages    = resp.u.queue_attr.max_mr_pages;
	result->page_size_cap   = PAGE_SIZE;
	result->max_pd          = resp.u.queue_attr.max_pd;
	result->max_ah          = resp.u.queue_attr.max_ah;
	result->sub_cqs_per_cq  = resp.u.queue_attr.sub_cqs_per_cq;

	return 0;
}

int efa_com_get_hw_hints(struct efa_com_dev *edev,
			 struct efa_com_get_hw_hints_result *result)
{
	struct efa_admin_get_feature_resp resp;
	int err;

	err = efa_com_get_feature(edev, &resp, EFA_ADMIN_HW_HINTS);
	if (unlikely(err)) {
		pr_err("Failed to get hw hints %d\n", err);
		return err;
	}

	result->admin_completion_timeout = resp.u.hw_hints.admin_completion_timeout;
	result->driver_watchdog_timeout  = resp.u.hw_hints.driver_watchdog_timeout;
	result->mmio_read_timeout        = resp.u.hw_hints.mmio_read_timeout;
	result->poll_interval            = resp.u.hw_hints.poll_interval;

	return 0;
}

static int efa_com_set_feature_ex(struct efa_com_dev *edev,
				  struct efa_admin_set_feature_resp *set_resp,
				  struct efa_admin_set_feature_cmd *set_cmd,
				  enum efa_admin_aq_feature_id feature_id,
				  dma_addr_t control_buf_dma_addr,
				  u32 control_buff_size)
{
	struct efa_com_admin_queue *admin_queue;
	int err;

	if (!efa_com_check_supported_feature_id(edev, feature_id)) {
		pr_err("Feature %d isn't supported\n", feature_id);
		return -EOPNOTSUPP;
	}

	admin_queue = &edev->admin_queue;

	set_cmd->aq_common_descriptor.opcode = EFA_ADMIN_SET_FEATURE;
	if (control_buff_size) {
		set_cmd->aq_common_descriptor.flags =
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
		efa_com_set_dma_addr(control_buf_dma_addr,
				     &set_cmd->control_buffer.address.mem_addr_high,
				     &set_cmd->control_buffer.address.mem_addr_low);
	} else {
		set_cmd->aq_common_descriptor.flags = 0;
	}

	set_cmd->control_buffer.length = control_buff_size;
	set_cmd->feature_common.feature_id = feature_id;
	err = efa_com_cmd_exec(admin_queue,
			       (struct efa_admin_aq_entry *)set_cmd,
			       sizeof(*set_cmd),
			       (struct efa_admin_acq_entry *)set_resp,
			       sizeof(*set_resp));

	if (unlikely(err))
		pr_err("Failed to submit set_feature command %d error: %d\n",
		       feature_id, err);

	return err;
}

static int efa_com_set_feature(struct efa_com_dev *edev,
			       struct efa_admin_set_feature_resp *set_resp,
			       struct efa_admin_set_feature_cmd *set_cmd,
			       enum efa_admin_aq_feature_id feature_id)
{
	return efa_com_set_feature_ex(edev, set_resp, set_cmd, feature_id,
				      0, 0);
}

int efa_com_set_aenq_config(struct efa_com_dev *edev, u32 groups)
{
	struct efa_admin_get_feature_resp get_resp;
	struct efa_admin_set_feature_resp set_resp;
	struct efa_admin_set_feature_cmd cmd;
	int err;

	pr_debug("configuring aenq with groups[0x%x]\n", groups);

	err = efa_com_get_feature(edev, &get_resp, EFA_ADMIN_AENQ_CONFIG);
	if (unlikely(err)) {
		pr_err("Failed to get aenq attributes: %d\n", err);
		return err;
	}

	pr_debug("Get aenq groups: supported[0x%x] enabled[0x%x]\n",
		 get_resp.u.aenq.supported_groups,
		 get_resp.u.aenq.enabled_groups);

	if ((get_resp.u.aenq.supported_groups & groups) != groups) {
		pr_err("Trying to set unsupported aenq groups[0x%x] supported[0x%x]\n",
		       groups, get_resp.u.aenq.supported_groups);
		return -EOPNOTSUPP;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.u.aenq.enabled_groups = groups;
	err = efa_com_set_feature(edev, &set_resp, &cmd,
				  EFA_ADMIN_AENQ_CONFIG);
	if (unlikely(err)) {
		pr_err("Failed to set aenq attributes: %d\n", err);
		return err;
	}

	return 0;
}

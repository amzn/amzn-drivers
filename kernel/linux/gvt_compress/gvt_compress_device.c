// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include "gvt_compress_device.h"
#include "gvt_compress_regs.h"

/** HW completion descriptor */
struct gvt_comp_udma_cdesc {
	__le32 word0; /* ctrl_meta */
	__le32 word1; /* compress output size */
	__le32 word2; /* compress output size */
	__le32 word3; /* decompress output size */
};

/** HW submission descriptor */
struct gvt_comp_udma_desc {
	__le32 len_ctrl;
	__le32 meta_ctrl;
	__le64 buf_ptr;
} __aligned(16);

/** DMA buffer types */
enum gvt_udma_buf_type {
	GVT_UDMA_BUF_SA_UPDATE	= 0,
	GVT_UDMA_BUF_SRC	= 3,
	GVT_UDMA_BUF_AUTH_SIGN	= 4,
};

enum gvt_comp_udma_queue_status {
	GVT_UDMA_QUEUE_DISABLED = 1,
	GVT_UDMA_QUEUE_ENABLED = 2,
	GVT_UDMA_QUEUE_ABORTED = 3,
};

struct __aligned(64) gvt_comp_udma_q {
	void __iomem *q_regs;
	void __iomem *shadow_regs;

	uint16_t size_mask;
	struct gvt_comp_udma_desc *desc_base_ptr;
	uint16_t next_desc_idx;

	uint32_t desc_ring_id;
	uint8_t *cdesc_base_ptr;
	uint32_t cdesc_size;
	uint16_t next_cdesc_idx;
	uint8_t *end_cdesc_ptr;
	uint16_t comp_head_idx;

	struct gvt_comp_udma_cdesc *comp_head_ptr;

	uint32_t pkt_crnt_descs;
	uint32_t comp_ring_id;

	dma_addr_t desc_phy_base;
	dma_addr_t cdesc_phy_base;

	uint32_t flags;
	uint32_t size;
	enum gvt_comp_udma_queue_status status;
};

struct gvt_comp_udma {
	void __iomem *regs_base;
	struct gvt_comp_udma_q udma_q[GVT_MAX_CHANNELS];
};

struct gvt_comp_engine {
	char name[GVT_MAX_NAME_LENGTH];
	void __iomem *regs_base;
	struct gvt_comp_udma	tx_udma;
	struct gvt_comp_udma	rx_udma;
	bool sa_cached[MAX_ALG_PARAM_INDEX];
	bool limit_sa_cached[MAX_ALG_PARAM_INDEX];
};

struct gvt_dma_buf {
	dma_addr_t addr;
	uint32_t len;
};

struct gvt_comp_sw_desc {
	enum gvt_comp_decomp dir;

	struct gvt_dma_buf src_bufs[GVT_UDMA_MAX_SRC_DESCS];
	unsigned int src_num;
	struct gvt_dma_buf dst_bufs[GVT_UDMA_MAX_SRC_DESCS];

	uint32_t sa_indx;
	struct gvt_dma_buf sa_in;

	struct gvt_dma_buf auth_sign_in;
	struct gvt_dma_buf auth_sign_out;
	uint32_t auth_in_off;

	unsigned int src_valid_cnt;
	unsigned int dst_valid_cnt;
};

/** get completion descriptor pointer from its index */
#define gvt_comp_udma_cdesc_idx_to_ptr(udma_q, idx)			      \
	((struct gvt_comp_udma_cdesc *)((udma_q)->cdesc_base_ptr +   \
				(idx) * (udma_q)->cdesc_size))

static inline void gvt_writel_masked_relaxed(void __iomem *reg,
					     uint32_t mask,
					     uint32_t data)
{
	uint32_t temp;

	temp = ((mask) & (data)) | ((~(mask)) & (readl(reg)));
	writel_relaxed(temp, reg);
}

static void gvt_comp_udma_shadow_q_reg_permission_set(
					void __iomem *shadow_access_regs,
					unsigned int q_reg_offset,
					bool write)
{
	void __iomem *permissions_reg;
	unsigned int reg_idx, bit_idx;

	reg_idx = q_reg_offset / sizeof(uint32_t); /* Bytes to register index */

	permissions_reg = shadow_access_regs + ((reg_idx / 32) + !!write) * 4;

	bit_idx = reg_idx % 32;
	gvt_writel_masked_relaxed(permissions_reg,
				  BIT(bit_idx),
				  BIT(bit_idx));
}

static void gvt_comp_udma_init_aux(struct gvt_comp_udma *udma,
				  uint8_t num_of_queues)
{
	int i;

	for (i = 0; i < num_of_queues; i++)
		udma->udma_q[i].status = GVT_UDMA_QUEUE_DISABLED;

	gvt_comp_udma_shadow_q_reg_permission_set(
			udma->regs_base + GVT_UDMA_SHADOW_ACCESS_OFF,
			GVT_UDMA_QUEUE_REG_DRTP_INC_OFF,
			true);

	gvt_comp_udma_shadow_q_reg_permission_set(
			udma->regs_base + GVT_UDMA_SHADOW_ACCESS_OFF,
			GVT_UDMA_QUEUE_REG_CRHP_OFF,
			false);
}

static void gvt_comp_udma_tx_init(struct gvt_comp_udma *udma,
				  void __iomem *udma_regs_base,
				  uint8_t num_of_queues)
{
	/* 128 total outstanding writes per-master allocation: */
	const uint32_t ocfg_rd_m2s_desc = 16;
	const uint32_t ocfg_wr_m2s_cmp = 4;
	const uint32_t ocfg_rd_data = 96;
	const uint32_t fifo_depth = 1024;
	const uint32_t pthr = 60;
	const uint32_t mthr = 8;
	uint32_t data;

	udma->regs_base = udma_regs_base;

	gvt_comp_udma_init_aux(udma, num_of_queues);

	gvt_writel_masked_relaxed(
				udma_regs_base + GVT_UDMA_TX_DATA_CFG_OFF,
				GVT_UDMA_TX_DATA_CFG_DATA_FIFO_DEPTH_MASK,
				fifo_depth);

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_TX_OSTAND_CFG_OFF,
		GVT_UDMA_TX_OSTAND_CFG_MAX_DATA_RD_MASK |
		GVT_UDMA_TX_OSTAND_CFG_MAX_DESC_RD_MASK |
		GVT_UDMA_TX_OSTAND_CFG_MAX_COMP_REQ_MASK,
		(ocfg_rd_data << GVT_UDMA_TX_OSTAND_CFG_MAX_DATA_RD_SHIFT) |
		(ocfg_rd_m2s_desc << GVT_UDMA_TX_OSTAND_CFG_MAX_DESC_RD_SHIFT) |
		(ocfg_wr_m2s_cmp << GVT_UDMA_TX_OSTAND_CFG_MAX_COMP_REQ_SHIFT));

	/* Ack time out */
	writel_relaxed(0, udma_regs_base + GVT_UDMA_TX_CFG_APPLICATION_ACK_OFF);

	data = GVT_UDMA_TX_CFG_LEN_ENCODE_64K |
		GVT_UDMA_TX_CFG_LEN_MAX_PKT_SIZE_MASK;
	gvt_writel_masked_relaxed(udma_regs_base + GVT_UDMA_TX_CFG_LEN_OFF,
				  data,
				  data);

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_TX_DESC_PREF_CFG_2_OFF,
		GVT_UDMA_TX_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_MASK,
		pthr << GVT_UDMA_TX_RD_DESC_PREF_CFG_2_MAX_DESC_PER_PKT_SHIFT);

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_TX_DESC_PREF_CFG_3_OFF,
		GVT_UDMA_TX_RD_DESC_PREF_CFG_3_PREF_THR_MASK |
		GVT_UDMA_TX_RD_DESC_PREF_CFG_3_MIN_BURST_MASK,
		(pthr << GVT_UDMA_TX_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) |
		(mthr << GVT_UDMA_TX_RD_DESC_PREF_CFG_3_MIN_BURST_SHIFT));
}

static void gvt_comp_udma_rx_init(struct gvt_comp_udma *udma,
				  void __iomem *udma_regs_base,
				  uint8_t num_of_queues)
{
	/* 128 total outstanding writes per-master allocation: */
	const uint32_t ocfg_wr_s2m_comp = 12;
	const uint32_t ocfg_rd_s2m_desc = 16;
	const uint32_t ocfg_wr_data = 96;
	const uint32_t pthr = 60;
	const uint32_t mthr = 8;

	udma->regs_base = udma_regs_base;

	gvt_comp_udma_init_aux(udma, num_of_queues);

	gvt_comp_udma_shadow_q_reg_permission_set(
			udma->regs_base + GVT_UDMA_SHADOW_ACCESS_OFF,
			GVT_UDMA_RX_QUEUE_REG_SW_CTRL_OFF,
			true);

	/* initialize configuration registers to correct values */
	/* Ack time out */
	writel_relaxed(0, udma_regs_base + GVT_UDMA_RX_CFG_APPLICATION_ACK_OFF);

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_RX_OSTAND_CFG_RD_OFF,
		GVT_UDMA_RX_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_MASK,
		(ocfg_rd_s2m_desc <<
			GVT_UDMA_RX_OSTAND_CFG_RD_MAX_DESC_RD_OSTAND_SHIFT));

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_RX_OSTAND_CFG_WR_OFF,
		GVT_UDMA_RX_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_MASK |
		GVT_UDMA_RX_OSTAND_CFG_WR_MAX_COMP_REQ_MASK,
		(ocfg_wr_data <<
			GVT_UDMA_RX_OSTAND_CFG_WR_MAX_DATA_WR_OSTAND_SHIFT) |
		(ocfg_wr_s2m_comp <<
			GVT_UDMA_RX_OSTAND_CFG_WR_MAX_COMP_REQ_SHIFT));

	gvt_writel_masked_relaxed(
		udma_regs_base + GVT_UDMA_RX_DESC_PREF_CFG_3_OFF,
		GVT_UDMA_RX_RD_DESC_PREF_CFG_3_PREF_THR_MASK |
		GVT_UDMA_RX_RD_DESC_PREF_CFG_3_MIN_BURST_MASK,
		(pthr << GVT_UDMA_RX_RD_DESC_PREF_CFG_3_PREF_THR_SHIFT) |
		(mthr << GVT_UDMA_RX_RD_DESC_PREF_CFG_3_MIN_BURST_SHIFT));

	gvt_writel_masked_relaxed(udma_regs_base + GVT_UDMA_RX_COMP_CFG_1C_OFF,
				  GVT_UDMA_RX_COMP_CFG_1C_DESC_SIZE_MASK,
				  (gvt_comp_device_get_hw_cdesc_size() >> 2));
}

static int gvt_comp_init(struct gvt_comp_device *comp_dev)
{
	/* AXI timeout of 1M is approximately 2.6 ms */
	const uint32_t axi_timeout = 1000000;
	struct gvt_comp_engine *gvt_engine;
	int rc, i;

	gvt_engine = comp_dev->gvt_engine;

	strncpy(gvt_engine->name, dev_name(comp_dev->dev), GVT_MAX_NAME_LENGTH);

	gvt_engine->regs_base = comp_dev->dma_regs_base;

	/* initialize the udma  */
	gvt_comp_udma_tx_init(&gvt_engine->tx_udma,
			      comp_dev->dma_regs_base + GVT_UDMA_TX_REGS_OFF,
			      comp_dev->num_channels);

	gvt_comp_udma_rx_init(&gvt_engine->rx_udma,
			      comp_dev->dma_regs_base + GVT_UDMA_RX_REGS_OFF,
			      comp_dev->num_channels);

	writel_relaxed(axi_timeout, comp_dev->dma_regs_base +
					GVT_UDMA_GEN_REGS_OFF +
					GVT_UDMA_GEN_AXI_CFG_1_OFF);

	for (i = 0; i < GVT_MAX_CHANNELS; i++) {
		void __iomem *regs_base_gen_ex = comp_dev->dma_regs_base +
						 GVT_UDMA_GEN_EX_REGS_OFF;
		writel_relaxed(0xffffffff,
			regs_base_gen_ex + GVT_UDMA_GEN_EX_VMPR_V4_OFF(i, 0x0));
		writel_relaxed(0xffffffff,
			regs_base_gen_ex + GVT_UDMA_GEN_EX_VMPR_V4_OFF(i, 0x4));
		writel_relaxed(0xffffffff,
			regs_base_gen_ex + GVT_UDMA_GEN_EX_VMPR_V4_OFF(i, 0x8));
		writel_relaxed(0xffffffff,
			regs_base_gen_ex + GVT_UDMA_GEN_EX_VMPR_V4_OFF(i, 0xC));
	}

	writel_relaxed(GVT_COMP_SW_VERSION,
			comp_dev->dma_regs_base + GVT_SW_VER_METRIC_OFFSET);

	rc = gvt_unit_adapter_init(comp_dev->pdev);
	if (rc)
		return rc;

	gvt_comp_engine_init(comp_dev->engine_regs_base);

	return 0;
}

static void gvt_comp_q_common_init(struct gvt_comp_udma *udma,
				   struct gvt_comp_udma_q *udma_q)
{
	memset(udma_q, 0, sizeof(struct gvt_comp_udma_q));
	udma_q->cdesc_size = gvt_comp_device_get_hw_cdesc_size();
	udma_q->next_desc_idx = 0;
	udma_q->next_cdesc_idx = 0;
	udma_q->comp_head_idx = 0;
	udma_q->desc_ring_id = 1;
	udma_q->comp_ring_id = 1;
	udma_q->pkt_crnt_descs = 0;
	udma_q->flags = 0;
	udma_q->status = GVT_UDMA_QUEUE_DISABLED;
}

static void gvt_comp_q_tx_init(struct gvt_comp_udma *udma,
			       void __iomem *regs_base,
			       uint32_t qid,
			       struct gvt_comp_udma_desc *desc_base,
			       dma_addr_t desc_phy_base,
			       uint32_t queue_size)
{
	struct gvt_comp_udma_q *udma_q = &udma->udma_q[qid];
	uint32_t val;

	gvt_comp_q_common_init(udma, udma_q);

	udma_q->size_mask = queue_size - 1;
	udma_q->size = queue_size;
	udma_q->desc_base_ptr = desc_base;
	udma_q->desc_phy_base = desc_phy_base;

	udma_q->q_regs = udma->regs_base + GVT_UDMA_QUEUE_REGS_OFF(qid);
	/* shadow registers are based off on their register base */
	udma_q->shadow_regs = regs_base +
			      GVT_UDMA_QUEUE_TX_SHADOW_REGS_OFF(qid);

	/* start hardware configuration */
	val = readl(udma_q->q_regs + GVT_UDMA_QUEUE_REG_RLIMIT_MASK_OFF);
	val &= ~GVT_UDMA_Q_RATE_LIMIT_MASK_INTERNAL_PAUSE_DMB;
	writel_relaxed(val,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_RLIMIT_MASK_OFF);

	writel_relaxed(desc_phy_base & 0xFFFFFFFF,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRBP_LOW_OFF);
	writel_relaxed(desc_phy_base >> 32,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRBP_HIGH_OFF);
	writel_relaxed(queue_size,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRL_OFF);

	val = readl(udma_q->q_regs + GVT_UDMA_TX_QUEUE_REG_COMP_CFG_OFF);
	val &= ~GVT_UDMA_Q_COMP_CFG_EN_COMP_RING_UPDATE;
	val |= GVT_UDMA_Q_COMP_CFG_DIS_COMP_COAL;
	writel_relaxed(val,
		       udma_q->q_regs + GVT_UDMA_TX_QUEUE_REG_COMP_CFG_OFF);

	gvt_writel_masked_relaxed(
		udma_q->q_regs + GVT_UDMA_QUEUE_REG_CFG_OFF,
		GVT_UDMA_Q_CFG_EN_PREF | GVT_UDMA_Q_CFG_EN_SCHEDULING,
		GVT_UDMA_Q_CFG_EN_PREF | GVT_UDMA_Q_CFG_EN_SCHEDULING);
	udma_q->status = GVT_UDMA_QUEUE_ENABLED;
}

static void gvt_comp_q_rx_init(struct gvt_comp_udma *udma,
			       void __iomem *regs_base,
			       uint32_t qid,
			       struct gvt_comp_udma_desc *desc_base,
			       dma_addr_t desc_phy_base,
			       uint8_t *cdesc_base,
			       dma_addr_t cdesc_phy_base,
			       uint32_t queue_size)
{
	struct gvt_comp_udma_q *udma_q;

	udma_q = &udma->udma_q[qid];

	gvt_comp_q_common_init(udma, udma_q);

	udma_q->size_mask = queue_size - 1;
	udma_q->size = queue_size;
	udma_q->desc_base_ptr = desc_base;
	udma_q->desc_phy_base = desc_phy_base;
	udma_q->cdesc_base_ptr = cdesc_base;
	udma_q->cdesc_phy_base = cdesc_phy_base;
	udma_q->end_cdesc_ptr = cdesc_base +
				(udma_q->size - 1) * udma_q->cdesc_size;
	udma_q->comp_head_ptr = (struct gvt_comp_udma_cdesc *)cdesc_base;

	udma_q->q_regs = udma->regs_base + GVT_UDMA_QUEUE_REGS_OFF(qid);
	/* shadow registers are based off on their register base */
	udma_q->shadow_regs = regs_base +
			      GVT_UDMA_QUEUE_RX_SHADOW_REGS_OFF(qid);

	/* initialize RX queue */
	writel_relaxed(desc_phy_base & 0xFFFFFFFF,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRBP_LOW_OFF);
	writel_relaxed(desc_phy_base >> 32,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRBP_HIGH_OFF);

	writel_relaxed(queue_size,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_DRL_OFF);

	writel_relaxed(cdesc_phy_base & 0xFFFFFFFF,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_CRBP_LOW_OFF);
	writel_relaxed(cdesc_phy_base >> 32,
		       udma_q->q_regs + GVT_UDMA_QUEUE_REG_CRBP_HIGH_OFF);

	gvt_writel_masked_relaxed(
		udma_q->q_regs + GVT_UDMA_RX_QUEUE_REG_COMP_CFG_OFF,
		GVT_UDMA_Q_COMP_CFG_EN_COMP_RING_UPDATE |
		GVT_UDMA_Q_COMP_CFG_DIS_COMP_COAL,
		GVT_UDMA_Q_COMP_CFG_EN_COMP_RING_UPDATE |
		GVT_UDMA_Q_COMP_CFG_DIS_COMP_COAL);

	gvt_writel_masked_relaxed(
		udma_q->q_regs + GVT_UDMA_QUEUE_REG_CFG_OFF,
		GVT_UDMA_Q_CFG_EN_PREF | GVT_UDMA_Q_CFG_EN_SCHEDULING,
		GVT_UDMA_Q_CFG_EN_PREF | GVT_UDMA_Q_CFG_EN_SCHEDULING);
	udma_q->status = GVT_UDMA_QUEUE_ENABLED;
}

static void gvt_comp_device_chan_init(struct gvt_comp_device *comp_dev,
				      struct gvt_comp_chan *chan)
{
	struct gvt_comp_engine *gvt_engine;
	unsigned int chan_idx;

	chan_idx = chan->idx;
	gvt_engine = comp_dev->gvt_engine;

	gvt_comp_q_tx_init(&gvt_engine->tx_udma,
			   gvt_engine->regs_base,
			   chan_idx,
			   chan->tx_dma_desc_virt,
			   chan->tx_dma_desc,
			   comp_dev->hw_descs_num);

	gvt_comp_q_rx_init(&gvt_engine->rx_udma,
			   gvt_engine->regs_base,
			   chan_idx,
			   chan->rx_dma_desc_virt,
			   chan->rx_dma_desc,
			   chan->rx_dma_cdesc_virt,
			   chan->rx_dma_cdesc,
			   comp_dev->hw_descs_num);
}

int gvt_comp_device_init(struct gvt_comp_device *comp_dev)
{
	struct gvt_comp_engine *gvt_engine;
	int i, rc;

	rc = gvt_comp_init(comp_dev);
	if (rc) {
		dev_err(comp_dev->dev, "gvt_comp_init failed\n");
		return rc;
	}

	for (i = 0; i < comp_dev->num_channels; i++)
		gvt_comp_device_chan_init(comp_dev, &comp_dev->channels[i]);

	gvt_engine = comp_dev->gvt_engine;

	writel_relaxed(GVT_UDMA_CHANGE_STATE_NORMAL,
		       gvt_engine->regs_base +
		       GVT_UDMA_RX_REGS_OFF +
		       GVT_UDMA_CHANGE_STATE_OFF);
	writel_relaxed(GVT_UDMA_CHANGE_STATE_NORMAL,
		       gvt_engine->regs_base +
		       GVT_UDMA_TX_REGS_OFF +
		       GVT_UDMA_CHANGE_STATE_OFF);

	return 0;
}

void gvt_comp_device_terminate(struct gvt_comp_device *comp_dev)
{
	struct gvt_comp_engine *gvt_engine;

	gvt_engine = comp_dev->gvt_engine;

	writel_relaxed(GVT_UDMA_CHANGE_STATE_DIS,
		       gvt_engine->regs_base +
		       GVT_UDMA_RX_REGS_OFF +
		       GVT_UDMA_CHANGE_STATE_OFF);
	writel_relaxed(GVT_UDMA_CHANGE_STATE_DIS,
		       gvt_engine->regs_base +
		       GVT_UDMA_TX_REGS_OFF +
		       GVT_UDMA_CHANGE_STATE_OFF);
}

uint32_t gvt_comp_device_get_gvt_engine_size(void)
{
	return sizeof(struct gvt_comp_engine);
}

uint32_t gvt_comp_device_get_sw_desc_size(void)
{
	return sizeof(struct gvt_comp_sw_desc);
}

uint32_t gvt_comp_device_get_hw_desc_size(void)
{
	return sizeof(struct gvt_comp_udma_desc);
}

uint32_t gvt_comp_device_get_hw_cdesc_size(void)
{
	return sizeof(struct gvt_comp_udma_cdesc);
}

uint32_t gvt_comp_device_get_alg_desc_size(void)
{
	return GVT_DEVICE_SA_SIZE;
}

int gvt_comp_device_reset(struct gvt_comp_device *comp_dev)
{
	struct pci_dev *pdev;
	struct device *dev;
	u16 device_id;
	int rc;

	dev = comp_dev->dev;
	pdev = comp_dev->pdev;

	dev_dbg(dev, "FLR the device: %s\n", dev_name(dev));

	rc = pci_save_state(pdev);
	if (rc) {
		dev_err(dev, "%s: could not save state\n", __func__);
		return rc;
	}

	pcie_flr(pdev);

	/* Read vendor id and confirm device is alive */
	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);
	if (unlikely(device_id != comp_dev->dev_id)) {
		dev_err(dev, "%s: FLR failed\n", __func__);
		return -ENODEV;
	}

	pci_restore_state(pdev);

	return 0;
}

static inline unsigned int gvt_comp_lz_prof_id(enum gvt_comp_decomp dir)
{
	int compress_level = gvt_comp_get_compress_level();
	unsigned int lz_prof_id;

	if (dir == GVT_DECOMPRESS)
		return GVT_COMP_DEFLATE_32KB_HISTORY_100_NORMAL;

	switch (compress_level) {
	case GVT_COMP_LEVEL_1:
		lz_prof_id = GVT_COMP_DEFLATE_1KB_HISTORY_100_NORMAL;
		break;
	case GVT_COMP_LEVEL_2:
		lz_prof_id = GVT_COMP_DEFLATE_4KB_HISTORY_100_NORMAL;
		break;
	case GVT_COMP_LEVEL_3:
		lz_prof_id = GVT_COMP_DEFLATE_8KB_HISTORY_100_NORMAL;
		break;
	case GVT_COMP_LEVEL_4:
		lz_prof_id = GVT_COMP_DEFLATE_16KB_HISTORY_100_NORMAL;
		break;
	case GVT_COMP_LEVEL_5:
		lz_prof_id = GVT_COMP_DEFLATE_32KB_HISTORY_100_NORMAL;
		break;
	default:
		lz_prof_id = GVT_COMP_DEFLATE_16KB_HISTORY_100_NORMAL;
	}

	return lz_prof_id;
}

static void gvt_comp_device_sa_init(struct gvt_comp_chan *chan,
				    struct gvt_comp_req *req)
{
	const uint32_t mask_bitcount =
		__builtin_popcount(GVT_CRYPT_SAD_CMPRS_LZ_PROF_IDX_LSBS_MASK);
	const uint32_t shifted_mask =
		(GVT_CRYPT_SAD_CMPRS_LZ_PROF_IDX_LSBS_MASK >>
			GVT_CRYPT_SAD_CMPRS_LZ_PROF_IDX_LSBS_SHIFT);
	const unsigned int crc32_auth = 10;
	unsigned int cmprs_lz_prof_idx;
	uint32_t sa_word;
	__le32 *hw_sa;

	/* Two SA entries assigned per algorithm for limit and no-limit modes */
	BUILD_BUG_ON(!(GVT_DEVICE_MAX_SA >= (MAX_ALG_PARAM_INDEX * 2)));

	cmprs_lz_prof_idx = gvt_comp_lz_prof_id(req->dir);

	hw_sa = chan->alg_dma_desc_virt;

	sa_word = 0;
	sa_word |= (GVT_CRYPT_SAD_OP_CMPRS | GVT_CRYPT_SAD_OP_AUTH)
						<< GVT_CRYPT_SAD_OP_SHIFT;
	sa_word |= (crc32_auth) << GVT_CRYPT_SAD_AUTH_TYPE_SHIFT;
	sa_word |= GVT_CRYPT_SAD_AUTH_AFTER_DEC;
	hw_sa[0] = cpu_to_le32(sa_word);
	/* signature size, crc32_mode and sa_length are 0 */
	sa_word = 0;
	sa_word |= (GVT_COMP_ICRC_PROF_ID_CRC32)
					<< GVT_CRYPT_SAD_ICRC_PROF_ID_SHIFT;
	sa_word |= (cmprs_lz_prof_idx & shifted_mask)
				<< GVT_CRYPT_SAD_CMPRS_LZ_PROF_IDX_LSBS_SHIFT;
	sa_word |= (cmprs_lz_prof_idx >> mask_bitcount)
			<< GVT_CRYPT_SAD_CMPRS_LZ_PROF_IDX_MSBS_SHIFT;
	sa_word |= (!req->limit_compression)
			<< GVT_CRYPT_SAD_CMPRS_FORCE_COMPRESSED_OUT_SHIFT;
	hw_sa[1] = cpu_to_le32(sa_word);
	hw_sa[2] = 0;
	hw_sa[3] = 0;
}

static void gvt_comp_device_chan_rx_reset(struct gvt_comp_udma_q *rx_udma_q,
					  uint32_t num_cdesc_to_zero_out)
{
	struct gvt_comp_udma_cdesc *rx_cdesc;

	/* step 1: zero-out completion descriptors to avoid misinterpreting
	 *	   completions from earlier transaction
	 *	   for example: ring_id would be non-zero
	 */
	rx_cdesc = (struct gvt_comp_udma_cdesc *)rx_udma_q->cdesc_base_ptr;
	memset((void *)rx_cdesc, 0,
	       num_cdesc_to_zero_out * gvt_comp_device_get_hw_cdesc_size());

	/* step 2: reset the queue registers as there could have been
	 *	   excess rx submissions in earlier transaction
	 *	   which weren't completed
	 */
	writel_relaxed(
		GVT_UDMA_RX_Q_SW_CTRL_RST_Q,
		rx_udma_q->shadow_regs + GVT_UDMA_RX_QUEUE_REG_SW_CTRL_OFF);

	/* step 3: reset SW representation of the HW */
	/* reset submission and completion ring id */
	rx_udma_q->desc_ring_id = 1;
	rx_udma_q->comp_ring_id = 1;
	/* reset next available submission and completion descriptors */
	rx_udma_q->next_desc_idx = 0;
	rx_udma_q->next_cdesc_idx = 0;
	/* completion head pointer shadow */
	rx_udma_q->comp_head_ptr = rx_cdesc;
	rx_udma_q->comp_head_idx = 0;
	/* reset number of already processed descriptors */
	rx_udma_q->pkt_crnt_descs = 0;
}

static int sg_map_to_xaction_buffers(struct scatterlist *sg_in,
				     unsigned int length,
				     enum gvt_comp_decomp dir,
				     bool dst_buf_type,
				     struct gvt_dma_buf *bufs_out,
				     unsigned int max_buf_idx,
				     int *buf_idx)
{
	struct scatterlist *sg, *next_sg;
	unsigned int max_buf_length;
	dma_addr_t hdr_addr = 0;
	unsigned int remain;
	bool hdr_present;
	bool hdr_flag;
	bool contig;
	int rc = 0;

	max_buf_length = GVT_UDMA_TX_DESC_LEN_MASK + 1;
	sg = sg_in;
	remain = length;
	*buf_idx = 0;

	if (((dir == GVT_COMPRESS) && dst_buf_type) ||
	    ((dir == GVT_DECOMPRESS) && !dst_buf_type))
		hdr_present = true;
	else
		hdr_present = false;

	/* When data contains header, save the header for last */
	if (hdr_present) {
		hdr_addr = sg_dma_address(sg);

		/* First buffer index points to data */
		bufs_out[*buf_idx].addr = sg_dma_address(sg) +
					  GVT_COMP_HDR_LEN;
		bufs_out[*buf_idx].len = 0;
		hdr_flag = true;
	} else {
		bufs_out[*buf_idx].addr = sg_dma_address(sg);
		bufs_out[*buf_idx].len = 0;
		hdr_flag = false;
	}

	/* Remaining data buffers */
	while (remain > sg_dma_len(sg)) {
		if (hdr_flag) {
			bufs_out[*buf_idx].len +=
			    sg_dma_len(sg) - GVT_COMP_HDR_LEN;
			hdr_flag = false;
		} else
			bufs_out[*buf_idx].len += sg_dma_len(sg);
		remain -= sg_dma_len(sg);
		next_sg = sg_next(sg);

		if ((bufs_out[*buf_idx].len + sg_dma_len(next_sg) <=
		     max_buf_length) &&
		    (sg_dma_address(sg) + sg_dma_len(sg) ==
		     sg_dma_address(next_sg)))
			contig = true;
		else
			contig = false;

		if (!contig) {
			(*buf_idx)++;
			if (*buf_idx >= max_buf_idx) {
				rc = -EFBIG;
				goto done;
			}
			bufs_out[*buf_idx].addr =
					sg_dma_address(next_sg);
			bufs_out[*buf_idx].len = 0;
		}
		sg = next_sg;
	}

	/* Last data buffer */
	if (hdr_flag) {
		bufs_out[*buf_idx].len +=
			remain - GVT_COMP_HDR_LEN;
		hdr_flag = false;
	} else
		bufs_out[*buf_idx].len += remain;

	/* Use the saved header address as last buffer */
	if (hdr_addr) {
		(*buf_idx)++;

		if (*buf_idx >= max_buf_idx) {
			rc = -EFBIG;
			goto done;
		}

		bufs_out[*buf_idx].addr = hdr_addr;
		bufs_out[*buf_idx].len = GVT_COMP_HDR_LEN;
	}

	(*buf_idx)++;

	/* When data contains header,
	 * we expect buf count to be at least 2
	 */
	if (unlikely(hdr_present && (*buf_idx < 2)))
		rc = -EINVAL;

done:
	return rc;
}

static int prepare_xaction_buffers(struct device *dev,
				   struct gvt_comp_engine *gvt_engine,
				   struct gvt_comp_chan *chan,
				   struct gvt_comp_req *req,
				   struct gvt_comp_sw_desc *sw_desc,
				   enum gvt_alg_index alg_index)
{
	bool sa_cached;
	int max_descs;
	int rc;

	/* Is SA already cached ? */
	sa_cached = req->limit_compression ?
			gvt_engine->limit_sa_cached[alg_index] :
			gvt_engine->sa_cached[alg_index];

	/* Subtract one desc for CRC or SA */
	if ((req->dir == GVT_DECOMPRESS) || (!sa_cached))
		max_descs = GVT_UDMA_MAX_SRC_DESCS - 1;
	else
		max_descs = GVT_UDMA_MAX_SRC_DESCS;

	dma_map_sg(dev, req->src, req->src_nents, DMA_TO_DEVICE);
	rc = sg_map_to_xaction_buffers(req->src,
				       req->slen,
				       req->dir,
				       false,
				       sw_desc->src_bufs,
				       max_descs,
				       &sw_desc->src_valid_cnt);
	if (unlikely(rc)) {
		if (likely(rc == -EFBIG))
			dev_err(dev, "Src is too big\n");
		else
			dev_err(dev, "Src not big enough\n");

		return rc;
	}

	/* Subtract one desc for CRC */
	if (req->dir == GVT_COMPRESS)
		max_descs = GVT_UDMA_MAX_SRC_DESCS - 1;
	else
		max_descs = GVT_UDMA_MAX_SRC_DESCS;

	/* Subtract one desc for CRC */
	dma_map_sg(dev, req->dst, req->dst_nents, DMA_FROM_DEVICE);
	rc = sg_map_to_xaction_buffers(req->dst,
				       req->dlen,
				       req->dir,
				       true,
				       sw_desc->dst_bufs,
				       max_descs,
				       &sw_desc->dst_valid_cnt);
	if (unlikely(rc)) {
		if (likely(rc == -EFBIG))
			dev_err(dev, "Dst is too big\n");
		else
			dev_err(dev, "Dst not big enough\n");

		return rc;
	}

	if (req->dir == GVT_COMPRESS)
		sw_desc->src_num = sw_desc->src_valid_cnt;
	else
		/* Last buffer is for header, not data */
		sw_desc->src_num = sw_desc->src_valid_cnt - 1;

	/* Point to SA desc */
	if (!sa_cached) {
		gvt_comp_device_sa_init(chan, req);
		sw_desc->sa_in.addr = chan->alg_dma_desc;
		sw_desc->sa_in.len = gvt_comp_device_get_alg_desc_size();
	}

	/* Choose HW SA index where the SW SA will be cached */
	if (!req->limit_compression)
		/* Use SA index 0 for no limits */
		sw_desc->sa_indx = alg_index + 0;
	else
		/* Use SA index 1 for limiting compression */
		sw_desc->sa_indx = alg_index + 1;

	return 0;
}

static inline struct gvt_comp_udma_desc *
gvt_comp_udma_desc_get(struct gvt_comp_udma_q *udma_q)
{
	struct gvt_comp_udma_desc *desc;
	uint16_t next_desc_idx;

	next_desc_idx = udma_q->next_desc_idx;
	desc = udma_q->desc_base_ptr + next_desc_idx;

	next_desc_idx++;

	/* if reached end of queue, wrap around */
	udma_q->next_desc_idx = next_desc_idx & udma_q->size_mask;

	return desc;
}

static inline uint32_t gvt_comp_udma_ring_id_get(struct gvt_comp_udma_q *udma_q)
{
	uint32_t ring_id;

	ring_id = udma_q->desc_ring_id;

	/* calculate the ring id of the next desc */
	/* if next_desc points to first desc, then queue wrapped around */
	if (unlikely(udma_q->next_desc_idx == 0))
		udma_q->desc_ring_id = (udma_q->desc_ring_id + 1) &
			GVT_UDMA_RING_ID_MASK;
	return ring_id;
}

static inline void gvt_comp_prep_one_rx_desc(struct gvt_comp_udma_q *rx_udma_q,
					     dma_addr_t addr,
					     uint32_t len)
{
	struct gvt_comp_udma_desc *rx_desc;
	uint32_t len_ctrl;

	rx_desc = gvt_comp_udma_desc_get(rx_udma_q);
	len_ctrl = gvt_comp_udma_ring_id_get(rx_udma_q)
					<< GVT_UDMA_RX_DESC_RING_ID_SHIFT;
	len_ctrl |= len & GVT_UDMA_RX_DESC_LEN_MASK;

	rx_desc->len_ctrl = cpu_to_le32(len_ctrl);
	rx_desc->buf_ptr = cpu_to_le64(addr);
}

static inline struct gvt_comp_udma_desc *
gvt_comp_prep_one_tx_desc(struct gvt_comp_udma_q *tx_udma_q,
			  uint32_t flags,
			  uint32_t meta,
			  dma_addr_t addr,
			  uint32_t len)
{
	struct gvt_comp_udma_desc *tx_desc;
	uint32_t len_ctrl;

	tx_desc = gvt_comp_udma_desc_get(tx_udma_q);
	len_ctrl = gvt_comp_udma_ring_id_get(tx_udma_q)
					<< GVT_UDMA_TX_DESC_RING_ID_SHIFT;
	len_ctrl |= flags | (len & GVT_UDMA_TX_DESC_LEN_MASK);

	tx_desc->len_ctrl = cpu_to_le32(len_ctrl);
	tx_desc->meta_ctrl = cpu_to_le32(meta);
	tx_desc->buf_ptr = cpu_to_le64(addr);

	return tx_desc;
}

static void gtv_comp_set_rx_descs(struct gvt_comp_udma_q *rx_udma_q,
				  struct gvt_comp_sw_desc *sw_desc)
{
	uint32_t buf_idx;

	if (sw_desc->dst_valid_cnt) {
		struct gvt_dma_buf *buf = &sw_desc->dst_bufs[0];

		for (buf_idx = 0; buf_idx < sw_desc->dst_valid_cnt; buf_idx++) {
			gvt_comp_prep_one_rx_desc(rx_udma_q,
						  buf->addr,
						  buf->len);
			buf++;
		}
	}

	if (sw_desc->auth_sign_out.len) {
		gvt_comp_prep_one_rx_desc(rx_udma_q,
				sw_desc->auth_sign_out.addr,
				sw_desc->auth_sign_out.len);
	}
}

static void gvt_comp_set_tx_descs(struct gvt_comp_udma_q *tx_udma_q,
				  struct gvt_comp_sw_desc *sw_desc,
				  uint32_t tx_desc_cnt)
{
	struct gvt_comp_udma_desc *tx_desc;
	uint32_t desc_cnt = tx_desc_cnt;
	uint32_t word1_meta;
	uint32_t flags;

	/* Set flags */
	flags = GVT_UDMA_TX_DESC_FIRST;

	/* Set first desc word1 metadata */
	word1_meta = GVT_UDMA_TX_COMP_OP << GVT_UDMA_TX_DESC_META_OP_SHIFT;
	word1_meta |= sw_desc->dir << GVT_UDMA_TX_DESC_META_CRYPT_DIR_SHIFT;
	word1_meta |= (sw_desc->sa_indx << GVT_UDMA_TX_DESC_META_SA_IDX_SHIFT)
		& GVT_UDMA_TX_DESC_META_SA_IDX_MASK;

	/* Set first desc word1 metadata */
	word1_meta |= GVT_UDMA_TX_DESC_META_CRYPT_GET_ORIG;

	word1_meta |= (sw_desc->auth_sign_out.len) ?
		GVT_UDMA_TX_DESC_META_CRYPT_S_GET_SIGN : 0;

	word1_meta |= GVT_UDMA_TX_DESC_META_CMPRS_HIS_WIN_RST;
	word1_meta |= (sw_desc->auth_sign_in.len) ?
		GVT_UDMA_TX_DESC_META_AUTH_VALID : 0;

	/* prepare descriptors for the SA_in if found */
	if (sw_desc->sa_in.len) {
		/* update buffer type in metadata */
		word1_meta |= (uint32_t)GVT_UDMA_BUF_SA_UPDATE;

		tx_desc = gvt_comp_prep_one_tx_desc(tx_udma_q, flags,
						    word1_meta,
						    sw_desc->sa_in.addr,
						    sw_desc->sa_in.len);

		desc_cnt--;
		if (unlikely(desc_cnt == 0))
			tx_desc->len_ctrl |= cpu_to_le32(GVT_UDMA_TX_DESC_LAST);

		word1_meta = 0;
		flags = 0;
	}

	/* prepare descriptors for the source buffer if found */
	if (likely(sw_desc->src_num)) {
		struct gvt_dma_buf *buf = &sw_desc->src_bufs[0];
		uint32_t buf_idx;

		word1_meta |= (uint32_t)GVT_UDMA_BUF_SRC;

		tx_desc = gvt_comp_prep_one_tx_desc(tx_udma_q, flags,
						    word1_meta,
						    buf->addr,
						    buf->len);

		desc_cnt--;
		if (unlikely(desc_cnt == 0))
			tx_desc->len_ctrl |= cpu_to_le32(GVT_UDMA_TX_DESC_LAST);
		word1_meta = 0;
		flags = 0;
		buf++;

		for (buf_idx = 1; buf_idx < sw_desc->src_num; buf_idx++) {
			tx_desc = gvt_comp_prep_one_tx_desc(
					tx_udma_q,
					flags | GVT_UDMA_TX_DESC_CONCAT,
					word1_meta,
					buf->addr,
					buf->len);

			desc_cnt--;
			if (unlikely(desc_cnt == 0))
				tx_desc->len_ctrl |=
					cpu_to_le32(GVT_UDMA_TX_DESC_LAST);
			buf++;
			/* do not reset flags and meta- they haven't changed */
		}
	}

	/* prepare descriptors for the auth signature if found */
	if (unlikely(sw_desc->auth_sign_in.len)) {
		word1_meta |= (uint32_t)GVT_UDMA_BUF_AUTH_SIGN;

		tx_desc = gvt_comp_prep_one_tx_desc(tx_udma_q, flags,
						    word1_meta,
						    sw_desc->auth_sign_in.addr,
						    sw_desc->auth_sign_in.len);
		desc_cnt--;
		if (unlikely(desc_cnt == 0))
			tx_desc->len_ctrl |= cpu_to_le32(GVT_UDMA_TX_DESC_LAST);
	}

	WARN_ON(desc_cnt);
}

int gvt_comp_device_submit(struct gvt_comp_device *comp_dev,
			   struct gvt_comp_chan *chan,
			   struct gvt_comp_req *req,
			   enum gvt_alg_index alg_index)
{
	struct gvt_comp_udma_q *rx_udma_q, *tx_udma_q;
	struct gvt_comp_engine *gvt_engine;
	struct gvt_comp_sw_desc *sw_desc;
	uint32_t rx_descs, tx_descs;
	unsigned int chan_idx;
	int rc;

	chan_idx = chan->idx;
	gvt_engine = comp_dev->gvt_engine;

	/* Clear sw desc contents */
	sw_desc = chan->sw_desc;
	memset(chan->sw_desc, 0, sizeof(struct gvt_comp_sw_desc));

	/* prepare device transaction */
	rc = prepare_xaction_buffers(comp_dev->dev,
				     gvt_engine,
				     chan,
				     req,
				     sw_desc,
				     alg_index);

	if (unlikely(rc)) {
		GVT_COMP_STATS_INC(chan->stats.hw_desc_err, 1);
		/* We don't mark the device in error state
		 * if user-buffer exceeds hw
		 */
		return rc;
	}

	sw_desc->dir = req->dir;

	/* Add a descriptor for CRC */
	if (req->dir == GVT_COMPRESS) {
		uint32_t hdr_idx = sw_desc->dst_valid_cnt - 1;
		/* During compression, CRC never lands in the right spot
		 * because we always enqueue the header as a data buffer to
		 * make dst_size == src_size.
		 * CRC always lands on first 4 bytes of buffer in the descriptor
		 * after the last used rx data descriptor.
		 */
		sw_desc->auth_sign_out.addr = sw_desc->dst_bufs[hdr_idx].addr;
		sw_desc->auth_sign_out.len = 4;
	} else {
		uint32_t hdr_idx = sw_desc->src_valid_cnt - 1;

		sw_desc->auth_sign_in.addr = sw_desc->src_bufs[hdr_idx].addr +
						GVT_COMP_HDR_CHECKSUM_OFFSET;
		sw_desc->auth_sign_in.len = 4;
	}

	/* Reset channel to default state.
	 * The maximum possible number of Rx cdesc is dst.num + 1
	 */
	rx_udma_q = &gvt_engine->rx_udma.udma_q[chan_idx];
	tx_udma_q = &gvt_engine->tx_udma.udma_q[chan_idx];

	gvt_comp_device_chan_rx_reset(rx_udma_q, sw_desc->dst_valid_cnt + 1);

	/* Prepare dma hw descriptors */
	/* calc tx (M2S) descriptors */
	tx_descs = sw_desc->src_num +
		   (sw_desc->sa_in.len ? 1 : 0) +
		   (sw_desc->auth_sign_in.len ? 1 : 0);

	/* calc rx (S2M) descriptors, at least one desc is required */
	rx_descs = sw_desc->dst_valid_cnt +
		   (sw_desc->auth_sign_out.len ? 1 : 0);

	if (unlikely((tx_descs > GVT_UDMA_MAX_SRC_DESCS) ||
		     (rx_descs > GVT_UDMA_MAX_SRC_DESCS))) {
		GVT_COMP_STATS_INC(chan->stats.hw_desc_err, 1);
		return -ENOSPC;
	}

	/* prepare rx descs */
	gtv_comp_set_rx_descs(rx_udma_q, sw_desc);

	/* add rx descriptors */
	writel(rx_descs,
	       rx_udma_q->shadow_regs + GVT_UDMA_QUEUE_REG_DRTP_INC_OFF);

	/* prepare tx descriptors */
	gvt_comp_set_tx_descs(tx_udma_q, sw_desc, tx_descs);

	writel(tx_descs,
	       tx_udma_q->shadow_regs + GVT_UDMA_QUEUE_REG_DRTP_INC_OFF);

	if (req->dir == GVT_COMPRESS) {
		GVT_COMP_STATS_INC(chan->stats.comp_reqs, 1);
		GVT_COMP_STATS_INC(chan->stats.comp_input_bytes, req->slen);
	} else {
		GVT_COMP_STATS_INC(chan->stats.decomp_reqs, 1);
		GVT_COMP_STATS_INC(chan->stats.decomp_input_bytes, req->slen);
	}

	return 0;
}

static void get_crc_word(struct gvt_dma_buf *bufs,
			 unsigned int data_len,
			 unsigned int hdr_idx,
			 uint32_t *crc_offset)
{
	struct gvt_dma_buf *buf;
	bool found = false;
	int cur_len = 0;
	int i;

	for (i = 0; i < GVT_UDMA_MAX_SRC_DESCS - 1; i++) {
		buf = &bufs[i];
		cur_len += buf->len;
		if (cur_len >= data_len) {
			i++;
			found = true;
			break;
		}
	}

	if (i == hdr_idx)
		*crc_offset = 0;
	else
		*crc_offset = cur_len + GVT_COMP_HDR_LEN;
}

static void gvt_comp_device_err(struct gvt_comp_device *comp_dev,
				unsigned int chan_idx)
{
	unsigned int cur_err_channel;
	struct device *dev;

	dev = comp_dev->dev;

	/* Channels are numbered from 0 to num_channels, however,
	 * when error is marked with channel number,
	 * we increment channel idx by 1 and write to error status variable
	 * to distinguish from no-error state whose value is 0.
	 * Also, we use compare-and-swap atomic op so only one channel
	 * could update this variable.
	 */
	cur_err_channel = atomic_cmpxchg(&comp_dev->error, 0, chan_idx + 1);

	if (!cur_err_channel) {
		dev_err(dev, "Device fatal error on chan #: %u\n", chan_idx);
		dev_err(dev, "Remove and re-insert driver to recover\n");
		GVT_COMP_STATS_INC(comp_dev->stats.device_err, 1);
	}
}

static inline bool gvt_comp_udma_new_cdesc(struct gvt_comp_udma_q *udma_q,
					   uint32_t flags)
{
	const uint32_t ring_id = (flags >> GVT_UDMA_TX_DESC_RING_ID_SHIFT)
					& GVT_UDMA_TX_DESC_RING_ID_MASK;
	return (ring_id == udma_q->comp_ring_id);
}

static inline struct gvt_comp_udma_cdesc *
gvt_comp_cdesc_next_update(struct gvt_comp_udma_q *udma_q,
			   struct gvt_comp_udma_cdesc *cdesc)
{
	/* if last desc, wrap around */
	if (unlikely(((uint8_t *) cdesc == udma_q->end_cdesc_ptr))) {
		udma_q->comp_ring_id =
		    (udma_q->comp_ring_id + 1) & GVT_UDMA_RING_ID_MASK;
		return (struct gvt_comp_udma_cdesc *) udma_q->cdesc_base_ptr;
	}
	return (struct gvt_comp_udma_cdesc *)((uint8_t *)cdesc
							+ udma_q->cdesc_size);
}

static uint32_t gvt_comp_udma_cdesc_packet_get(
					struct gvt_comp_udma_q *udma_q,
					struct gvt_comp_udma_cdesc **cdesc)
{
	struct gvt_comp_udma_cdesc *curr;
	uint32_t comp_flags;
	uint32_t count;

	/* comp_head points to the last comp desc that was processed */
	curr = udma_q->comp_head_ptr;
	comp_flags = le32_to_cpu(READ_ONCE(curr->word0));

	/* check if the completion descriptor is new */
	if (unlikely(!gvt_comp_udma_new_cdesc(udma_q, comp_flags)))
		return 0;

	/* if new desc found, increment the current packets descriptors */
	count = udma_q->pkt_crnt_descs + 1;
	while (!(comp_flags & GVT_UDMA_CDESC_LAST)) {
		curr = gvt_comp_cdesc_next_update(udma_q, curr);
		comp_flags = le32_to_cpu(READ_ONCE(curr->word0));
		if (unlikely(!gvt_comp_udma_new_cdesc(udma_q, comp_flags))) {
			/* the current packet here doesn't have all  */
			/* descriptors completed. log the current desc */
			/* location and number of completed descriptors so */
			/*  far. then return */
			udma_q->pkt_crnt_descs = count;
			udma_q->comp_head_ptr = curr;
			return 0;
		}
		count++;
		/* check against max descs per packet. */
		BUG_ON(count > udma_q->size);
	}
	/* return back the first descriptor of the packet */
	*cdesc = gvt_comp_udma_cdesc_idx_to_ptr(udma_q, udma_q->next_cdesc_idx);
	udma_q->pkt_crnt_descs = 0;
	udma_q->comp_head_ptr = gvt_comp_cdesc_next_update(udma_q, curr);

	return count;
}

static inline struct gvt_comp_udma_cdesc *
gvt_comp_cdesc_next(struct gvt_comp_udma_q *udma_q,
		    struct gvt_comp_udma_cdesc	*cdesc,
		    uint32_t offset)
{
	uint8_t *tmp = (uint8_t *)cdesc + offset * udma_q->cdesc_size;

	/* if wrap around */
	if (unlikely((tmp > udma_q->end_cdesc_ptr)))
		return (struct gvt_comp_udma_cdesc *)
			(udma_q->cdesc_base_ptr +
			(tmp - udma_q->end_cdesc_ptr - udma_q->cdesc_size));

	return (struct gvt_comp_udma_cdesc *)tmp;
}

int gvt_comp_device_poll_comp(struct gvt_comp_device *comp_dev,
			      struct gvt_comp_chan *chan,
			      struct gvt_comp_req *req,
			      enum gvt_alg_index alg_index)
{
	struct gvt_comp_udma_cdesc *cdesc_first, *cdesc_last;
	struct gvt_comp_udma_cdesc *rx_cdesc = NULL;
	struct gvt_comp_engine *gvt_engine;
	struct gvt_comp_udma_q *rx_udma_q;
	struct gvt_comp_sw_desc *sw_desc;

	unsigned long max_wait_time;
	unsigned int result_len = 0;

	uint32_t comp_status = 0;
	uint32_t cdesc_count = 0;
	struct gvt_comp_hdr *hdr;
	unsigned int chan_idx;
	uint32_t crc_offset;
	struct device *dev;
	int rc = 0;

	dev = comp_dev->dev;
	chan_idx = chan->idx;
	sw_desc = chan->sw_desc;
	gvt_engine = comp_dev->gvt_engine;

	rx_udma_q = &gvt_engine->rx_udma.udma_q[chan_idx];

	/* Wait for completion before exiting error */
	max_wait_time = jiffies +
				msecs_to_jiffies(gvt_comp_get_req_timeout_ms());
	do {
		cdesc_count =
			gvt_comp_udma_cdesc_packet_get(rx_udma_q, &cdesc_first);
		/* We can't sleep/schedule here because compress(), decompress()
		 * are called in atomic context from zram/zswap.
		 */
	} while (!cdesc_count && time_before(jiffies, max_wait_time));

	if (unlikely(!cdesc_count)) {
		dev_err(dev, "Timeout: No completions in chan idx=%u\n",
			     chan_idx);
		rc = -EIO;
		goto err;
	}

	dev_dbg(dev, "cdesc_count=%u\n", cdesc_count);

	/* if we have multiple completion descriptors,
	 * then last one has the valid status
	 */
	cdesc_last = cdesc_first;
	if (unlikely(cdesc_count > 1))
		cdesc_last = gvt_comp_cdesc_next(rx_udma_q,
						 cdesc_first,
						 cdesc_count - 1);

	comp_status = le32_to_cpu(READ_ONCE(cdesc_last->word0));

	dev_dbg(dev,
		"comp packet completed in chan idx %u. last cdesc status %x\n",
		chan_idx, comp_status);

	if (unlikely(comp_status & GVT_COMP_ERROR_MASK)) {
		if (comp_status & GVT_COMP_AUTH_ERROR) {
			dev_err(dev, "crc error; source buffer corrupted in chan %d\n",
				     chan_idx);
			rc = -EINVAL;
		} else {
			dev_err(dev, "error on completion status: 0x%x in chan %d\n",
				     comp_status, chan_idx);
			rc = -EIO;
		}
		goto ack;
	}

	if ((req->dir == GVT_COMPRESS) &&
	    (comp_status & GVT_COMP_CMPRS_STATUS)) {
		dev_dbg(dev, "data incompressible in chan idx %u\n", chan_idx);
		req->result_len = req->slen;
		rc = -EFBIG;
		goto ack;
	} else {
		rx_cdesc = (struct gvt_comp_udma_cdesc *)cdesc_last;

		if (req->dir == GVT_COMPRESS) {
			result_len = (le32_to_cpu(READ_ONCE(rx_cdesc->word1)) &
				     GVT_COMP_RESULT_SIZE_CDESC_W1_MASK) |
				     (le32_to_cpu(READ_ONCE(rx_cdesc->word2)) &
				     GVT_COMP_RESULT_SIZE_CDESC_W2_MASK);

			/* There is a corner case where even though compression
			 * is successful, the result size encroaches into the
			 * header space. We return error in that case.
			 */
			if (req->limit_compression &&
			    (result_len > (req->slen - GVT_COMP_HDR_LEN))) {
				dev_dbg(dev, "data incompressible, no space for header in chan idx %u\n",
						chan_idx);
				req->result_len = req->slen;
				rc = -EFBIG;
				goto ack;
			}

			get_crc_word(sw_desc->dst_bufs,
				     result_len,
				     sw_desc->dst_valid_cnt - 1,
				     &crc_offset);

			hdr = (struct gvt_comp_hdr *)req->user_dst;
			hdr->data_checksum = *(uint32_t *)
					      (req->user_dst+crc_offset);
		} else {
			result_len = le32_to_cpu(READ_ONCE(rx_cdesc->word3)) &
				     GVT_DECOMP_RESULT_SIZE_CDESC_W3_MASK;
		}

		if (req->dir == GVT_COMPRESS) {
			req->result_len = result_len + GVT_COMP_HDR_LEN;
			GVT_COMP_STATS_INC(chan->stats.comp_output_bytes,
					   req->result_len);
		} else {
			req->result_len = result_len;
			GVT_COMP_STATS_INC(chan->stats.decomp_output_bytes,
					   req->result_len);
		}
	}

ack:
	/* Ack processing of completion to hw */
	rx_udma_q->next_cdesc_idx = (rx_udma_q->next_cdesc_idx + cdesc_count) &
				    rx_udma_q->size_mask;

	/* Mark SA is cached given successful transaction */
	if (req->limit_compression)
		gvt_engine->limit_sa_cached[alg_index] = true;
	else
		gvt_engine->sa_cached[alg_index] = true;

	goto done;

err:
	GVT_COMP_STATS_INC(chan->stats.completion_err, 1);
	gvt_comp_device_err(comp_dev, chan_idx);

done:
	dma_unmap_sg(dev, req->src, req->src_nents, DMA_TO_DEVICE);
	dma_unmap_sg(dev, req->dst, req->dst_nents, DMA_FROM_DEVICE);

	return rc;
}

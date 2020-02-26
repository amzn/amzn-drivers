/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#ifndef __GVT_COMPRESS_H__
#define __GVT_COMPRESS_H__

#include <linux/pci.h>
#include <crypto/algapi.h>
#include "gvt_compress_hdr.h"

#define DEVICE_NAME	"AWS Graviton2 Compression Engine"
#define DRV_NAME	"gvt_compress"
#define DRV_VERSION	"1.0.0"

/* Driver wait time:
 * a) to assign a channel to a transaction request before returning error
 * b) for channel submission to complete before device marked in error state
 */
#define GVT_MAX_TIMEOUT_WAIT_MS	100

/* Device and Alg names string length */
#define GVT_MAX_NAME_LENGTH	CRYPTO_MAX_ALG_NAME

#define GVT_MAX_CHANNELS	16

/* Enum to indicate compression or decompression operation */
enum gvt_comp_decomp {
	GVT_COMPRESS	=	0,
	GVT_DECOMPRESS	=	1,
};

/* Supported hardware accelerated algorithms */
enum gvt_alg_index {
	GVT_DEFLATE = 0,
	MAX_ALG_PARAM_INDEX,
};

enum gvt_comp_level {
	GVT_COMP_LEVEL_1 = 1,
	GVT_COMP_LEVEL_2 = 2,
	GVT_COMP_LEVEL_3 = 3,
	GVT_COMP_LEVEL_4 = 4,
	GVT_COMP_LEVEL_5 = 5,
};

#define GVT_COMP_STATS_INC(var, incval)	((var) += (incval))

/* Sysfs compression/decompression device stats structure */
struct gvt_comp_device_stats {
	/* Number of compression user errors */
	uint64_t		comp_user_err;
	/* Number of decompression user errors */
	uint64_t		decomp_user_err;
	/* Number of channel unavailable errors */
	uint64_t		chan_unavail_err;
	/* Device fatal error */
	uint64_t		device_err;
};

/* Sysfs compression/decompression channel stats structure */
struct gvt_comp_chan_stats {
	/* Any errors encountered in preparing hw descriptors */
	uint64_t		hw_desc_err;
	/* Software errors due to some sw inconsistency */
	uint64_t		sw_err;
	/* Errors when completions not received */
	uint64_t		completion_err;

	/* Number of compression requests */
	uint64_t		comp_reqs;
	/* Number of input bytes for compression */
	uint64_t		comp_input_bytes;
	/* Number of compressed bytes (output of compression) */
	uint64_t		comp_output_bytes;

	/* Number of de-compression requests */
	uint64_t		decomp_reqs;
	/* Number of compressed bytes (input to decompression) */
	uint64_t		decomp_input_bytes;
	/* Number of output bytes from decompression */
	uint64_t		decomp_output_bytes;
};

/* Compression device structure - device registers, channels, algorithms */
struct gvt_comp_device {
	/* PCI info */
	struct pci_dev		*pdev;
	struct device		*dev;
	u16			vendor_id;
	u16			dev_id;
	u8			rev_id;
	void __iomem		*dma_regs_base;
	void __iomem		*engine_regs_base;

	/* Device error status */
	atomic_t		error;

	/* Channels supported */
	struct gvt_comp_chan	*channels;
	int			num_channels;
	DECLARE_BITMAP(channel_use_bitmap, GVT_MAX_CHANNELS);
	struct kset		*channels_kset;

	/* Common channel attributes */
	uint32_t		hw_descs_num;
	uint32_t		hw_desc_size;
	uint32_t		hw_cdesc_size;
	uint32_t		sw_desc_size;
	uint32_t		alg_desc_size;

	/* List of crypto algorithms supported by device channels */
	struct list_head	alg_list;

	/* Pointer to internal device engine */
	void			*gvt_engine;

	/* Device stats */
	struct gvt_comp_device_stats	stats;

	/* Sysfs object for stats */
	struct kobject			kobj;
};

/* Channel structure - hw, sw, alg descriptors & stats */
struct gvt_comp_chan {
	/* Channel index */
	int			idx;

	/* Pointer to device structure */
	struct gvt_comp_device	*comp_dev;

	/* Channel stats */
	struct gvt_comp_chan_stats	stats;

	/* Sysfs object for stats */
	struct kobject		kobj;

	/* Tx (submission) DMA descriptors */
	void			*tx_dma_desc_virt;
	dma_addr_t		tx_dma_desc;

	/* Rx (submission) DMA descriptors */
	void			*rx_dma_desc_virt;
	dma_addr_t		rx_dma_desc;

	/* Rx (completion) DMA descriptors */
	void			*rx_dma_cdesc_virt;
	dma_addr_t		rx_dma_cdesc;

	/* Alg DMA descriptor */
	void			*alg_dma_desc_virt;
	dma_addr_t		alg_dma_desc;

	/* Pointer to internal device SW descriptor */
	void			*sw_desc;

	/* Incoming src and dst addresses
	 * will be converted to sg_table.
	 * This struct is here to avoid runtime allocation
	 * for every request.
	 */
	struct sg_table		src_sgt;
	struct sg_table		dst_sgt;
};

/* Runtime compress/decompress request structure describing data locations */
struct gvt_comp_req {
	/* Direction - compression or decompression */
	enum gvt_comp_decomp	dir;

	/* Channel used for this request */
	struct gvt_comp_chan	*chan;

	/* Incoming src and dst */
	u8			*user_src;
	u8			*user_dst;

	/* Scatterlist pointers from sg_table in chan */
	struct scatterlist	*src;
	struct scatterlist	*dst;
	unsigned int		src_nents;
	unsigned int		dst_nents;

	/* Incoming request src and dst len */
	unsigned int		slen;
	unsigned int		dlen;

	/* Result length after operation */
	uint32_t		result_len;

	/* Flag to limit compress len to src len */
	uint8_t			limit_compression;
};

/* Runtime compression/decompression session context structure
 * allocated by the crypto subsystem
 */
struct gvt_comp_ctx {
	struct gvt_comp_device	*comp_dev;
	enum gvt_alg_index	alg_index;

	/* Request structure updated on every
	 * compression or decompression operation.
	 */
	struct gvt_comp_req	comp_req;
};

/* module param read function */
int gvt_comp_get_compress_level(void);
int gvt_comp_get_req_timeout_ms(void);

/* core init functions */
int gvt_comp_core_init(struct gvt_comp_device *comp_dev);
void gvt_comp_core_terminate(struct gvt_comp_device *comp_dev);

/* alg init functions */
int gvt_comp_alg_init(struct gvt_comp_device *comp_dev);
void gvt_comp_alg_terminate(struct gvt_comp_device *comp_dev);

/* sysfs init functions */
int gvt_comp_sysfs_init(struct gvt_comp_device *comp_dev);
void gvt_comp_sysfs_terminate(struct gvt_comp_device *comp_dev);

#endif	/* __GVT_COMPRESS_H__ */

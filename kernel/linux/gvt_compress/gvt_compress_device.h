/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#ifndef __GVT_COMPRESS_DEVICE_H__
#define __GVT_COMPRESS_DEVICE_H__

#include "gvt_compress.h"

/* Hardware specific constants */
#define GVT_DEVICE_MAX_SA		16
#define GVT_DEVICE_MAX_HW_DESCS		32
#define GVT_DEVICE_MIN_PKT_SIZE		(8)
#define GVT_DEVICE_MAX_PKT_SIZE		(64 * 1024)
#define GVT_DEVICE_SA_SIZE		(256)

#define GVT_COMP_SG_ENT_MAX		(DIV_ROUND_UP(GVT_DEVICE_MAX_PKT_SIZE, \
						PAGE_SIZE) + 1)
/**
 * Compression profile ID specifying the compression variant performed
 */
enum gvt_comp_prof_id {
	GVT_COMP_DEFLATE_1KB_HISTORY_100_NORMAL = 0,
	GVT_COMP_DEFLATE_4KB_HISTORY_100_NORMAL = 1,
	GVT_COMP_DEFLATE_8KB_HISTORY_100_NORMAL = 2,
	GVT_COMP_DEFLATE_16KB_HISTORY_100_NORMAL = 3,
	GVT_COMP_DEFLATE_32KB_HISTORY_100_NORMAL = 4,
};

/**
 * Inline CRC Profile ID
 */
enum gvt_comp_icrc_prof_id {
	GVT_COMP_ICRC_PROF_ID_CRC16 = 0,
	GVT_COMP_ICRC_PROF_ID_CRC32 = 1,
	GVT_COMP_ICRC_PROF_ID_ADLER = 2,
};

/* Return internal device structures sizes */
uint32_t gvt_comp_device_get_gvt_engine_size(void);
uint32_t gvt_comp_device_get_sw_desc_size(void);
uint32_t gvt_comp_device_get_hw_desc_size(void);
uint32_t gvt_comp_device_get_hw_cdesc_size(void);
uint32_t gvt_comp_device_get_alg_desc_size(void);

/* Device reset */
int gvt_comp_device_reset(struct gvt_comp_device *comp_dev);

/* Device init */
int gvt_comp_device_init(struct gvt_comp_device *comp_dev);

/* Compression engine init */
void gvt_comp_engine_init(void __iomem *app_regs);

/* Unit adapter init */
int gvt_unit_adapter_init(struct pci_dev *pdev);

/* Device terminate */
void gvt_comp_device_terminate(struct gvt_comp_device *comp_dev);

/* Submit work to hw in sw desc */
int gvt_comp_device_submit(struct gvt_comp_device *comp_dev,
			   struct gvt_comp_chan *chan,
			   struct gvt_comp_req *req,
			   enum gvt_alg_index alg_index);

/* Wait/Poll for completion of work */
int gvt_comp_device_poll_comp(struct gvt_comp_device *comp_dev,
			   struct gvt_comp_chan *chan,
			   struct gvt_comp_req *req,
			   enum gvt_alg_index alg_index);

#endif	/* __GVT_COMPRESS_DEVICE_H__ */

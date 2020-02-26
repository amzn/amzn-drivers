/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#ifndef __GVT_COMPRESS_HDR_H__
#define __GVT_COMPRESS_HDR_H__

/**
 * Compress data header
 */
struct gvt_comp_hdr {
	/* Header magic */
	uint32_t	hdr_magic;
	/* Header version */
	uint8_t		hdr_ver;
	/* Hardware version */
	uint8_t		hw_ver;
	/* Software version */
	uint16_t	sw_ver;
	/* Header checksum */
	uint16_t	hdr_checksum;
	/* Compression profile id */
	uint16_t	profile_id;
	/* Checksum over uncompressed data */
	uint32_t	data_checksum;
	/* Uncompressed data length */
	uint32_t	data_len;
	/* Compressed data length (including header) */
	uint32_t	compress_len;
	uint8_t		rsvd[8];
} __packed;

enum gvt_hw_ver {
	GVT_HW_VER_GRAVITON2 = 2,
};

#define GVT_COMP_HDR_MAGIC		0x43545647 /* GVTC */
#define GVT_COMP_SW_VERSION		0x4000
#define GVT_COMP_HDR_VERSION		0
#define GVT_COMP_HDR_LEN		32UL
#define GVT_COMP_HDR_CHECKSUM_LEN	4UL

#define GVT_COMP_HDR_CHECKSUM_OFFSET \
	offsetof(struct gvt_comp_hdr, data_checksum)

/* Graviton compress bound */
#define GVT_COMPRESS_BOUND(in_len) \
	((in_len) + ((in_len) >> 3) + GVT_COMP_HDR_LEN)

static inline uint16_t comp_hdr_checksum(uint8_t *addr, uint16_t len)
{
	uint32_t csum = 0;

	while (len > 1) {
		csum += *(uint16_t *) addr;
		addr += 2;
		len -= 2;
	};

	/* add remaining byte, if any */
	if (len > 0)
		csum += *(uint8_t *) addr;

	while (csum >> 16)
		csum = (csum & 0xffff) + (csum >> 16);

	return ~csum;
}

#endif	/* __GVT_COMPRESS_HDR_H__ */

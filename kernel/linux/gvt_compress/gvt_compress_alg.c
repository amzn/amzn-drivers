// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include <linux/delay.h>

#include "gvt_compress.h"
#include "gvt_compress_device.h"

static char alg_name_prefixes[][GVT_MAX_NAME_LENGTH] = {
	[GVT_DEFLATE] = "gvt_compress",
};

/* List of algorithms supported by the device */
struct gvt_comp_crypto_alg {
	/* List of supported algorithms */
	struct list_head entry;

	/* Register this with crypto subsystem */
	struct crypto_alg crypto_alg;

	/* Pointer to device */
	struct gvt_comp_device *comp_dev;

	/* Index into internal device parameters for alg */
	enum gvt_alg_index alg_index;

	/* Alg name */
	char alg_name[GVT_MAX_NAME_LENGTH];
};

void gvt_comp_alg_terminate(struct gvt_comp_device *comp_dev)
{
	struct gvt_comp_crypto_alg *gvt_alg, *n;
	struct device *dev;

	dev = comp_dev->dev;

	if (!comp_dev->alg_list.next)
		return;

	list_for_each_entry_safe(gvt_alg, n, &comp_dev->alg_list, entry) {
		crypto_unregister_alg(&gvt_alg->crypto_alg);
		list_del(&gvt_alg->entry);
	}
}

static int gvt_comp_cra_init_compress(struct crypto_tfm *tfm)
{
	struct gvt_comp_crypto_alg *gvt_comp_alg;
	struct gvt_comp_device *comp_dev;
	struct gvt_comp_ctx *ctx;
	struct crypto_alg *alg;
	struct device *dev;

	alg = tfm->__crt_alg;
	gvt_comp_alg = container_of(alg,
				    struct gvt_comp_crypto_alg, crypto_alg);
	comp_dev = gvt_comp_alg->comp_dev;
	dev = comp_dev->dev;

	if (unlikely(atomic_read(&comp_dev->error))) {
		dev_err_once(dev, "%s: Device fatal error. Remove and re-insert driver.\n",
			      __func__);
		return -EIO;
	}

	ctx = crypto_tfm_ctx(tfm);

	/* This was allocated outside the driver by crypto subsystem,
	 * so lets clear it so no stale data
	 */
	memset(ctx, 0, sizeof(struct gvt_comp_ctx) +
		       gvt_comp_device_get_hw_cdesc_size());

	ctx->comp_dev = comp_dev;
	ctx->alg_index = gvt_comp_alg->alg_index;

	dev_dbg(dev, "%s init\n", __func__);

	return 0;
}

static void gvt_comp_cra_exit_compress(struct crypto_tfm *tfm)
{
	/* nothing to do here */
}

static void populate_sg_table(struct sg_table *sgt,
			      unsigned int nents,
			      u8 *buf,
			      unsigned int buf_len)
{
	unsigned long buf_addr, size, remain;
	struct scatterlist *sg_ptr;
	struct page *page;
	unsigned int i;

	buf_addr = (unsigned long)buf;
	remain = (unsigned long)buf_len;

	for_each_sg(sgt->sgl, sg_ptr, nents, i) {
		size = min(remain, PAGE_SIZE - offset_in_page(buf_addr));

		if (is_vmalloc_addr((void *)buf_addr))
			page = vmalloc_to_page((void *)buf_addr);
		else
			page = virt_to_page((void *)buf_addr);

		sg_set_page(sg_ptr, page, (unsigned int)size,
			    offset_in_page(buf_addr));

		buf_addr += size;
		remain -= size;
	}
}

static inline bool find_available_channel(struct gvt_comp_device *comp_dev,
					  unsigned int *ret_chan_idx)
{
	unsigned int chan_idx;
	struct device *dev;
	bool ret;

	dev = comp_dev->dev;

	chan_idx = raw_smp_processor_id() % comp_dev->num_channels;

	dev_dbg(dev, "Try to claim channel %u\n", chan_idx);

	/* This returns old value before setting the bit */
	ret = test_and_set_bit(chan_idx, comp_dev->channel_use_bitmap);
	if (!ret) {
		dev_dbg(dev, "Claimed channel %u\n", chan_idx);
		*ret_chan_idx = chan_idx;
	}

	return !ret;
}

static int perform_comp_decomp(struct crypto_tfm *tfm,
			       enum gvt_comp_decomp dir,
			       const u8 *src,
			       unsigned int slen,
			       u8 *dst,
			       unsigned int *dlen,
			       bool limit_compression)
{
	struct gvt_comp_device *comp_dev;
	unsigned long max_wait_time;
	struct gvt_comp_chan *chan;
	struct gvt_comp_ctx *ctx;
	struct gvt_comp_req *req;
	unsigned int chan_idx;
	struct device *dev;
	bool chan_found;
	int rc = 0;
	int err;

	ctx = crypto_tfm_ctx(tfm);
	req = &ctx->comp_req;
	comp_dev = ctx->comp_dev;
	dev = comp_dev->dev;

	if (unlikely(atomic_read(&comp_dev->error))) {
		dev_err_once(dev, "%s: Device fatal error. Remove and re-insert driver.\n",
			      __func__);
		return -EIO;
	}

	/* Look for an available channel and
	 * claim it when it becomes available.
	 */
	max_wait_time = jiffies +
				msecs_to_jiffies(gvt_comp_get_req_timeout_ms());
	do {
		chan_found = find_available_channel(comp_dev, &chan_idx);
		if (!chan_found)
			cpu_relax();
	} while (!chan_found && time_before(jiffies, max_wait_time));

	if (unlikely(!chan_found)) {
		dev_err(dev, "Timeout: channel not available\n");
		rc = -EIO;
		GVT_COMP_STATS_INC(comp_dev->stats.chan_unavail_err, 1);
		goto err_no_chan;
	}

	chan = &comp_dev->channels[chan_idx];

	/* Clear any stale data from older transactions */
	memset(req, 0, sizeof(struct gvt_comp_req));

	req->dir = dir;
	req->chan = chan;
	req->limit_compression = limit_compression;

	dev_dbg(dev, "%s: channel #%u chosen for %s, mode: %s, bitmap:0x%lx\n",
		       __func__,
			chan_idx, dir ? "decompression" : "compression",
			limit_compression ? "limit" : "no limits",
			*comp_dev->channel_use_bitmap);

	req->src_nents = DIV_ROUND_UP(offset_in_page(src) + slen, PAGE_SIZE);
	req->dst_nents = DIV_ROUND_UP(offset_in_page(dst) + *dlen, PAGE_SIZE);

	if (unlikely((req->src_nents > GVT_COMP_SG_ENT_MAX) ||
		     (req->dst_nents > GVT_COMP_SG_ENT_MAX))) {
		dev_err(dev, "sg entries exceeded limit (%lu) src %u dst %u\n",
			GVT_COMP_SG_ENT_MAX, req->src_nents, req->dst_nents);
		rc = -EIO;
		goto done;
	}

	populate_sg_table(&chan->src_sgt, req->src_nents, (u8 *)src, slen);
	populate_sg_table(&chan->dst_sgt, req->dst_nents, dst, *dlen);

	req->user_src = (u8 *)src;
	req->user_dst = dst;
	req->slen = slen;
	req->dlen = *dlen;
	req->src = chan->src_sgt.sgl;
	req->dst = chan->dst_sgt.sgl;

	rc = gvt_comp_device_submit(comp_dev, chan, req, ctx->alg_index);
	if (unlikely(rc)) {
		dev_err(dev, "%s: gvt_comp_device_submit failed!\n",
			      __func__);
		goto done;
	}

	rc = gvt_comp_device_poll_comp(comp_dev, chan, req, ctx->alg_index);
	if (unlikely(rc)) {
		dev_dbg(dev, "%s: gvt_comp_device_poll_comp failed!\n",
			      __func__);
		goto done;
	}

	*dlen = req->result_len;

done:
	/* Clear channel usage */
	err = test_and_clear_bit(chan_idx, comp_dev->channel_use_bitmap);
	if (unlikely(err != 1)) {
		dev_err(dev, "%s: Expected chan #%u to be enabled, bitmap: 0x%lx\n",
			      __func__, chan_idx,
			      *comp_dev->channel_use_bitmap);
		GVT_COMP_STATS_INC(chan->stats.sw_err, 1);
	}

	dev_dbg(dev, "%s: channel #%u closed after %s, mode: %s, bitmap:0x%lx\n",
		       __func__,
			chan_idx, dir ? "decompression" : "compression",
			req->limit_compression ? "limit" : "no limits",
			*comp_dev->channel_use_bitmap);
err_no_chan:
	return rc;
}

static int gvt_compress(struct crypto_tfm *tfm,
		       const u8 *src,
		       unsigned int slen,
		       u8 *dst,
		       unsigned int *dlen)
{
	struct gvt_comp_device *comp_dev;
	struct gvt_comp_hdr *hdr;
	struct gvt_comp_ctx *ctx;
	bool limit_compression;
	unsigned int comp_len;
	struct device *dev;
	int rc;

	if (unlikely(!tfm || !src || !dst))
		return -EINVAL;

	ctx = crypto_tfm_ctx(tfm);
	comp_dev = ctx->comp_dev;
	dev = comp_dev->dev;
	hdr = (struct gvt_comp_hdr *)dst;
	comp_len = *dlen;

	if (unlikely(slen < GVT_DEVICE_MIN_PKT_SIZE)) {
		dev_err(dev, "%s: source buffer %u less than min size %u\n",
			     __func__, slen, GVT_DEVICE_MIN_PKT_SIZE);
		goto user_err;
	}

	if (unlikely(slen > *dlen)) {
		dev_err(dev,
			"%s: target buffer size %u less than source buffer size %u\n",
			__func__, *dlen, slen);
		goto user_err;
	}

	if (unlikely(*dlen > GVT_DEVICE_MAX_PKT_SIZE)) {
		dev_err(dev,
			"%s: target buffer size %u exceeds hw limit %u\n",
			__func__, *dlen, GVT_DEVICE_MAX_PKT_SIZE);
		goto user_err;
	}

	memset(hdr, 0, GVT_COMP_HDR_LEN);

	/* If dlen is greater than compress bound,
	 * don't set flag to limit compression
	 */
	if (*dlen >= GVT_COMPRESS_BOUND(slen))
		limit_compression = false;
	else
		limit_compression = true;

	rc = perform_comp_decomp(tfm, GVT_COMPRESS,
				 src, slen, dst, &comp_len,
				 limit_compression);

	/* Save metadata in header */
	if (!rc) {
		hdr->hdr_magic = GVT_COMP_HDR_MAGIC;
		hdr->sw_ver = GVT_COMP_SW_VERSION;
		hdr->hw_ver = GVT_HW_VER_GRAVITON2;
		hdr->data_len = slen;
		hdr->compress_len = comp_len;
		hdr->hdr_checksum = comp_hdr_checksum((uint8_t *)hdr,
					GVT_COMP_HDR_LEN);
		*dlen = hdr->compress_len;

		dev_dbg(dev, "%s: crc32: 0x%x\n", __func__, hdr->data_checksum);
		dev_dbg(dev, "%s:  csum: 0x%x\n", __func__, hdr->hdr_checksum);
	}

	return rc;

user_err:
	GVT_COMP_STATS_INC(comp_dev->stats.comp_user_err, 1);
	return -EINVAL;
}

static int gvt_decompress(struct crypto_tfm *tfm,
			  const u8 *src,
			  unsigned int slen,
			  u8 *dst,
			  unsigned int *dlen)
{
	struct gvt_comp_device *comp_dev;
	struct gvt_comp_hdr *hdr;
	struct gvt_comp_ctx *ctx;
	uint32_t hdr_checksum;
	struct device *dev;
	int rc;

	if (unlikely(!tfm || !src || !dst))
		return -EINVAL;

	ctx = crypto_tfm_ctx(tfm);
	comp_dev = ctx->comp_dev;
	dev = comp_dev->dev;
	hdr = (struct gvt_comp_hdr *)src;

	if (unlikely(slen < GVT_COMP_HDR_LEN)) {
		dev_err(dev,
			"%s: Source buffer invalid slen %d\n",
			__func__, slen);
		goto user_err;
	}

	if (unlikely((*dlen > GVT_DEVICE_MAX_PKT_SIZE) ||
		     (slen > GVT_DEVICE_MAX_PKT_SIZE))) {
		dev_err(dev,
			"%s: buffer size %u:%u exceeds hw limit %u\n",
			__func__, slen, *dlen, GVT_DEVICE_MAX_PKT_SIZE);
		goto user_err;
	}

	if (unlikely(hdr->hdr_magic != GVT_COMP_HDR_MAGIC)) {
		dev_err(dev,
			"%s: Magic number failure, source buffer corrupted\n",
			__func__);
		goto user_err;
	}

	dev_dbg(dev, "%s: crc32: 0x%x\n", __func__, hdr->data_checksum);
	dev_dbg(dev, "%s:  csum: 0x%x\n", __func__, hdr->hdr_checksum);

	hdr_checksum = comp_hdr_checksum((uint8_t *)hdr, GVT_COMP_HDR_LEN);

	if (unlikely(hdr_checksum != 0)) {
		dev_err(dev,
			"%s: Header checksum failure, source buffer corrupted\n",
			__func__);
		goto user_err;
	}

	if (unlikely(hdr->compress_len < slen)) {
		dev_err(dev,
			"%s: Source buffer provided is smaller than expected\n",
			__func__);
		goto user_err;
	}

	if (unlikely(hdr->data_len > *dlen)) {
		dev_err(dev,
			"%s: Target buffer provided is smaller than expected\n",
			__func__);
		goto user_err;
	}

	rc = perform_comp_decomp(tfm, GVT_DECOMPRESS,
				 src, slen, dst, dlen,
				 false);

	return rc;

user_err:
	GVT_COMP_STATS_INC(comp_dev->stats.decomp_user_err, 1);
	return -EINVAL;
}

/* Kernel Crypto Subsystem Interface */
static struct crypto_alg driver_algs[] = {
	/* compression/decompression supported algo are listed here */
	[GVT_DEFLATE] = {
			.cra_driver_name	= DRV_NAME,
			.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
			.cra_priority           = 500,
			.cra_module		= THIS_MODULE,
			.cra_init		= gvt_comp_cra_init_compress,
			.cra_exit		= gvt_comp_cra_exit_compress,
			.cra_u			= { .compress = {
				.coa_compress	= gvt_compress,
				.coa_decompress	= gvt_decompress
			} }
		},
};

int gvt_comp_alg_init(struct gvt_comp_device *comp_dev)
{
	struct device *dev;
	int rc = 0;
	int i;

	dev = comp_dev->dev;

	INIT_LIST_HEAD(&comp_dev->alg_list);

	/* register crypto algorithms the device supports */
	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct gvt_comp_crypto_alg *gvt_alg;

		gvt_alg = devm_kzalloc(dev, sizeof(*gvt_alg), GFP_KERNEL);
		if (!gvt_alg) {
			rc = -ENOMEM;
			goto alg_error;
		}

		gvt_alg->alg_index = i;
		gvt_alg->comp_dev = comp_dev;
		gvt_alg->crypto_alg = driver_algs[i];
		gvt_alg->crypto_alg.cra_ctxsize = sizeof(struct gvt_comp_ctx);
		snprintf(gvt_alg->alg_name, GVT_MAX_NAME_LENGTH - 1,
			 "%s:%s", alg_name_prefixes[i], dev_name(dev));

		memcpy(gvt_alg->crypto_alg.cra_name,
		       gvt_alg->alg_name,
		       CRYPTO_MAX_ALG_NAME);

		rc = crypto_register_alg(&gvt_alg->crypto_alg);
		if (rc) {
			dev_err(dev, "%s: %s alg registration failed\n",
				      __func__, gvt_alg->crypto_alg.cra_name);
			goto alg_error;
		} else
			list_add_tail(&gvt_alg->entry, &comp_dev->alg_list);
	}

	if (!list_empty(&comp_dev->alg_list)) {
		dev_info(dev, "%d algorithm(s) registered with /proc/crypto\n",
				i);
	} else {
		dev_info(dev, "%s: NO algorithms registered\n",
				__func__);
		rc = -ENODEV;
	}

	return rc;

alg_error:
	gvt_comp_alg_terminate(comp_dev);
	return rc;
}

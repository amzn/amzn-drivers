// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include "gvt_compress.h"
#include "gvt_compress_device.h"

static void gvt_comp_free_chan_resources(struct gvt_comp_chan *chan)
{
	struct gvt_comp_device *comp_dev;
	struct device *dev;

	comp_dev = chan->comp_dev;
	dev = comp_dev->dev;

	if (!chan->tx_dma_desc_virt) {
		dma_free_coherent(dev,
				  comp_dev->hw_descs_num *
				  comp_dev->hw_desc_size,
				  chan->tx_dma_desc_virt, chan->tx_dma_desc);
		chan->tx_dma_desc_virt = NULL;
	}

	if (!chan->rx_dma_desc_virt) {
		dma_free_coherent(dev,
				  comp_dev->hw_descs_num *
				  comp_dev->hw_desc_size,
				  chan->rx_dma_desc_virt, chan->rx_dma_desc);
		chan->rx_dma_desc_virt = NULL;
	}

	if (!chan->rx_dma_cdesc_virt) {
		dma_free_coherent(dev,
				  comp_dev->hw_descs_num *
				  comp_dev->hw_cdesc_size,
				  chan->rx_dma_cdesc_virt, chan->rx_dma_cdesc);
		chan->rx_dma_desc_virt = NULL;
	}

	if (!chan->alg_dma_desc_virt) {
		dma_free_coherent(dev,
				  comp_dev->alg_desc_size,
				  chan->alg_dma_desc_virt, chan->alg_dma_desc);
		chan->alg_dma_desc_virt = NULL;
	}

	sg_free_table(&chan->src_sgt);
	sg_free_table(&chan->dst_sgt);
}

static void gvt_comp_free_chan_resource_all(struct gvt_comp_device *comp_dev)
{
	int i;

	for (i = 0; i < comp_dev->num_channels; i++)
		gvt_comp_free_chan_resources(&comp_dev->channels[i]);
}

static int gvt_comp_alloc_chan_resources(struct gvt_comp_chan *chan)
{
	struct gvt_comp_device *comp_dev;
	struct device *dev;

	comp_dev = chan->comp_dev;
	dev = comp_dev->dev;

	/* allocate memory for sw desc */
	chan->sw_desc = devm_kzalloc(dev,
				     comp_dev->sw_desc_size,
				     GFP_KERNEL);

	if (!chan->sw_desc)
		return -ENOMEM;

	/* allocate coherent memory for Tx submission descriptors */
	chan->tx_dma_desc_virt = dma_alloc_coherent(dev,
						    comp_dev->hw_descs_num *
						    comp_dev->hw_desc_size,
						    &chan->tx_dma_desc,
						    GFP_KERNEL);

	if (!chan->tx_dma_desc_virt)
		return -ENOMEM;

	/* allocate coherent memory for Rx submission descriptors */
	chan->rx_dma_desc_virt = dma_alloc_coherent(dev,
						    comp_dev->hw_descs_num *
						    comp_dev->hw_desc_size,
						    &chan->rx_dma_desc,
						    GFP_KERNEL);

	if (!chan->rx_dma_desc_virt)
		return -ENOMEM;

	/* allocate coherent memory for Rx completion descriptors */
	chan->rx_dma_cdesc_virt = dma_alloc_coherent(dev,
						     comp_dev->hw_descs_num *
						     comp_dev->hw_cdesc_size,
						     &chan->rx_dma_cdesc,
						     GFP_KERNEL);

	if (!chan->rx_dma_cdesc_virt)
		return -ENOMEM;

	/* allocate coherent memory for SA */
	chan->alg_dma_desc_virt = dma_alloc_coherent(dev,
						     comp_dev->alg_desc_size,
						     &chan->alg_dma_desc,
						     GFP_KERNEL);

	if (!chan->alg_dma_desc_virt)
		return -ENOMEM;

	if (unlikely(sg_alloc_table(&chan->src_sgt, GVT_COMP_SG_ENT_MAX,
				    GFP_KERNEL)))
		return -ENOMEM;


	if (unlikely(sg_alloc_table(&chan->dst_sgt, GVT_COMP_SG_ENT_MAX,
				    GFP_KERNEL)))
		return -ENOMEM;

	return  0;
}

int gvt_comp_core_init(struct gvt_comp_device *comp_dev)
{
	struct device *dev;
	int rc = 0;
	int i;

	dev = comp_dev->dev;

	bitmap_zero(comp_dev->channel_use_bitmap, GVT_MAX_CHANNELS);

	comp_dev->channels = devm_kcalloc(dev, comp_dev->num_channels,
					sizeof(struct gvt_comp_chan),
					GFP_KERNEL);

	if (!comp_dev->channels)
		return -ENOMEM;

	for (i = 0; i < comp_dev->num_channels; i++) {
		struct gvt_comp_chan *chan = &comp_dev->channels[i];

		chan->comp_dev = comp_dev;
		chan->idx = i;

		rc = gvt_comp_alloc_chan_resources(chan);
		if (rc) {
			dev_err(dev, "failed alloc resources for channel %d\n",
				     i);
			break;
		}
	}

	if (rc) {
		dev_err(dev, "failed to alloc channel resources\n");
		gvt_comp_free_chan_resource_all(comp_dev);
		return rc;
	}

	rc = gvt_comp_device_init(comp_dev);
	if (rc) {
		dev_err(dev, "failed to setup device\n");
		gvt_comp_free_chan_resource_all(comp_dev);
	}

	return rc;
}

void gvt_comp_core_terminate(struct gvt_comp_device *comp_dev)
{
	gvt_comp_device_terminate(comp_dev);
	gvt_comp_free_chan_resource_all(comp_dev);
}

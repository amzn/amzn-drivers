// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include "gvt_compress.h"

static ssize_t gvt_comp_device_attr_show(struct kobject *kobj,
					 struct attribute *attr,
					 char *buf);

static ssize_t gvt_comp_chan_attr_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf);

static ssize_t gvt_comp_device_read_stats(struct gvt_comp_device *dev,
					  size_t offset,
					  char *buf);

static ssize_t gvt_comp_chan_read_stats(struct gvt_comp_chan *chan,
					size_t offset,
					char *buf);

static void gvt_comp_release_stats(struct kobject *kobj);

struct gvt_comp_device_attr {
	struct attribute attr;
	size_t offset;
	ssize_t (*show)(struct gvt_comp_device *dev, size_t offset, char *buf);
	ssize_t (*store)(struct gvt_comp_device *dev, size_t offset,
			 const char *buf, size_t size);
};

struct gvt_comp_chan_attr {
	struct attribute attr;
	size_t offset;
	ssize_t (*show)(struct gvt_comp_chan *chan, size_t offset, char *buf);
	ssize_t (*store)(struct gvt_comp_chan *chan, size_t offset,
			 const char *buf, size_t size);
};

/* show function points to gvt_comp_device_read_stats() */
#define gvt_comp_device_init_attr(_name, _group)			\
static struct gvt_comp_device_attr gvt_comp_device_##_name = {		\
	.attr	= { .name = __stringify(_name), .mode = 0444},	\
	.offset	= offsetof(struct gvt_comp_device_##_group, _name),	\
	.show = gvt_comp_device_read_##_group,				\
	.store = NULL,							\
}

/* show function points to gvt_comp_chan_read_stats() */
#define gvt_comp_chan_init_attr(_name, _group)				\
static struct gvt_comp_chan_attr gvt_comp_chan_##_name = {		\
	.attr	= { .name = __stringify(_name), .mode = 0444},	\
	.offset	= offsetof(struct gvt_comp_chan_##_group, _name),	\
	.show = gvt_comp_chan_read_##_group,				\
	.store = NULL,							\
}

/* Device attrs */
gvt_comp_device_init_attr(comp_user_err, stats);
gvt_comp_device_init_attr(decomp_user_err, stats);
gvt_comp_device_init_attr(chan_unavail_err, stats);
gvt_comp_device_init_attr(device_err, stats);

/* Channel attrs */
gvt_comp_chan_init_attr(hw_desc_err, stats);
gvt_comp_chan_init_attr(sw_err, stats);
gvt_comp_chan_init_attr(completion_err, stats);
gvt_comp_chan_init_attr(comp_reqs, stats);
gvt_comp_chan_init_attr(comp_input_bytes, stats);
gvt_comp_chan_init_attr(comp_output_bytes, stats);
gvt_comp_chan_init_attr(decomp_reqs, stats);
gvt_comp_chan_init_attr(decomp_input_bytes, stats);
gvt_comp_chan_init_attr(decomp_output_bytes, stats);

static struct attribute *gvt_comp_device_default_attrs[] = {
	&gvt_comp_device_comp_user_err.attr,
	&gvt_comp_device_decomp_user_err.attr,
	&gvt_comp_device_chan_unavail_err.attr,
	&gvt_comp_device_device_err.attr,
	NULL
};

static struct attribute *gvt_comp_chan_default_attrs[] = {
	&gvt_comp_chan_hw_desc_err.attr,
	&gvt_comp_chan_sw_err.attr,
	&gvt_comp_chan_completion_err.attr,
	&gvt_comp_chan_comp_reqs.attr,
	&gvt_comp_chan_comp_input_bytes.attr,
	&gvt_comp_chan_comp_output_bytes.attr,
	&gvt_comp_chan_decomp_reqs.attr,
	&gvt_comp_chan_decomp_input_bytes.attr,
	&gvt_comp_chan_decomp_output_bytes.attr,
	NULL
};

static const struct sysfs_ops gvt_comp_device_sysfs_ops = {
	.show = gvt_comp_device_attr_show,
	.store = NULL,
};

static const struct sysfs_ops gvt_comp_chan_sysfs_ops = {
	.show = gvt_comp_chan_attr_show,
	.store = NULL,
};

static struct kobj_type dev_ktype = {
	.sysfs_ops = &gvt_comp_device_sysfs_ops,
	.release = gvt_comp_release_stats,
	.default_attrs = gvt_comp_device_default_attrs,
};

static struct kobj_type chan_ktype = {
	.sysfs_ops = &gvt_comp_chan_sysfs_ops,
	.release = gvt_comp_release_stats,
	.default_attrs = gvt_comp_chan_default_attrs,
};

static void gvt_comp_release_stats(struct kobject *kobj)
{
	/* Nothing to be done here */
}

static ssize_t gvt_comp_device_attr_show(struct kobject *kobj,
					 struct attribute *attr,
					 char *buf)
{
	struct gvt_comp_device_attr *dev_attr =
			container_of(attr, struct gvt_comp_device_attr, attr);
	struct gvt_comp_device *device =
			container_of(kobj, struct gvt_comp_device, kobj);
	ssize_t rc = 0;

	if (dev_attr->show)
		rc = dev_attr->show(device, dev_attr->offset, buf);

	return rc;
}

static ssize_t gvt_comp_chan_attr_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf)
{
	struct gvt_comp_chan_attr *chan_attr =
			container_of(attr, struct gvt_comp_chan_attr, attr);
	struct gvt_comp_chan *chan =
			container_of(kobj, struct gvt_comp_chan, kobj);
	ssize_t rc = 0;

	if (chan_attr->show)
		rc = chan_attr->show(chan, chan_attr->offset, buf);

	return rc;
}

static ssize_t gvt_comp_device_read_stats(struct gvt_comp_device *device,
					  size_t offset,
					  char *buf)
{
	uint64_t val;
	ssize_t size;

	val = *(uint64_t *)(((uint8_t *)&device->stats) + offset);

	size = sprintf(buf, "%llu\n", val);

	return size;
}

static ssize_t gvt_comp_chan_read_stats(struct gvt_comp_chan *chan,
					size_t offset,
					char *buf)
{
	uint64_t val;
	ssize_t size;

	val = *(uint64_t *)(((uint8_t *)&chan->stats) + offset);

	size = sprintf(buf, "%llu\n", val);

	return size;
}

int gvt_comp_sysfs_init(struct gvt_comp_device *comp_dev)
{
	int rc = 0;
	int i;

	comp_dev->channels_kset = kset_create_and_add("channels", NULL,
						    &comp_dev->pdev->dev.kobj);
	if (!comp_dev->channels_kset)
		return -ENOMEM;

	comp_dev->kobj.kset = comp_dev->channels_kset;
	rc = kobject_init_and_add(&comp_dev->kobj, &dev_ktype, NULL, "dev");

	for (i = 0; i < comp_dev->num_channels; i++) {
		struct gvt_comp_chan *chan = &comp_dev->channels[i];

		chan->kobj.kset = comp_dev->channels_kset;
		rc = kobject_init_and_add(&chan->kobj, &chan_ktype,
					  NULL, "chan%d", i);

		if (rc) {
			int j;

			for (j = 0; j < i; j++)
				kobject_put(&comp_dev->channels[j].kobj);
			kset_unregister(comp_dev->channels_kset);
			return -ENOMEM;
		}

		kobject_uevent(&chan->kobj, KOBJ_ADD);
	}

	return rc;
}

void gvt_comp_sysfs_terminate(struct gvt_comp_device *comp_dev)
{
	int i;

	for (i = 0; i < comp_dev->num_channels; i++)
		kobject_put(&comp_dev->channels[i].kobj);

	kobject_put(&comp_dev->kobj);
	kset_unregister(comp_dev->channels_kset);
}

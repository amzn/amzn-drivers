// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "ena_devlink.h"
#ifdef ENA_DEVLINK_SUPPORT

static int ena_devlink_llq_header_validate(struct devlink *devlink, u32 id,
					   union devlink_param_value val,
					   struct netlink_ext_ack *extack);

enum ena_devlink_param_id {
	ENA_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	ENA_DEVLINK_PARAM_ID_LLQ_HEADER_SIZE,
};

static const struct devlink_param ena_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(ENA_DEVLINK_PARAM_ID_LLQ_HEADER_SIZE,
			     "large_llq_header", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL, ena_devlink_llq_header_validate),
};

static int ena_devlink_llq_header_validate(struct devlink *devlink, u32 id,
					   union devlink_param_value val,
					   struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	bool value = val.vbool;

	if (!value)
		return 0;

	if (adapter->ena_dev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST) {
		NL_SET_ERR_MSG_MOD(extack, "Instance doesn't support LLQ");
		return -EOPNOTSUPP;
	}

	if (!adapter->large_llq_header_supported) {
		NL_SET_ERR_MSG_MOD(extack, "Instance doesn't support large LLQ");
		return -EOPNOTSUPP;
	}

	return 0;
}

#ifdef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
/* Determines if ena_devlink_register has been called.
 * Prefer to check if the driver enabled reloading capabilities, but fallback
 * to check if driver configured 'dev' devlink attribute for older kernels.
 */
bool ena_is_devlink_params_registered(struct devlink *devlink)
{
#if defined(ENA_DEVLINK_RELOAD_ENABLING_REQUIRED)
	return devlink->reload_enabled;
#elif !defined(ENA_DEVLINK_RECEIVES_DEVICE_ON_ALLOC)
	return devlink->dev;
#endif
}

#endif
void ena_devlink_params_get(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	union devlink_param_value val;
	int err;

#ifdef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	/* If devlink params aren't registered, don't access them */
	if (!ena_is_devlink_params_registered(devlink))
		return;
#endif
	err = devlink_param_driverinit_value_get(devlink,
						 ENA_DEVLINK_PARAM_ID_LLQ_HEADER_SIZE,
						 &val);
	if (err) {
		netdev_err(adapter->netdev, "Failed to query LLQ header size param\n");
		return;
	}

	adapter->large_llq_header_enabled = val.vbool;
}

void ena_devlink_disable_large_llq_header_param(struct devlink *devlink)
{
	union devlink_param_value value;

#ifdef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	/* If devlink params aren't registered, don't access them */
	if (!ena_is_devlink_params_registered(devlink))
		return;

#endif
	value.vbool = false;
	devlink_param_driverinit_value_set(devlink,
					   ENA_DEVLINK_PARAM_ID_LLQ_HEADER_SIZE,
					   value);
}

static int ena_devlink_reload_down(struct devlink *devlink,
#ifdef ENA_DEVLINK_RELOAD_NS_CHANGE_SUPPORT
				   bool netns_change,
#endif
#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
				   enum devlink_reload_action action,
				   enum devlink_reload_limit limit,
#endif
				   struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);

#ifdef ENA_DEVLINK_RELOAD_NS_CHANGE_SUPPORT
	if (netns_change) {
		NL_SET_ERR_MSG_MOD(extack, "Namespace change is not supported");
		return -EOPNOTSUPP;
	}

#endif
#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
	if (action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT) {
		NL_SET_ERR_MSG_MOD(extack, "Action is not supported");
		return -EOPNOTSUPP;
	}

	if (limit != DEVLINK_RELOAD_LIMIT_UNSPEC) {
		NL_SET_ERR_MSG_MOD(extack, "Driver reload doesn't support limitations");
		return -EOPNOTSUPP;
	}

#endif
	rtnl_lock();
	ena_destroy_device(adapter, false);
	rtnl_unlock();

	return 0;
}

static int ena_devlink_reload_up(struct devlink *devlink,
#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
				 enum devlink_reload_action action,
				 enum devlink_reload_limit limit,
				 u32 *actions_performed,
#endif
				 struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	int err = 0;

#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
	if (action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT) {
		NL_SET_ERR_MSG_MOD(extack, "Action is not supported");
		return -EOPNOTSUPP;
	}

	if (limit != DEVLINK_RELOAD_LIMIT_UNSPEC) {
		NL_SET_ERR_MSG_MOD(extack, "Driver reload doesn't support limitations");
		return -EOPNOTSUPP;
	}

#endif
	rtnl_lock();
	/* Check that no other routine initialized the device (e.g.
	 * ena_fw_reset_device()). Also we're under devlink_mutex here,
	 * so devink (and ena_adapter with it) isn't freed under our
	 * feet.
	 */
	if (!test_bit(ENA_FLAG_DEVICE_RUNNING, &adapter->flags))
		err = ena_restore_device(adapter);
	rtnl_unlock();

#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
	if (!err)
		*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

#endif
	return err;
}
#ifndef ENA_DEVLINK_RELOAD_UP_DOWN_SUPPORTED

static int ena_devlink_reload(struct devlink *devlink, struct netlink_ext_ack *extack)
{
	/* This function always succeeds when called from this function */
	ena_devlink_reload_down(devlink, extack);

	return ena_devlink_reload_up(devlink, extack);
}

#endif

static const struct devlink_ops ena_devlink_ops = {
#ifdef ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
#endif
#ifdef ENA_DEVLINK_RELOAD_UP_DOWN_SUPPORTED
	.reload_down	= ena_devlink_reload_down,
	.reload_up	= ena_devlink_reload_up,
#else
	.reload		= ena_devlink_reload,
#endif
};

static int ena_devlink_configure_params(struct devlink *devlink)
{
	struct ena_adapter *adapter = ENA_DEVLINK_PRIV(devlink);
	union devlink_param_value value;
	int rc;

	rc = devlink_params_register(devlink, ena_devlink_params,
				     ARRAY_SIZE(ena_devlink_params));
	if (rc) {
		netdev_err(adapter->netdev, "Failed to register devlink params\n");
		return rc;
	}

	value.vbool = adapter->large_llq_header_enabled;
	devlink_param_driverinit_value_set(devlink,
					   ENA_DEVLINK_PARAM_ID_LLQ_HEADER_SIZE,
					   value);

#ifdef ENA_DEVLINK_RELOAD_SUPPORT_ADVERTISEMENT_NEEDED
	devlink_set_features(devlink, DEVLINK_F_RELOAD);

#endif
#ifdef ENA_DEVLINK_RELOAD_ENABLING_REQUIRED
	devlink_reload_enable(devlink);

#endif
	return 0;
}

struct devlink *ena_devlink_alloc(struct ena_adapter *adapter)
{
#ifdef ENA_DEVLINK_RECEIVES_DEVICE_ON_ALLOC
	struct device *dev = &adapter->pdev->dev;
#endif
	struct devlink *devlink;

#ifdef ENA_DEVLINK_RECEIVES_DEVICE_ON_ALLOC
	devlink = devlink_alloc(&ena_devlink_ops, sizeof(struct ena_adapter *), dev);
#else
	devlink = devlink_alloc(&ena_devlink_ops, sizeof(struct ena_adapter *));
#endif
	if (!devlink) {
		netdev_err(adapter->netdev, "Failed to allocate devlink struct\n");
		return NULL;
	}

	ENA_DEVLINK_PRIV(devlink) = adapter;
	adapter->devlink = devlink;

#ifndef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	if (ena_devlink_configure_params(devlink))
		goto free_devlink;

	return devlink;
free_devlink:
	devlink_free(devlink);

	return NULL;
#else
	return devlink;
#endif
}

static void ena_devlink_configure_params_clean(struct devlink *devlink)
{
#ifdef ENA_DEVLINK_RELOAD_ENABLING_REQUIRED
	devlink_reload_disable(devlink);

#endif
	devlink_params_unregister(devlink, ena_devlink_params,
				  ARRAY_SIZE(ena_devlink_params));
}

void ena_devlink_free(struct devlink *devlink)
{
#ifndef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	ena_devlink_configure_params_clean(devlink);

#endif
	devlink_free(devlink);
}

void ena_devlink_register(struct devlink *devlink, struct device *dev)
{
#ifdef ENA_DEVLINK_RECEIVES_DEVICE_ON_ALLOC
	devlink_register(devlink);
#else
	devlink_register(devlink, dev);
#endif
#ifdef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	ena_devlink_configure_params(devlink);
#endif
}

void ena_devlink_unregister(struct devlink *devlink)
{
#ifdef ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
	ena_devlink_configure_params_clean(devlink);
#endif
	devlink_unregister(devlink);
}
#endif /* ENA_DEVLINK_SUPPORT */

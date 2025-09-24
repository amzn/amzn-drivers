// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2025 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#define NV_P2P_MAJOR_VERSION 2
#include "efa_nvmem_impl.h"

const struct efa_p2p_provider *nvmem_v2_get_provider(void)
{
	return nvmem_get_provider();
}

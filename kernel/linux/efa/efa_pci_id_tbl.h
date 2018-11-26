/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates.
 */

#ifndef _EFA_PCI_ID_TBL_H_
#define _EFA_PCI_ID_TBL_H_

#ifndef PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON    0x1d0f
#endif

#ifndef PCI_DEV_ID_EFA_VF
#define PCI_DEV_ID_EFA_VF      0xefa0
#endif

#define EFA_PCI_DEVICE_ID(devid) \
	{ PCI_VDEVICE(AMAZON, devid) }

static const struct pci_device_id efa_pci_tbl[] = {
	EFA_PCI_DEVICE_ID(PCI_DEV_ID_EFA_VF),
	{ }
};

#endif /* _EFA_PCI_ID_TBL_H_ */

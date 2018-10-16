# DPDK driver for Elastic Network Adapter (ENA):

### Overview

This folder includes critical bug fixes on previously released [DPDK version](https://dpdk.org).

### Applying the fixes:

In order to apply the fixes, follow these steps:
* Download the dpdk version from the [dpdk tree]
* Apply all patches for the same version:
 ```
 git am <DPDK_VERSION>/00*
```
# General notes for using igb_uio and vfio-pci drivers

Although documentation in older DPDK releases (18.08 and earlier) does not
mention about `vfio-pci` support, it can be used with the ENA DPDK PMD. It was
tested for DPDK releases starting from v17.02, so there are no contraindications
to use `vfio-pci` instead of `igb_uio` since v17.02 release.

### Instruction for using `vfio-pci`
To use `vfio-pci`, please do the following:
1. Insert module
```
modprobe vfio-pci
```
2. If the machine is not having IOMMU enabled (or is simply not supporting it),
   please use `vfio-pci` driver in `noiommu` mode
```
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
```
3. Bind the intended ENA device to the `vfio-pci` module

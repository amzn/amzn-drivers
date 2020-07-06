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

## Instruction for using `igb_uio` or vfio-pci`

1. Insert `vfio-pci` or `igb_uio` kernel module using the command
   `modprobe vfio-pci` or `modprobe uio; insmod igb_uio.ko wc_activate=1`
   respectively.

1. For `vfio-pci` users only:
   Please make sure that `IOMMU` is enabled in your system,
   or use `vfio` driver in `noiommu` mode:

   ```
   echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
   ```

1. To use `noiommu` mode, the `vfio-pci` must be built with flag
   `CONFIG_VFIO_NOIOMMU`.

1. Bind the intended ENA device to `vfio-pci` or `igb_uio` module.

At this point the system should be ready to run DPDK applications. Once the
application runs to completion, the ENA can be detached from attached module if
necessary.

### Note about usage on *.metal instances

On AWS, the metal instances are supporting IOMMU for both arm64 and x86_64
hosts.

* x86_64 (e.g. c5.metal, i3.metal):

  IOMMU should be disabled by default. In that situation, the `igb_uio` can
  be used as it is but `vfio-pci` should be working in no-IOMMU mode (please
  see above).

  When IOMMU is enabled, `igb_uio` cannot be used as it's not supporting this
  feature, while `vfio-pci` should work without any changes.
  To enable IOMMU on those hosts, please update `GRUB_CMDLINE_LINUX` in file
  `/etc/default/grub` with the below extra boot arguments:

  ```
  iommu=1 intel_iommu=on
  ```

  Then, make the changes live by executing as a root:

  ```
  # grub2-mkconfig > /boot/grub2/grub.cfg
  ```

  Finally, reboot should result in IOMMU being enabled.

* arm64 (a1.metal):
  IOMMU should be enabled by default. Unfortunately, `vfio-pci` isn't
  supporting SMMU, which is implementation of IOMMU for arm64 architecture and
  `igb_uio` isn't supporting IOMMU at all, so to use DPDK with ENA on those
  hosts, one must disable IOMMU. This can be done by updating
  `GRUB_CMDLINE_LINUX` in file `/etc/default/grub` with the extra boot
  argument:

  ```
  iommu.passthrough=1
  ```

  Then, make the changes live by executing as a root:

  ```
  # grub2-mkconfig > /boot/grub2/grub.cfg
  ```

  Finally, reboot should result in IOMMU being disabled.
  Without IOMMU, `igb_uio` can be used as it is but `vfio-pci` should be
  working in no-IOMMU mode (please see above).

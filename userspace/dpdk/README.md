# DPDK driver for Elastic Network Adapter (ENA)

- [1. Overview](#1-overview)
- [2. Applying the fixes](#2-applying-the-fixes)
- [3. PMD configuration options](#3-pmd-configuration-options)
  - [3.1. Runtime options (devargs)](#31-runtime-options-devargs)
  - [3.2. Makefile (deprecated starting from v20.11)](#32-makefile-deprecated-starting-from-v2011)
- [4. ENAv2 (>= v2.0.0) and WriteCombining](#4-enav2--v200-and-writecombining)
- [5. Instructions for using `igb_uio` and `vfio-pci`](#5-instructions-for-using-igb_uio-and-vfio-pci)
  - [5.1. igb_uio](#51-igb_uio)
  - [5.2. vfio-pci](#52-vfio-pci)
- [6. Note about usage on *.metal instances](#6-note-about-usage-on-metal-instances)
  - [6.1. x86_64 (e.g. c5.metal, i3.metal)](#61-x86_64-eg-c5metal-i3metal)
  - [6.2. arm64 (a1.metal)](#62-arm64-a1metal)

## 1. Overview

This folder includes critical bug fixes for previously released ENA PMDs
(Poll Mode Drivers) for the [DPDK framework](https://www.dpdk.org/).

## 2. Applying the fixes

In order to apply the fixes, please follow below steps:

1. Clone the DPDK repostitory from the [DPDK source tree](http://git.dpdk.org/dpdk/):

   ```sh
   git clone git://dpdk.org/dpdk
   ```

1. Clone repository with patches:

   ```sh
   git clone https://github.com/amzn/amzn-drivers.git
   ```

1. Checkout DPDK to the tag which is one of the supported versions:

   ```sh
   cd dpdk
   # <DPDK_VERSION> is one of the versions support by ENA, like v17.11
   git checkout <DPDK_VERSION>
   ```

1. Apply all required patches:

   ```sh
   git am ../amzn-drivers/userspace/dpdk/<DPDK_VERSION>/*.patch
   ```

## 3. PMD configuration options

The PMD can change it's behavior using either static (compile time) Makefile
arguments or the runtime options (device arguments - also called devargs).

### 3.1. Runtime options (devargs)

To tweak ENA behavior after build, one can use device arguments for that
purpose. The example usage of the device arguments for device with PCI BDF of
`00:06.0` is like below:

```sh
./dpdk_app -w 00:06.0,large_llq_hdr=1
```

ENA supports below devargs:

- **large_llq_hdr** *(default 0)* - Starting from DPDK v20.05. Enables or
  disables usage of large LLQ headers. This option will have effect only if the
  device also supports large LLQ headers. Otherwise, the default value will be
  used.

### 3.2. Makefile (deprecated starting from v20.11)

Makefile support has been removed at v20.11. But for all previous versions,
one can use below configuration options for enabling/disabling some features.

They can be modified either by passing them to the make or by editing `.config`
file of the current build setup.

- **CONFIG_RTE_LIBRTE_ENA_PMD** *(default y)* - enabled or disables inclusion of
  the ENA PMD driver in the DPDK compilation.
- **CONFIG_RTE_LIBRTE_ENA_DEBUG_RX** *(default n)*: Enables or disables debug
  logging of RX logic within the ENA PMD driver.
- **CONFIG_RTE_LIBRTE_ENA_DEBUG_TX** *(default n)*: Enables or disables debug
  logging of TX logic within the ENA PMD driver.
- **CONFIG_RTE_LIBRTE_ENA_COM_DEBUG** *(default n)*: Enables or disables debug
  logging of low level tx/rx logic in ena_com(base) within the ENA PMD driver.

## 4. ENAv2 (>= v2.0.0) and WriteCombining

For ENA PMD of v2.0.0 and higher it's mandatory to map memory BAR of the ENA as
WC (write combined) when used on ENAv2 hardware. Otherwise, if the driver will
use the LLQ (Low Latency Queues) without the WC, it will suffer from high CPU
usage and loss of performance due to very slow PCI transactions.

By default `igb_uio` and `vfio-pci` is not mapping the BARs as WC, even if they
support them, so the user must take care of that by himself.

Please refer to the section below for exact instructions how to use both
`igb_uio` and `vfio-pci` with WC support.

If the user do not want (or cannot) use one of the above modules in the WC mode,
the LLQ should be disabled in the driver (option for doing that should be
available soon).

## 5. Instructions for using `igb_uio` and `vfio-pci`

### 5.1. igb_uio

`igb_uio` module was part of the DPDK repository since up to v20.11. At v20.11
it was removed from there and can be found on the
[separate repository](http://git.dpdk.org/dpdk-kmods/).

Instructions for loading and configuring `igb_uio` can be found below.

1. Insert `igb_uio` kernel module with WC support, by passing `wc_activate=1`
   flag:

   ```sh
   modprobe uio # Load dependent kernel module uio
   insmod igb_uio.ko wc_activate=1 # insert igb_uio with WC support
   ```

1. Bind the intended ENA device to the `igb_uio` kernel module. The exact steps
   depends on the DPDK version being used, so please refer to the appropriate
   DPDK documentation.

At this point the system should be ready to run DPDK applications. Once the
application runs to completion, the ENA can be detached from attached module if
necessary.

### 5.2. vfio-pci

`vfio-pci` driver is part of the Linux Kernel, so it should be available for
every Linux user. However, if one needs to use ENAv2 and LLQ, the default vfio
driver does not support WC. The patch that will download vfio and apply WC
patch can be found in the [amzn-drivers repository](https://github.com/amzn/amzn-drivers/tree/master/userspace/dpdk/enav2-vfio-patch)
and it supports Amazon Linux, Amazon Linux 2, Red Hat Enterprise Linux and
Ubuntu 18.04.

Although documentation in older DPDK releases (18.08 and earlier) does not
mention `vfio-pci` support, it can be used with the ENA DPDK PMD. It was
tested for DPDK releases starting from v17.02, so there are no contraindications
to use `vfio-pci` instead of `igb_uio` since v17.02 release.

1. Insert `vfio-pci` kernel module:

   ```sh
   modprobe vfio-pci
   ```

1. Please make sure that `IOMMU` is enabled in your system, or use `vfio` driver
   in the `noiommu` mode (more about IOMMU on AWS instances can be found in the
   section below):

   ```sh
   echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
   ```

   To use `noiommu` mode, the `vfio-pci` must be built with flag
   `CONFIG_VFIO_NOIOMMU`.

1. Bind the intended ENA device to the `vfio-pci` kernel module. The exact steps
   depends on the DPDK version being used, so please refer to the appropriate
   DPDK documentation.

At this point the system should be ready to run DPDK applications. Once the
application runs to completion, the ENA can be detached from attached module if
necessary.

## 6. Note about usage on *.metal instances

On AWS, the metal instances are supporting IOMMU for both arm64 and x86_64
hosts.

### 6.1. x86_64 (e.g. c5.metal, i3.metal)

  IOMMU should be disabled by default. In that situation, the `igb_uio` can
  be used as it is but `vfio-pci` should be working in no-IOMMU mode (please
  see above).

  When IOMMU is enabled, `igb_uio` cannot be used as it's not supporting this
  feature, while `vfio-pci` should work without any changes.
  To enable IOMMU on those hosts, please update `GRUB_CMDLINE_LINUX` in file
  `/etc/default/grub` with the below extra boot arguments:

  ```txt
  iommu=1 intel_iommu=on
  ```

  Then, make the changes live by executing as a root:

  ```sh
  grub2-mkconfig > /boot/grub2/grub.cfg
  ```

  Finally, reboot should result in IOMMU being enabled.

### 6.2. arm64 (a1.metal)

  IOMMU should be enabled by default. Unfortunately, `vfio-pci` isn't
  supporting SMMU, which is implementation of IOMMU for arm64 architecture and
  `igb_uio` isn't supporting IOMMU at all, so to use DPDK with ENA on those
  hosts, one must disable IOMMU. This can be done by updating
  `GRUB_CMDLINE_LINUX` in file `/etc/default/grub` with the extra boot
  argument:

  ```txt
  iommu.passthrough=1
  ```

  Then, make the changes live by executing as a root:

  ```sh
  grub2-mkconfig > /boot/grub2/grub.cfg
  ```

  Finally, reboot should result in IOMMU being disabled.
  Without IOMMU, `igb_uio` can be used as it is but `vfio-pci` should be
  working in no-IOMMU mode (please see above).

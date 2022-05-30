# DPDK driver for Elastic Network Adapter (ENA)

- [1. Overview](#1-overview)
- [2. ENA versions and DPDK releases](#2-ena-versions-and-dpdk-releases)
- [3. ENA PMD backports](#3-ena-pmd-backports)
  - [3.1. Manual patching](#31-manual-patching)
  - [3.2. Patching using script](#32-patching-using-script)
- [4. Quick start guide](#4-quick-start-guide)
  - [4.1. Install prerequisites](#41-install-prerequisites)
  - [4.2. `igb_uio` setup](#42-igb_uio-setup)
  - [4.3. Get the DPDK sources](#43-get-the-dpdk-sources)
  - [4.4. Prepare environment](#44-prepare-environment)
  - [4.5. Build the DPDK](#45-build-the-dpdk)
  - [4.6. Execute testpmd application](#46-execute-testpmd-application)
- [5. PMD configuration options](#5-pmd-configuration-options)
  - [5.1. Runtime options (devargs)](#51-runtime-options-devargs)
  - [5.2. Makefile (deprecated starting from v20.11)](#52-makefile-deprecated-starting-from-v2011)
  - [5.3. Meson build system](#53-meson-build-system)
    - [5.3.1. meson arguments](#531-meson-arguments)
    - [5.3.2. Configuration file changes](#532-configuration-file-changes)
    - [5.3.3. Available reconfiguration options](#533-available-reconfiguration-options)
- [6. vfio-pci and igb_uio](#6-vfio-pci-and-igb_uio)
  - [6.1. ENAv2 (>= v2.0.0) and Write Combining](#61-enav2--v200-and-write-combining)
  - [6.2. Instructions for using `igb_uio` and `vfio-pci`](#62-instructions-for-using-igb_uio-and-vfio-pci)
    - [6.2.1. igb_uio](#621-igb_uio)
    - [6.2.2. vfio-pci](#622-vfio-pci)
    - [6.2.3. Verification of the Write Combined memory mapping](#623-verification-of-the-write-combined-memory-mapping)
  - [6.3. Note about usage on *.metal instances](#63-note-about-usage-on-metal-instances)
    - [6.3.1. x86_64 (e.g. c5.metal, i3.metal)](#631-x86_64-eg-c5metal-i3metal)
    - [6.3.2. arm64 (a1.metal)](#632-arm64-a1metal)
- [7. ENA PMD logging system](#7-ena-pmd-logging-system)
  - [7.1. DPDK v16.11 - v17.11](#71-dpdk-v1611---v1711)
  - [7.2. DPDK v18.02 - v21.05](#72-dpdk-v1802---v2105)
    - [7.2.1. Makefile (deprecated since v20.11)](#721-makefile-deprecated-since-v2011)
    - [7.2.2. meson + ninja](#722-meson--ninja)
    - [7.2.3. Loggers runtime configuration](#723-loggers-runtime-configuration)
  - [7.3. DPDK v21.08+](#73-dpdk-v2108)
    - [7.3.1. Loggers runtime configuration](#731-loggers-runtime-configuration)
- [8. Driver statistics](#8-driver-statistics)
  - [8.1. Generic statistics](#81-generic-statistics)
  - [8.2. Extended statistics (xstats)](#82-extended-statistics-xstats)
    - [8.2.1. General statistics](#821-general-statistics)
    - [8.2.2. ENI limiters](#822-eni-limiters)
    - [8.2.3. Tx per-queue statistics](#823-tx-per-queue-statistics)
    - [8.2.4. Rx per-queue statistics](#824-rx-per-queue-statistics)
- [9. Device reset and the timer service](#9-device-reset-and-the-timer-service)
- [10. Multi process (MP) support](#10-multi-process-mp-support)
- [11. RSS support](#11-rss-support)
  - [11.1. RSS feature](#111-rss-feature)
  - [11.2. Indirection table update](#112-indirection-table-update)
  - [11.3. RSS hash key update](#113-rss-hash-key-update)
  - [11.4. RSS hash fields modification](#114-rss-hash-fields-modification)
- [12. Performance tuning](#12-performance-tuning)
  - [12.1. Tx path](#121-tx-path)
  - [12.2. Rx path](#122-rx-path)
- [13. General FAQ](#13-general-faq)
- [14. Performance FAQ](#14-performance-faq)

## 1. Overview

This folder includes resources and utilities for the ENA PMD (Poll Mode Driver)
which can be found in the [DPDK framework](https://www.dpdk.org/):

1. Detailed [ENA PMD release notes](RELEASENOTES.md) for all the DPDK releases
   including ENA PMD changes.
2. [Patch for the `vfio-pci` module](enav2-vfio-patch) which may be used by the
   ENA PMD instead of the `igb_uio`. Details can be found
   [below](#622-vfio-pci).
3. The latest [ENA PMD driver backports](backports) based on the supported LTS
   branches.

This documentation may not be fully accurate for the older ENA and the DPDK
versions, as not all version discrepancy topics were covered. However, the most
important changes between the different ENA and the DPDK versions are being
documented.

## 2. ENA versions and DPDK releases

New ENA driver version releases are strictly associated with the new releases
of the DPDK framework. Table below allows to quickly associate DPDK releases
with the ENA driver's releases.

| DPDK version | ENA version  |
|--------------|--------------|
| 16.04        | unversioned  |
| 16.07        | 1.0.0        |
| 18.08        | 1.1.0        |
| 18.11        | 1.1.1        |
| 19.02        | 2.0.0        |
| 19.08        | 2.0.1        |
| 19.11        | 2.0.2        |
| 20.02        | 2.0.3        |
| 20.05        | 2.1.0        |
| 20.11        | 2.2.0        |
| 21.02        | 2.2.1        |
| 21.05        | 2.3.0        |
| 21.08        | 2.4.0        |
| 21.11        | 2.5.0        |

## 3. ENA PMD backports

ENA PMD v2.4.0 from DPDK v21.08 was backported to the all LTS DPDK versions,
since it was released.

- v16.11.11
- v17.11.10
- v18.11.11
- v19.11.10
- v20.11.3

ENA PMD backports are skipping DPDK features which aren't supported by the given
DPDK version - for example v16.11.11 backport won't contain the reset device
feature or the Tx prepare function. In order to use those features, the
application should migrate to the newer DPDK version.

The backported ENA version for all branches requires Write Combining support. To
satisfy that, the `igb_uio` included within the DPDK source tree needs to be
loaded with `wc_activate` option if it's being used instead of `vfio-pci`. This
feature was backported together with ENA PMD to all the LTS releases listed in
this chapter, which didn't had this feature available.
Please refer to the [appropriate chapter](#61-enav2--v200-and-write-combining)
for more details.

The backport is based on the latest available LTS branch for each supported DPDK
version at a time when it is created.

Already existing ENA backports may be rebased on the latests LTS release if the
LTS branch is still supported by the DPDK community. Previous LTS releases for
the same branch will no longer be supported (e.g. when ENA backport will be
targeted for the new LTS release v20.11.4, the v20.11.3 will be no longer
supported).

The DPDK application is advised to rebase as well in order to be sure that all
ENA PMD patches are applied.

The patches can be either applied manually, or by using the helper script. Full
instruction can be found below.

### 3.1. Manual patching

1. Clone the stable DPDK repository from the
   [stable DPDK source tree](https://git.dpdk.org/dpdk-stable/):

   ```sh
   git clone git://dpdk.org/dpdk-stable
   ```

2. Clone repository with patches:

   ```sh
   git clone https://github.com/amzn/amzn-drivers.git
   ```

3. Checkout DPDK to the LTS tag which is one of the supported versions:

   ```sh
   cd dpdk-stable
   # <DPDK_VERSION> is one of the versions support by ENA, like v17.11.10
   git checkout <DPDK_VERSION>
   ```

4. Apply all required patches:

   ```sh
   git am ../amzn-drivers/userspace/dpdk/<DPDK_VERSION>/*.patch
   ```

### 3.2. Patching using script

Patching using the script can be used instead of applying the patches manually.
The script can detect DPDK version automatically and choose the right backports
which should be applied.

The DPDK source code which should be patched can be either passed directly, or
implicitly, using the RTE_SDK environment variable:

```sh
# Read the RTE_SDK environmental variable value and apply appropriate ENA patches
./backports/apply-patches.sh

# Patch the repository, which can be found under the 'dpdk_src' location
./backports/apply-patches.sh -d dpdk_src
```

If the script will reject to apply the patches to the given DPDK source code,
it can be forced to do so. This options should be used with caution! It can
result in merge conflicts or the final source code can be missing some ENA
patches.

```sh
# Use the patches from the 'backports/v18.11.11' directory and apply them to the
# repository, which can be found under the 'dpdk_src' location
./backports/apply-patches.sh -p backports/v18.11.11 -d dpdk_src
```

## 4. Quick start guide

Short instructions for setting up and installing the DPDK on Amazon Linux 2 AMI.

Instructions are showing how to prepare clean AMI for the DPDK, get the DPDK
sources and build them using the meson build system.

### 4.1. Install prerequisites

Get the latest kernel version to make sure the downloaded kernel headers will be
up to date.

```sh
sudo yum update
sudo yum install -y kernel
sudo reboot
```

Get the kernel headers and tools required to download and install the DPDK.

```sh
sudo yum install -y git kernel-devel kernel-headers
pip3 install meson ninja pyelftools
```

### 4.2. `igb_uio` setup

This example is focusing on `igb_uio` module - alternatively, ENA support
`vfio-pci` as well. Please refer to the [appropriate section](#622-vfio-pci) for
more details.

Get the repository containing `igb_uio` module.

```sh
git clone git://dpdk.org/dpdk-kmods
cd dpdk-kmods/linux/igb_uio
make
```

Load the module with `wc_activate=1` option in order to use the Write Combining,
required by the ENAv2 in order to work efficiently.

```sh
# igb_uio depends on the generic uio module
sudo modprobe uio
# Load igb_uio with Write Combining activated
sudo insmod ./igb_uio.ko wc_activate=1
```

### 4.3. Get the DPDK sources

For main releases or the latest development version of the DPDK, please clone
the `dpdk` repository. In order to use some DPDK version (for example `v21.08`),
just checkout to the tag.

```sh
git clone git://dpdk.org/dpdk
cd dpdk
git checkout v21.08
```

Stable DPDK releases, including LTS versions, can be found in the `dpdk-stable`
repository. Same as for the main DPDK repository, the stable release can be
acquired by checking out to the tag (for example: `v19.11.10`).

```sh
git clone git://dpdk.org/dpdk-stable
cd dpdk-stable
git checkout v19.11.10
```

### 4.4. Prepare environment

Allocate hugepages for the DPDK.

```sh
echo 4096 | sudo tee /proc/sys/vm/nr_hugepages
```

Disable and bind `igb_uio` module to the `eth1` (secondary) ENA interface, which
has PCI ID equal to `00:06.0`.

```sh
# Disable the interface
sudo ifconfig eth1 down
# Check the status
sudo usertools/dpdk-devbind.py --status
# Bind the right device
sudo usertools/dpdk-devbind.py --bind=igb_uio 00:06.0
```

### 4.5. Build the DPDK

The quickest way of building the DPDK is using the meson and ninja tools.
Two command below are enough in order to accomplish that process.

```sh
meson build
ninja -C build
```

### 4.6. Execute testpmd application

Run `testpmd` application which is designed for testing various PMD features.
More information can be found in the
[official DPDK guide](https://doc.dpdk.org/guides/testpmd_app_ug/).

```sh
sudo ./build/app/dpdk-testpmd -- -i
```

## 5. PMD configuration options

The PMD can change it's behavior using either static (compile time) Makefile
arguments or the runtime options (_device arguments_ - also called _devargs_).

### 5.1. Runtime options (devargs)

To tweak ENA behavior after build, one can use device arguments for that
purpose. The example usage of the device arguments for device with PCI BDF of
`00:06.0` is like below:

```sh
./dpdk_app -w 00:06.0,large_llq_hdr=1
```

ENA supports below devargs:

- **large_llq_hdr** *(default 0)* - Starting from DPDK v20.05. Enables or
  disables usage of large LLQ headers.

  Allows application to send packets which header size is greater than 96B.
  Large LLQ header maximum size is 224B.

  Increasing LLQ header size reduces the size of the Tx queue by half.

  This option will have effect only if the device also supports large LLQ
  headers. Otherwise, the default value will be used.

  Note: it's fully functional since DPDK release v21.05 (ENA v2.3.0) and LTS
  release v20.11.2.

### 5.2. Makefile (deprecated starting from v20.11)

Makefile support has been removed at DPDK `v20.11`. But for all previous
versions one can use the configuration options showed below for
enabling/disabling some features.

They can be modified either by passing them to the make or by editing `.config`
file of the current build setup.

- **CONFIG_RTE_LIBRTE_ENA_PMD** *(default y)*: Enabled or disables inclusion of
  the ENA PMD driver in the DPDK compilation.
- **CONFIG_RTE_LIBRTE_ENA_DEBUG_RX** *(default n)*: Enables or disables debug
  logging of RX logic within the ENA PMD driver.
- **CONFIG_RTE_LIBRTE_ENA_DEBUG_TX** *(default n)*: Enables or disables debug
  logging of TX logic within the ENA PMD driver.
- **CONFIG_RTE_LIBRTE_ENA_COM_DEBUG** *(default n)*: Enables or disables debug
  logging of low level tx/rx logic in ena_com(base) within the ENA PMD driver.

### 5.3. Meson build system

If the meson build system is used instead of the Makefile, there are two ways of
modifying those arguments.

#### 5.3.1. meson arguments

During the configuration or reconfiguration process, the appropriate build flags
can be passed to the meson by using the `-Dc_args` argument, like below:

```sh
meson build -Dc_args='-DRTE_LIBRTE_ENA_DEBUG_RX=1 -DRTE_LIBRTE_ENA_DEBUG_TX=1'
```

This options can also be modified after the project has been configured:

```sh
cd build
meson configure -Dc_args='-DRTE_LIBRTE_ENA_DEBUG_TX=1'
```

List of available options depending on the used DPDK version can be found below.

#### 5.3.2. Configuration file changes

After project configuration using the meson, file  `rte_config.h` is being
created in the build folder. It can be used to control some of the parameters,
but it's being overwritten each time the reconfiguration is performed.

To enable the extra loggers, they should be defined in the mentioned file. List
of available options depending on the used DPDK version can be found below.

```C
// build/rte_config.h
#define RTE_LIBRTE_ENA_DEBUG_RX 1
#define RTE_LIBRTE_ENA_DEBUG_TX 1
#define RTE_LIBRTE_ENA_COM_DEBUG 1
```

#### 5.3.3. Available reconfiguration options

Please note, that for the Makefile all the options have the `CONFIG_` prefix.

| Flag                           | Default | Used since | Available to | Effect                                       |
|--------------------------------|---------|------------|--------------|----------------------------------------------|
| `RTE_LIBRTE_ENA_DEBUG_DRIVER`  | _N/A_   | v16.11     | v17.11       | Enable additional driver's logs.             |
| `RTE_LIBRTE_ENA_DEBUG_RX`      | _N/A_   | v16.11     | v21.05       | Enable extra Rx path logs.                   |
| `RTE_LIBRTE_ENA_DEBUG_TX`      | _N/A_   | v16.11     | v21.05       | Enable extra Tx path logs.                   |
| `RTE_LIBRTE_ENA_DEBUG_TX_FREE` | _N/A_   | v16.11     | v21.05       | Enable extra Tx cleanup path logs.           |
| `RTE_LIBRTE_ENA_COM_DEBUG`     | _N/A_   | v16.11     | v21.05       | Enable `ena_com` layer logs                  |
| `RTE_LIBRTE_ETHDEV_DEBUG`      | _N/A_   | v16.11     | -            | PMD debug mode. Also enables both `RTE_ETHDEV_DEBUG_RX` and `RTE_ETHDEV_DEBUG_TX`. |
| `RTE_ETHDEV_DEBUG_RX`          | _N/A_   | v21.08     | -            | Enable extra Rx path logs and verifications. |
| `RTE_ETHDEV_DEBUG_TX`          | _N/A_   | v21.08     | -            | Enable extra Tx path logs and verifications. |

__*Notes:*__

- Starting from DPDK `v21.08`, `ena_com` debug logs are enabled by default.
- Both `RTE_LIBRTE_ETHDEV_DEBUG` and `RTE_ETHDEV_DEBUG_RX` or
  `RTE_ETHDEV_DEBUG_TX` cannot be declared.

## 6. vfio-pci and igb_uio

__Important note - please read it first!__

ENA on 5th generation instances and newer supports LLQ (Low Latency Queue) mode.
While it's highly recommended to use the LLQ mode for the 5th generation
platforms, it is specifically required on the 6th generation platforms and
later. Failing to do so will result in a huge performance degradation.

List of the all instance generation can be found
[here](https://aws.amazon.com/ec2/instance-types/).

### 6.1. ENAv2 (>= v2.0.0) and Write Combining

For ENA PMD of v2.0.0 and higher it's mandatory to map memory BAR of the ENA as
WC (write combined) when used on ENAv2 hardware. Otherwise, if the driver will
use the LLQ (Low Latency Queues) without the WC, it will suffer from high CPU
usage and loss of performance due to very slow PCI transactions.

By default `igb_uio` and `vfio-pci` is not mapping the BARs as WC, even if they
support them, so the user must take care of that by himself.

Please refer to the section below for exact instructions how to use both
`igb_uio` and `vfio-pci` with WC support.

If the user do not want (or cannot) use one of the above modules in the WC mode,
he should use instances with ENAv1 hardware, which doesn't support LLQ and
doesn't require Write Combining in order to work properly.

### 6.2. Instructions for using `igb_uio` and `vfio-pci`

#### 6.2.1. igb_uio

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

2. Bind the intended ENA device to the `igb_uio` kernel module. The exact steps
   depends on the DPDK version being used.

   ```sh
   # DPDK v16.04
   ./dpdk/tools/dpdk_nic_bind.py --bind=igb_uio <pci_id>
   # DPDK v16.07 - v16.11
   ./dpdk/tools/dpdk-devbind.py --bind=igb_uio <pci_id>
   # DPDK v17.02+
   ./dpdk/usertools/dpdk-devbind.py --bind=igb_uio <pci_id>
   ```

At this point the system should be ready to run DPDK applications. Once the
application runs to completion, the ENA can be detached from `igb_uio` if
necessary.

#### 6.2.2. vfio-pci

`vfio-pci` driver is part of the Linux Kernel, so it should be available for
every Linux user. However, if one needs to use ENAv2 and LLQ, the default vfio
driver does not support WC.

As upstream of this change is complex, users that would like to use vfio with
WC may use [this script](enav2-vfio-patch/get-vfio-with-wc.sh).

It supports Amazon Linux, Amazon Linux 2, Red Hat Enterprise Linux,
Ubuntu 18.04 and Ubuntu 20.04.

After cloning this repository, the usage of the script is straight forward:

```sh
cd amzn-drivers/userspace/dpdk/enav2-vfio-patch
sudo get-vfio-with-wc.sh
```

The script will download appropriate Linux kernel sources, apply WC patch,
rebuild vfio with support for the no-iommu-mode and replace existing vfio module
with the patched version. In some cases the kernel update and reboot is required
if the package manager no longer holds the kernel sources for the currently
running kernel.

Although documentation in older DPDK releases (18.08 and earlier) does not
mention `vfio-pci` support, it can be used with the ENA DPDK PMD. It was
tested for DPDK releases starting from v17.02, so there are no contraindications
to use `vfio-pci` instead of `igb_uio` since v17.02 release.

1. Insert `vfio-pci` kernel module:

   ```sh
   modprobe vfio-pci
   ```

2. Please make sure that `IOMMU` is enabled in your system, or use `vfio` driver
   in the `noiommu` mode (more about IOMMU on AWS instances can be found in the
   section below):

   ```sh
   echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
   ```

   To use `noiommu` mode, the `vfio-pci` must be built with flag
   `CONFIG_VFIO_NOIOMMU`.

3. Bind the intended ENA device to the `vfio-pci` kernel module. The exact steps
   depends on the DPDK version being used, so please refer to the appropriate
   DPDK documentation.

At this point the system should be ready to run DPDK applications. Once the
application runs to completion, the ENA can be detached from attached module if
necessary.

__Note:__ On Dec/2021 it was reported, that Ubuntu 20.04 LTS AMI changed the way
how the `vfio-pci` is being distributed. Instead of releasing it as a module,
it's being built-in the kernel.

The last working Ubuntu kernel, which is having the `vfio-pci` provided as a
module is a `5.4.0-1060-aws`.

There are two possible workaround to make the `vfio-pci` work on the latest
Ubuntu AMIs:

1. Change configuration of the existing Ubuntu kernel and rebuild it with
   `vfio-pci` being built as a kernel module. This helper script
   ['buildscript.zip'](https://github.com/amzn/amzn-drivers/files/7769634/buildscript.zip)
   can be used to automate those steps. It must be executed as a root.

2. Downgrade the kernel to the version where the `vfio-pci` is distributed as a
   module.

   A. Revert to the kernel `5.4.0-1060-aws`

      ```sh
      sudo apt install -y linux-image-5.4.0-1060-aws linux-headers-5.4.0-1060-aws linux-tools-5.4.0-1060-aws
      ```

   B. Update the grub

     1. On the fresh instance, open the `/etc/default/grub` and set the
        `GRUB_DEFAULT` value to `"1>2"`. It can be verified using the command
        below.

        ```sh
        $ grep GRUB_DEFAULT /etc/default/grub
        GRUB_DEFAULT="1>2"
        ```

        If the OS was modified prior to those steps, the `GRUB_DEFAULT` value
        may need to be set to a different value. This entry changes the kernel
        which will be booted.

     2. Apply the grub changes and reboot

        ```sh
        sudo update-grub
        sudo reboot
        ```

#### 6.2.3. Verification of the Write Combined memory mapping

In order to verify that the kernel module has been properly configured and
the Write Combining has been enabled on the host, the steps below must be
performed (x86_64 only):

1. Load appropriate kernel module as described above.

2. Bind ENA device to the kernel driver (`vfio-pci` or `igb_uio`).

3. Start any DPDK application that will use this ENA device - the device has to
   be properly initialized and the application must be running in order to make
   the ENA resources be exposed with their attributes.

   For example:

   ```sh
   `sudo ./dpdk-build-dir/apps/dpdk-testpmd`
   ```

4. _In the second console:_

   1. Verify address of the _prefetchable_ BAR of the ENA device that is used by
      the DPDK application.

      ```sh
      # lspci -v -s 00:06.0
      00:06.0 Ethernet controller: Amazon.com, Inc. Elastic Network Adapter (ENA)
      Subsystem: Amazon.com, Inc. Elastic Network Adapter (ENA)
      Physical Slot: 6
      Flags: fast devsel, IOMMU group 0
      Memory at febf8000 (32-bit, non-prefetchable) [size=16K]
      Memory at fe900000 (32-bit, prefetchable) [size=1M]
      Capabilities: [70] Express Endpoint, MSI 00
      Capabilities: [b0] MSI-X: Enable- Count=9 Masked-
      Kernel driver in use: vfio-pci
      Kernel modules: ena
      ```

      In this case, the prefetchable BAR has address of `0xfe900000`.

   2. Check what memory attributes are assigned to this memory region.

      __NOTE:__ The DPDK application must be still running while the command
      below is being executed! Otherwise the mapping won't be visible.

      ```sh
      # cat /sys/kernel/debug/x86/pat_memtype_list | grep fe900000
      PAT: [mem 0x00000000fe800000-0x00000000fe900000] write-combining
      PAT: [mem 0x00000000fe900000-0x00000000fea00000] write-combining
      ```

      If `write-combining` appears next to this memory region as above, then the
      Write Combining is working properly. Only the second line in the above
      output is relevant, as it describes the prefetchable memory region
      starting at the address `0xfe900000`.

### 6.3. Note about usage on *.metal instances

On AWS, the metal instances are supporting IOMMU for both arm64 and x86_64
hosts.

#### 6.3.1. x86_64 (e.g. c5.metal, i3.metal)

  IOMMU should be disabled by default. In that situation, the `igb_uio` can
  be used as is, but `vfio-pci` should be working in no-IOMMU mode (please
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

#### 6.3.2. arm64 (a1.metal)

  IOMMU should be enabled by default. Since `vfio-pci` does not support SMMU
  (implementation of IOMMU for arm64 architecture) and `igb_uio` does not
  support IOMMU at all, it is mandatory to disable IOMMU to allow DPDK with ENA
  to function on such hosts. This can be done by updating  `GRUB_CMDLINE_LINUX`
  in file `/etc/default/grub` with the extra boot argument:

  ```txt
  iommu.passthrough=1
  ```

  Then, make the changes live by executing as a root:

  ```sh
  grub2-mkconfig > /boot/grub2/grub.cfg
  ```

  Finally, reboot should result in IOMMU being disabled.
  Without IOMMU, `igb_uio` can be used as is but `vfio-pci` should be working
  in no-IOMMU mode (please see above).

## 7. ENA PMD logging system

ENA driver provides various loggers which can be enabled or disabled by changing
the build or execution configuration.

As both DPDK and ENA logging system was evolving, steps required to modify the
loggers behavior may be different depending on the used ENA version.

Below you can find the instructions on how to enable the extra ENA loggers for
the given DPDK version.

### 7.1. DPDK v16.11 - v17.11

1. Perform initial configuration of the project (if it wasn't already done)

   ```sh
   make config T=x86_64-native-linuxapp-gcc
   cd build
   ```

2. Enable extra logs in the configuration file

   ```sh
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX_FREE=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX_FREE=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_RX=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_RX=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_DRIVER=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_DRIVER=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_COM_DEBUG=.*/CONFIG_RTE_LIBRTE_ENA_COM_DEBUG=y/g' .config
   ```

3. Rebuild the DPDK

   ```sh
   make -j
   ```

4. Execute the dpdk application with highest logging level (if needed)

   ```sh
   ./dpdk-app --log-level=8
   ```

### 7.2. DPDK v18.02 - v21.05

#### 7.2.1. Makefile (deprecated since v20.11)

1. Perform initial configuration of the project (if it wasn't already done)

   ```sh
   make defconfig
   cd build
   ```

2. Enable extra logs in the configuration file

   ```sh
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX_FREE=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_TX_FREE=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_DEBUG_RX=.*/CONFIG_RTE_LIBRTE_ENA_DEBUG_RX=y/g' .config
   sed -i 's/CONFIG_RTE_LIBRTE_ENA_COM_DEBUG=.*/CONFIG_RTE_LIBRTE_ENA_COM_DEBUG=y/g' .config
   ```

3. Rebuild the DPDK

   ```sh
   make -j
   ```

#### 7.2.2. meson + ninja

1. Configure the project with appropriate build flags

   A. Initial configuration:

      ```sh
      meson build -Dc_args='-DRTE_LIBRTE_ENA_DEBUG_TX=1 -DRTE_LIBRTE_ENA_DEBUG_TX_FREE=1 -DRTE_LIBRTE_ENA_DEBUG_RX=1 -DRTE_LIBRTE_ENA_COM_DEBUG=1'
      cd build
      ```

   B. Reconfigure existing build:

     ```sh
     cd build
     meson configure -Dc_args='-DRTE_LIBRTE_ENA_DEBUG_TX=1 -DRTE_LIBRTE_ENA_DEBUG_TX_FREE=1 -DRTE_LIBRTE_ENA_DEBUG_RX=1 -DRTE_LIBRTE_ENA_COM_DEBUG=1'
     ```

2. Build the project

   ```sh
   ninja
   ```

#### 7.2.3. Loggers runtime configuration

ENA provides dynamic loggers which can be controlled by using the `--log-level`
EAL flags.

| DPDK version | Logger name           | Dependency                     | Purpose                     |
|--------------|-----------------------|--------------------------------|-----------------------------|
| v18.02       | `pmd.net.ena.init`    | -                              | PMD initialization logs     |
| v18.02       | `pmd.net.ena.driver`  | -                              | General driver logs         |
| v19.11       | `pmd.net.ena.com`     | `RTE_LIBRTE_ENA_COM_DEBUG`     | ena_com layer logs          |
| v19.11       | `pmd.net.ena.tx`      | `RTE_LIBRTE_ENA_DEBUG_TX`      | Tx path debugging logs      |
| v19.11       | `pmd.net.ena.tx_free` | `RTE_LIBRTE_ENA_DEBUG_TX_FREE` | Tx free path debugging logs |
| v19.11       | `pmd.net.ena.rx`      | `RTE_LIBRTE_ENA_DEBUG_RX`      | Rx path debugging logs      |

- _(since v18.02)_ `pmd.net.ena.init` - initialization logs
- _(since v18.02)_ `pmd.net.ena.driver` - general driver logs
- _(since v19.11)_ `pmd.net.ena.com` - ena_com layer logs (requires `RTE_LIBRTE_ENA_COM_DEBUG` flag)
- _(since v19.11)_ `pmd.net.ena.tx` - Tx path (requires `RTE_LIBRTE_ENA_DEBUG_TX` flag)
- _(since v19.11)_ `pmd.net.ena.tx_free` - Tx path cleanup path (requires `RTE_LIBRTE_ENA_DEBUG_TX_FREE` flag)
- _(since v19.11)_ `pmd.net.ena.rx` - general Rx logs (requires `RTE_LIBRTE_ENA_DEBUG_RX` flag)

Level of the logs can be controlled per module:

```sh
./dpdk-app --log-level=pmd.net.ena.init:8 --log-level=pmd.net.ena.tx:3 --log-level=pmd.net.ena.com:4
```

Or for the whole component:

```sh
# Change all ENA logs to the highest level
./dpdk-app --log-level=pmd.net.ena.*:8
```

### 7.3. DPDK v21.08+

In DPDK v21.08 the ENA logging system was reworked and both flag requirements
and the available loggers changed. The way of enabling the logs for the meson
build system is exactly the same as previously.

#### 7.3.1. Loggers runtime configuration

Available dynamic loggers names with the required flag dependency can be found
in the table below.

| DPDK version | Logger name           | Dependency                 | Purpose                 |
|--------------|-----------------------|----------------------------|-------------------------|
| v18.02       | `pmd.net.ena.init`    | -                          | PMD initialization logs |
| v18.02       | `pmd.net.ena.driver`  | -                          | General driver logs     |
| v21.08       | `pmd.net.ena.com`     | -                          | ena_com layer logs      |
| v21.08       | `pmd.net.ena.tx`      | `RTE_ETHDEV_DEBUG_TX`      | Tx path debugging logs  |
| v21.08       | `pmd.net.ena.rx`      | `RTE_ETHDEV_DEBUG_RX`      | Rx path debugging logs  |

## 8. Driver statistics

ENA PMD supports two kind of statistics:

- Generic statistics
- Extended statistics (xstats) available since ENA v2.0.0

### 8.1. Generic statistics

To get the generic driver's statistics, use
[`rte_eth_stats_get()` function](https://doc.dpdk.org/api/structrte__eth__stats.html).

Please note, that currently this function cannot be called from the secondary
process in the MP mode, as it must use the ENA's admin queue, which may work
only in the primary process.

ENA PMD supports per-queue statistics since ENA v2.0.0. However
DPDK limits its number to 16 by default. Some of ENA devices can support up to
32 IO queues, so in that case only the first 16 queues will return the per-queue
stats.

In order to increase number of available per-queue counters, constant
`RTE_ETHDEV_QUEUE_STAT_CNTRS` must be changed to some higher value (for
example: `32`).

### 8.2. Extended statistics (xstats)

Since DPDK v19.02, ENA PMD supports xstats. Except the standard xstats provided
by the DPDK, ENA provides extra ones:

#### 8.2.1. General statistics

Statistics global for the driver.

- `wd_expired` - The number of times keep alive watchdog has expired.
- `dev_start` - The number of times the device was started.
- `dev_stop` - The number of times the device was stopped.
- `tx_drops` - (since ENA v2.0.3) The number of Tx packets dropped
  by the HW. Device can start dropping Tx packets if the driver will push more data
  than the link can handle.

#### 8.2.2. ENI limiters

Counters provided by the ENA device, notifies user about exceeding performance
limiters. More details about those metrics can be read in the
[related AWS blog post](https://aws.amazon.com/blogs/networking-and-content-delivery/amazon-ec2-instance-level-network-performance-metrics-uncover-new-insights/).
Available since ENA v2.2.0.

- `bw_in_allowance_exceeded` - The number of packets queued or dropped because
  the inbound aggregate bandwidth exceeded the maximum for the instance.
- `bw_out_allowance_exceeded` - The number of packets queued or dropped because
  the outbound aggregate bandwidth exceeded the maximum for the instance.
- `pps_allowance_exceeded` - The number of packets queued or dropped because the
  bidirectional PPS exceeded the maximum for the instance.
- `conntrack_allowance_exceeded` -The number of packets dropped because
  connection tracking exceeded the maximum for the instance and new connections
  could not be established. This can result in packet loss for traffic to or
  from the instance.
- `linklocal_allowance_exceeded` - The number of packets dropped because the PPS
  of the traffic to local proxy services exceeded the maximum for the network
  interface. This impacts traffic to the DNS service, the Instance Metadata
  Service, and the Amazon Time Sync Service.

#### 8.2.3. Tx per-queue statistics

Each Tx queue provides below set of statistics. Each statistic described below
has "tx_qX_" prefix, where 'X' stands for the queue ID.

- `cnt` - The number of packets were transmitted by this queue.
- `bytes` - The number of bytes were transmitted by this queue.
- `prepare_ctx_err` - The number of failures of the Tx routine which prepares
  packets for the hardware. This kind of error is a reset condition.
- `linearize` - The number of times Tx packet has been linearized by the driver
  because it had too much segments.
- `linearize_failed` - The number of times linearization of the Tx packet has
  failed.
- `tx_poll` -  The number of times the Tx polling function has been called.
- `doorbells` - The number of times the hardware doorbell has been written.
- `bad_req_id` - The number of times invalid Tx request ID has been detect. It
  is a reset condition.
- `available_desc` - The number of Tx descriptors that has been available last
  time, the Tx burst function has been called. Rough calculations for devices
  using LLQ.

#### 8.2.4. Rx per-queue statistics

Each Rx queue provides below set of statistics. Each statistic described below
has "rx_qX_" prefix, where 'X' stands for the queue ID.

- `cnt` - The number of packets were received by this queue.
- `bytes` - The number of bytes were received by this queue.
- `refill_partial` - The number of times Rx ring has been partially refilled
  because the descriptor couldn't be passed to the HW.
- `bad_csum` - The number of times invalid Rx checksum was detected by the
  device.
- `mbuf_alloc_fail` - The number of times memory pool couldn't provide enough
  mbufs to refill the Rx ring.
- `bad_desc_num` - The number of times Rx packets couldn't be retrieved from the
  HW because it had too many Rx descriptors. It is a reset condition.
- `bad_req_id` - The number of times Rx packets couldn't be retrieved from the
  HW because it had invalid Rx request ID. It is a reset condition.

## 9. Device reset and the timer service

ENA supports health checks which can be used to detect faulty behavior of the
hardware and the driver, including:

- HW unresponsiveness – Driver detected that the periodic keep alive signals
  stopped arriving from the HW through the Asynchronous Event Notification Queue
  (AENQ).
- Faulty admin queue behavior – The admin queue entered a faulty state.
- Faulty behavior on the IO path – The  device returned invalid descriptors or
  caused the driver to enter the invalid state.
- Missing Tx completions exceeds a dynamically calculated threshold - Prevent Tx
  ring stalls in case Tx descriptors were not returned by the HW.


In order to make both of those features work, the application perform steps
listed below:

1. Bind ENA device to the kernel driver which supports interrupts (both
   `igb_uio` and `vfio-pci` should work properly).
2. Application should periodically call `rte_timer_manage()` function (1 second
   frequency is recommended). This shall trigger `ena_timer_wd_callback()`
   routine execution which performs all the health checks no more often than
   once a second.
3. Register event callback and check for the event `RTE_ETH_EVENT_INTR_RESET`
4. If the above event was detected, prepare the application for the device reset
   and perform it using the `rte_eth_dev_reset()` function.

## 10. Multi process (MP) support

Currently MP support is __*experimental*__ - this means, that the driver does
not guarantee all calls are MP safe and it is not performing all the required
verification checks for the MP mode in all the places.

As many of the driver's requests to the hardware are performed using the admin
queue which requests cannot be handled from the secondary process, all the API
which must use it will not be supported from the secondary process.

Some API is clear about it's usage in the secondary process, but other is not.
To make it more clear, below is the list of the generic API which should not
be used from the secondary process in the DPDK v21.08:

- `rte_eth_stats_get()` - To get information about statistics, the ENA admin
  command needs to be send.
- `rte_eth_xstats_get()` - To get information about ENI limiters, the ENA admin
  command needs to be send.
- `rte_eth_xstats_get_by_id()` - It's not allowed only if the ENI limiters are
  being requested from the secondary process.

## 11. RSS support

Depending on the used instance type (which determines ENA hardware version),
ENA may support different RSS (Receive Side Scaling) reconfiguration features,
as shown in the table below. The table shows how ENA RSS features names
corresponds with the DPDK features and when support for the given features was
added.

| ENA feature                    | DPDK feature    | Available since          | Support in HW |
|--------------------------------|-----------------|--------------------------|---------------|
| RSS                            | RSS hash        | DPDK v16.04              | All HW types  |
| Indirection table update       | RSS reta update | DPDK v16.04              | All HW types  |
| RSS hash function (key) update | RSS key update  | DPDK v21.08 (ENA v2.4.0) | Subset of HW  |
| RSS hash fields set            | RSS key update  | DPDK v21.08 (ENA v2.4.0) | None          |

### 11.1. RSS feature

ENA driver supports RSS. In order to activate it, flag `RTE_ETH_MQ_RX_RSS_FLAG`
must be passed to the `dev_conf.rxmode.mq_mode` field and at least 1 Rx queue
must be configured. Enabling RSS allows the hardware to use multiple Rx queues
and redirect flow depending on the calculated hash, indirection table values and
the supported packet types.

When this mode is being activated, driver will automatically
enable offload `RTE_ETH_RX_OFFLOAD_RSS_HASH` and pass RSS hash value to the
mbuf.

By default RSS hash calculation works only for the TCP and UDP packets.

### 11.2. Indirection table update

Applications may distribute traffic across the queues by using the indirection
table (in the DPDK it's also being called the redirection table). The table
consists of 128 elements and each one of them holds the Rx queue ID.
Modulo operation `pkt_hash % 128` gives indirection table index, which contains
the Rx queue number to which the HW will steer the received packet

Indirection table is filled with Rx queue IDs using the round robin technique.
For example, if there are 4 Rx queues used by the application, the first 12
values of this table will look like this from the application perspective:
`[0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3]`. ENA HW indirection table will hold
different values as it uses common ring structure definition for Tx and Rx.
However it's not important from the application point of view.

The indirection table can be modified using the `rte_eth_dev_rss_reta_update()`
function and acquired by calling `rte_eth_dev_rss_reta_query()`.

The script
[toeplitz_calc.py](https://github.com/amzn/amzn-ec2-ena-utilities/tree/main/ena-toeplitz)
can be very handy when the indirection table is being optimized for the known
flows.

### 11.3. RSS hash key update

Since DPDK v21.08 and ENA v2.4.0 ENA supports modification of the RSS hash key
for the Toeplitz hashing function.

Before ENA v2.4.0, the HW used the static key below:

```text
be:ac:01:fa:6a:42:b7:3b:80:30:f2:0c:77:cb:2d:a3:ae:7b:30:b4:d0:ca:2b:cb:43:a3:8f:b0:41:67:25:3d:25:5b:0e:c2:6d:5a:56:da
```

ENA v2.4.0+ PMD generates the RSS hash key randomly each time the application is
being executed. In order to get the predictable flow-to-queue mapping,
application should set it's own RSS hash key, by filling the `struct
rte_eth_rss_conf` like below:

- `.rss_key` - pointer to the 40 bytes table containing RSS hash key
- `.rss_key_len` - size of the RSS key, which must be 40

If the hardware doesn't support RSS hash fields modification, the field
`.rss_hf` can be set to `0`.

Filled structure should be passed to the function
`rte_eth_dev_rss_hash_update()`.

In case RSS HF update is not supported, the user will see warning message like:

```text
Setting RSS hash fields is not supported. Using default values: 0xc30
```

But the return code from the function will be 0, as otherwise it won't be
possible to reconfigure RSS hash key without RSS HF support in the HW.

### 11.4. RSS hash fields modification

This feature was introduced in the DPDK v21.08 and ENA v2.4.0, but it is not
supported by the currently available ENA hardware.

Hash fields determine the packet types for which the hash function is calculated
(effectively defining the packet types for which RSS will be used).

Packets for which the hash function is not used are always received on the
queue 0.

If this feature is not supported, the hardware will use RSS for the below type
of packets:

```text
RTE_ETH_RSS_NONFRAG_IPV4_TCP
RTE_ETH_RSS_NONFRAG_IPV4_UDP
RTE_ETH_RSS_NONFRAG_IPV6_TCP
RTE_ETH_RSS_NONFRAG_IPV6_UDP
```

The application can always probe the supported hash functions by using the
API `rte_eth_dev_rss_hash_conf_get()` and reading the `.rss_hf` field in the
structure `rte_eth_rss_conf`. However attempt to modify `.rss_hf` in the call
`rte_eth_dev_rss_hash_update()` won't succeed if it lacks HW support and can
result in:

- error, if the `.rss_key` wasn't provided as well,
- error, if the `.rss_hf` exceeds HW capabilities,
- success, if the `.rss_key` was provided.

The last situation is a workaround around DPDK API for the devices which
supports hash key change, but doesn't support HF modification.

## 12. Performance tuning

Applying below tips allows the application to achieve optimal packets per second
and bandwidth values.

To get the reference values of what AWS instances are capable of, please refer
the [ENA DTS](https://github.com/amzn/amzn-ec2-ena-utilities/tree/main/ena-dts) -
this tool based on the DTS (DPDK Test Suite), which generates optimized flows
to get the best results. This tool can measure PPS, BW and latency.

To get more details about instance network bandwidth tuning, please refer to
the
[Amazon EC2 instance network bandwidth guide](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-instance-network-bandwidth.html).

### 12.1. Tx path

- Bandwidth for single flow is limited to 5-10 Gbps. Use multiple flows
  to overcome this limitation.
- For multiple flow traffic, it should be spread across multiple Tx queues.
- Single Tx flow should be pinned to one queue.
- Processing only one flow in a bulk will be faster as the queue can cache the
  flow's meta data.
- Use the jumbo frames in order to get the best bandwidth.
- Limit global Tx rate of the application to the instance's upper Tx limit. For
  example: for the instance capable of getting 100 Gbps set the max Tx rate to
  100 Gbps and do not call Tx burst function if this rate is exceeded. ENAv2
  hardware can accept packets beyond the link limit, but they will be simply
  dropped and the hardware will waste time processing those unsent packets.

### 12.2. Rx path

- Configure the RSS (Receive Side Scaling) for the Rx path in order to use
  multiple Rx queues.
- Use the jumbo frames in order to get the best bandwidth.
- If Rx drops can be observed, increase the Rx ring size.
- Use the RSS indirection table to redirect some of the flows from very busy
  queues, to the ones which are idle. To get the hash which will be calculated
  by the hardware for the given flow, the script
  [toeplitz_calc.py](https://github.com/amzn/amzn-ec2-ena-utilities/tree/main/ena-toeplitz)
  can be used.

## 13. General FAQ

__Q:__ _My application uses a watchdog which is timing out. The traces are
showing that function `ena_com_execute_admin_command` is causing a timeout._

__A:__ It is a known issue which exists in the mainline DPDK versions prior to
v21.05. It is caused by the PMD which wasn't handling the conditional variable
properly. If the thread sending admin command is slower than the interrupt
thread, the conditional variable can be set before the main thread even achieves
the `pthread_cond_timedwait` function. As a result, it will cause this thread to
wait for the full timeout value, which can be a few seconds value.

To fix that,
[the upstream commit](https://git.dpdk.org/dpdk/commit/?id=072b9f2bbc2402a8c86194fe9e11458c1605540a)
`072b9f2bbc2402a8c86194fe9e11458c1605540a` should be applied to the working DPDK
repository.

---

__Q:__ _I am getting dmesg error "`vfio-pci: probe of <pci_id> failed with error
-22`" while trying to bind ENA to the `vfio-pci` driver._

__A:__ Most likely you are trying to bind ENA on no-IOMMU machine to the
`vfio-pci` which is working in IOMMU mode. Please change `vfio-pci`
[to work in the no-IOMMU mode](#622-vfio-pci).

---

__Q:__ _Which IO kernel modules does the ENA PMD support?_

__A:__ Currently only `igb_uio` and `vfio-pci` are supported by the ENA PMD.
Other modules may not attach properly or may cause disfunction of the driver.
Please refer to the [appropriate section](#6-vfio-pci-and-igb_uio) for more
details about those modules.

---

__Q:__ _Where can I find the `igb_uio` kernel module?_

__A:__ The latest `igb_uio` module now can be found in the
[dpdk-kmods repository](https://git.dpdk.org/dpdk-kmods/).

Since DPDK vXX.XX `igb_uio` is no longer part of the DPDK main tree. However
this standalone version is recommended even for the users using the older DPDK
versions as it provides the latest updates and fixes.

---

__Q:__ _How can I get the `vfio-pci` kernel module?_

__A:__ `vfio-pci` comes with the kernel. However it doesn't support no-IOMMU
mode nor the Write Combining. To get the patched `vfio-pci` for your kernel,
[the script](enav2-vfio-patch/get-vfio-with-wc.sh) in the amzn-drivers repository
can be used.

---

__Q:__ _Which module should I use - `igb_uio` or `vfio-pci`?_

__A:__ It's up to the user which one to use. `igb_uio` is simpler but it is
providing less security because lack of the IOMMU support. Currently only
`*.metal` AWS instances provides IOMMU support and if it's enabled, the
`vfio-pci` is the only valid option.

To use `vfio-pci` on systems without IOMMU it must be rebuilt with
`CONFIG_VFIO_NOIOMMU` flag and no-IOMMU mode of the `vfio-pci` must be enabled.

Also `vfio-pci` by default doesn't support Write Combining, what can affect
driver's network performance. It should be enabled with the patch located
[in this repository](enav2-vfio-patch).

Rx interrupt feature of the ENA PMD will work only with the `vfio-pci` module,
as it provides more than one interrupt. The first interrupt is always reserved
for the ENA's admin queue.

---

__Q:__ _Which platforms are supported by the ENA PMD?_

__A:__ Although DPDK can be built on Linux, FreeBSD and Windows, currently
officially only the Linux is supported. However it doesn't mean that the driver
won't work properly on other platforms - we just can't guarantee it will work
properly with all the features and to provide support for them.

---

__Q:__ _I'm using ENAv2 and I need to handle Tx packets with headers size larger
than 96B. How can I use Large LLQ Headers feature in the DPDK?_

__A:__ In order to use the Large LLQ Headers (headers with size > 96B and
<= 224B), the request must be passed to PMD using the "Device Arguments" feature
which is being passed as an EAL argument.

This device argument is called `large_llq_hdr` and must be set to `1`.
Please refer to the [appropriate section](#51-runtime-options-devargs) for more
details.

It's fully functional since DPDK release v21.05 and LTS release v20.11.2.

---

__Q:__ _How can I get the latest ENA driver?_

__A:__ The latest ENA driver is always available on the `main` branch of the
[DPDK repository](https://git.dpdk.org/). It contains all the latest features
and the fixes which currently aren't available for the previous versions.

Please refer to the [quick start guide](#4-quick-start-guide) for instructions how
to get the latest DPDK repository.

---

__Q:__ _I just checked out to the latest DPDK and I cannot use Makefile to build
the framework. Why is that?_

__A:__ Makefile has been deprecated at the DPDK v20.08 and since v20.11 only the
meson build system is available.

Meson was introduced in DPDK v18.02 and was slowly becoming the main build
system. Please refer to the DPDK build guide #link# for more details how to use
the meson.

---

__Q:__ _Can I use ENA on arm64 AWS instances?_

__A:__ Yes, ENA supports arm64 instances officially since DPDK v20.11. However,
for the Graviton2 (instances like c6g, c6gn) the DPDK v21.02 is needed, as
previous versions aren't generating project configuration properly, which may
affect available number of lcores and the overall performance.

---

__Q:__ _When I'm trying to configure RSS hash fields I can see ENA message:
`Setting RSS hash fields is not supported. Using default values: 0xc30`. Is it
some kind of error?_

__A:__ ENA device, depending on the HW version, may support only some subset of
the RSS configuration features:

- Indirection (redirection) table reconfiguration
- RSS hash key reconfiguration
- RSS hash fields (functions) reconfiguration

While for the device those are 3 independent features, for the DPDK both RSS
hash key and hash function reconfiguration is performed by filling the common
structure `struct rte_eth_rss_conf` and calling function
`rte_eth_dev_rss_hash_update()`. As a result the driver must support both of
those features even if they're not available in the HW.

If one of the above features won't be supported by the HW, the
`rte_eth_dev_rss_hash_update()` will only return error in some cases, and in
others it will return a warning messages, as shown in a table below

|Case | RSS hash key | RSS hash functions | Action       | Result            |
|-----|--------------|--------------------|--------------|-------------------|
| 1.  | unsupported  | unsupported        | any          | Return `-ENOTSUP` |
| 2.  | supported    | unsupported        | set hf       | Return `-ENOTSUP` |
| 3.  | supported    | unsupported        | set key + hf | Return `0` and print warning message: `Setting RSS hash fields is not supported. Using default values: 0xc30` |
| 4.  | supported    | supported          | any          | Return `0`        |

To allow change of the RSS hash key even though the hash functions cannot be
reconfigured, this API call cannot return an error. Otherwise the application
won't be able to detect the use case when the hash key has been modified
successfully.

In order to notify the user that his request was only partially completed, the
driver prints the warning message about lack of RSS hf reconfiguration support
and the default values used by the hardware.

Whenever application tries to retrieve RSS configuration and RSS hash function
modification is not supported, user may see warning message: `Reading hash
control from the device is not supported. .rss_hf will contain a default
value.` - however it will be printed only for the first time the API
`rte_eth_dev_rss_hash_conf_get()` is being called.

## 14. Performance FAQ

__Q:__ _Upon profiling I've noticed that `ena_com_prep_pkts` is taking a lot of
CPU time._

__A:__ Most likely you have loaded `igb_uio` or `vfio-pci` modules without Write
Combining support. This can lead to a poor performance when ENA is sending LLQ
(Low Latency Queue) buffers to the hardware, leading to high CPU utilization in
the `ena_com_prep_pkts` function. Please refer to the
[appropriate section](#61-enav2--v200-and-writecombining) for more details.

---

__Q:__ _I can see that Tx is higher than the instance capability (i.e. 120 Gbps
instead of 100 Gbps). However on the Rx side I can only see some of those
packets. Is ENA capable of sending more than the instance's link speed?_

__A:__ ENAv2 device is capable of accepting more Tx buffers than it can actually
send over the link but it will result in drop of the excessive Tx packets.
Leading to situation like that can reduce amount of the effective sent packets
as the hardware will be busy dropping the packets.

To get the most from the hardware, the application should limit it's Tx to
around 100% of the link speed instead of constantly calling PMD Tx burst when
it's exceeded.

---

__Q:__ _I'm using [pktgen-dpdk](https://git.dpdk.org/apps/pktgen-dpdk/) for
checking instance's performance capability. However the achieved Tx and Rx
values aren't as high as expected._

__A:__ Unpatched version of the pktgen doesn't work well as is with the ENA hardware:

- Pktgen shall be limited to the default 10 Gbps Tx rate unless it is manually
  patched. Pktgen uses the default Tx rate since the actual Tx rate calculation
  requires the link speed which the ENA does not expose (virtual device).
- A simple pktgen setup generates only a single flow which is limited to 5-10
  Gbps on EC2. Increasing number of queues will not improve performance since
  all the queues will relate to this single flow. This can be mitigated by
  either using the pktgen's `range` feature or by using the custom pcap files.
- Each queue generates all the flows from the pcaps or the `range` feature. To
  mitigate this, use pcap files, where each pcap file holds only one flow with
  multiple packets (which will increase Tx burst size) and bind each pcap file
  to a different Tx queue.
- Pktgen does not use jumbo frames by default.

Please refer to the [performance tuning](#12-performance-tuning) section for
more details.

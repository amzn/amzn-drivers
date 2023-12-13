.. SPDX-License-Identifier: GPL-2.0

============================================================
Linux kernel driver for Elastic Network Adapter (ENA) family
============================================================

Overview
========

ENA is a networking interface designed to make good use of modern CPU
features and system architectures.

The ENA device exposes a lightweight management interface with a
minimal set of memory mapped registers and extendible command set
through an Admin Queue.

The driver supports a range of ENA devices, is link-speed independent
(i.e., the same driver is used for 10GbE, 25GbE, 40GbE, etc), and has
a negotiated and extendible feature set.

Some ENA devices support SR-IOV. This driver is used for both the
SR-IOV Physical Function (PF) and Virtual Function (VF) devices.

ENA devices enable high speed and low overhead network traffic
processing by providing multiple Tx/Rx queue pairs (the maximum number
is advertised by the device via the Admin Queue), a dedicated MSI-X
interrupt vector per Tx/Rx queue pair, adaptive interrupt moderation,
and CPU cacheline optimized data placement.

The ENA driver supports industry standard TCP/IP offload features such as
checksum offload. Receive-side scaling (RSS) is supported for multi-core
scaling.

The ENA driver and its corresponding devices implement health
monitoring mechanisms such as watchdog, enabling the device and driver
to recover in a manner transparent to the application, as well as
debug logs.

Some of the ENA devices support a working mode called Low-latency
Queue (LLQ), which saves several more microseconds.

Driver compilation
===================================

Prerequisites:
--------------

Amazon Linux
````````````
.. code-block:: shell

  sudo yum update
  sudo reboot
  sudo yum install kernel-devel-$(uname -r) git

RHEL
````
.. code-block:: shell

  sudo yum update
  sudo reboot
  sudo yum install gcc kernel-devel-$(uname -r) git

Ubuntu
``````
.. code-block:: shell

  sudo apt-get update
  sudo apt install linux-headers-$(uname -r)
  sudo apt-get install make gcc

SLES
````
.. code-block:: shell

  sudo zypper update
  sudo reboot
  sudo zypper install make gcc kernel-default-devel git


CentOS
```````
.. code-block:: shell

  sudo yum update
  sudo reboot
  sudo yum install kernel-devel-$(uname -r) git

Compilation:
------------

Run

.. code-block:: shell

  make [UBUNTU_ABI=<ABI>]

in the **kernel/linux/ena/** folder.
**ena.ko** is created inside the folder

**Optional compilation parameters:**

For most kernel versions no special compilation parameters are needed.
The exceptions are:

- :code:`UBUNTU_ABI=<ABI>`
   For Ubuntu kernel versions < 3.13.0-31 add the special parameter
   ``UBUNTU_ABI=<ABI>``. The ABI of an ubuntu kernel is the 4th integer in the
   kernel version string. To see the kernel version you can run :code:`uname -r`.

   Example:
   if :code:`uname -r` yields the output ``3.13.0-29-generic``, then the ABI is 29,
   and the compilation command is :code:`make UBUNTU_ABI=29`.

Loading driver:
---------------
If the driver was compiled using ``ENA_PHC_INCLUDE`` environment variable set then
ptp module might need to be loaded prior to loading the driver (see `PHC`_ for more info).

ENA driver can be (re)loaded using:

.. code-block:: shell

  sudo rmmod ena && sudo insmod ena.ko


Please note, the following messages might appear during OS boot::

  ena: loading out-of-tree module taints kernel.
  ena: module verification failed: signature and/or required key missing - tainting kernel

These messages are informational and indicate that out-of-tree driver is being
used, and do not affect driver operation.


Driver installation using dkms
==============================
.. _`supported instances`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enhanced-networking-ena.html#ena-requirements
.. _`test-enhanced-networking-ena`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enhanced-networking-ena.html#test-enhanced-networking-ena

Please refer to `supported instances`_ for the list of instance types supporting ENA.

Please also make sure Enhanced Networking is enabled on your instance as specified in `test-enhanced-networking-ena`_.

Installing dkms:
----------------

Amazon Linux
````````````
.. code-block:: shell

  sudo yum install dkms

RHEL
````
.. code-block:: shell

  # Replace the 7 with the RHEL version you have (For example - for RHEL 9 use 9)
  sudo yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
  sudo yum install dkms

Ubuntu
``````
.. code-block:: shell

  sudo apt-get install dkms

SLES 15
```````
.. code-block:: shell

  # Replace the 15.4 with the correct SLES 15 version
  # For arm instances replace x86_64 with aarch64
  sudo SUSEConnect --product PackageHub/15.4/x86_64
  sudo zypper refresh
  sudo zypper install dkms

CentOS
```````
.. code-block:: shell

  sudo yum install --enablerepo=extras epel-release
  sudo yum install dkms

Installation from Source Code
`````````````````````````````
.. code-block:: shell

  git clone https://github.com/dell/dkms.git
  cd dkms
  sudo make install

Installing Driver with dkms:
----------------------------
.. code-block:: shell

  git clone https://github.com/amzn/amzn-drivers.git
  sudo mv amzn-drivers /usr/src/amzn-drivers-X.Y.Z (X.Y.Z = driver version)
  sudo vi /usr/src/amzn-drivers-X.Y.Z/dkms.conf

  # Paste the following and save the file:

  PACKAGE_NAME="ena"
  PACKAGE_VERSION="1.0.0"
  CLEAN="make -C kernel/linux/ena clean"
  MAKE="make -C kernel/linux/ena/ BUILD_KERNEL=${kernelver}"
  BUILT_MODULE_NAME[0]="ena"
  BUILT_MODULE_LOCATION="kernel/linux/ena"
  DEST_MODULE_LOCATION[0]="/updates"
  DEST_MODULE_NAME[0]="ena"
  REMAKE_INITRD="yes"
  AUTOINSTALL="yes"

  sudo dkms add -m amzn-drivers -v X.Y.Z
  sudo dkms build -m amzn-drivers -v X.Y.Z
  sudo dkms install -m amzn-drivers -v X.Y.Z

  # NOTE - To have the driver installation retain across reboots
  # please go over the next section as well.

Making Sure the Installed Driver is Loaded During Boot:
-------------------------------------------------------
In some cases it is necessary to perform some extra steps to make
sure that the installed driver loads during boot.

RHEL and SLES 12
````````````````
.. code-block:: shell

  sudo dracut -f -v

SLES
````
.. code-block:: shell

  sudo vi /etc/modprobe.d/10-unsupported-modules.conf

  # Paste the following into file and save it:

  allow_unsupported_modules 1

Loading the Newly Installed Driver:
-----------------------------------
You can either reboot the instance:

.. code-block:: shell

  sudo reboot

Or simply reload the driver:

.. code-block:: shell

  sudo modprobe -r ena; sudo modprobe ena

Module Parameters
=================

:rx_queue_size:
  Controls the number of requested entries in the Rx
  Queue. Increasing the Rx queue size can be useful in situations
  where rx drops are observed in loaded systems with NAPI not scheduled
  fast enough. The value provided will be rounded down to a power of 2.
  Default value 1024. Max value is up to 16K (16384), depending on the
  instance type, and the actual value can be seen by running :code:`ethtool -g`.
  The Min value is 256. The actual number of entries in the queues is
  negotiated with the device.

:force_large_llq_header:
  Controls the maximum supported packet header
  size when LLQ is enabled. When this parameter is set to 0 (default
  value), the maximum packet header size is set to 96 bytes. When this
  parameter is set to a non 0 value, the maximum packet header size is
  set to 224 bytes, and the Tx queue size is reduced by half.

:num_io_queues:
  Controls the number of requested IO queues. The maximum
  possible number is set to be MIN(maximum IO queues supported by the
  device, number of MSI-X vectors supported by the device, number of online
  CPUs). The minimum number of queues is 1. If the number of queues given is
  outside of the range, the number of queues will be set to the closest
  number from within the range.

:lpc_size:
  Controls the size of the Local Page Cache size which would be
  ``lpc_size * 1024``. Maximum value for this parameter is 32, and a value of 0
  disables it completely. The default value is 2. See LPC section in this README
  for a description of this system.

:phc_enable:
  Controls the enablement of the PHC feature. The default value is 0 (Disabled).
  Notice that PHC must be supported by the kernel and the device.

Disable Predictable Network Names:
==================================

When predictable network naming is enabled, Linux might change the
device name and affect the network configuration.
This can lead to a loss of network on boot.
To disable this feature add :code:`net.ifnames=0` to the kernel boot params.


Edit **/etc/default/grub** and add ``net.ifnames=0`` to ``GRUB_CMDLINE_LINUX``.
On Ubuntu run :code:`update-grub` as well

ENA Source Code Directory Structure
===================================

=================   ======================================================
ena_com.[ch]        Management communication layer. This layer is
                    responsible for the handling all the management
                    (admin) communication between the device and the
                    driver.
ena_eth_com.[ch]    Tx/Rx data path.
ena_admin_defs.h    Definition of ENA management interface.
ena_eth_io_defs.h   Definition of ENA data path interface.
ena_common_defs.h   Common definitions for ena_com layer.
ena_regs_defs.h     Definition of ENA PCI memory-mapped (MMIO) registers.
ena_netdev.[ch]     Main Linux kernel driver.
ena_sysfs.[ch]      Sysfs files.
ena_lpc.[ch]        Local Page Cache files (see `LPC`_ for more info)
ena_ethtool.c       ethtool callbacks.
ena_xdp.[ch]        XDP files
ena_pci_id_tbl.h    Supported device IDs.
ena_phc.[ch]        PTP hardware clock infrastructure (see `PHC`_ for more info)
=================   ======================================================

Management Interface:
=====================

ENA management interface is exposed by means of:

- PCIe Configuration Space
- Device Registers
- Admin Queue (AQ) and Admin Completion Queue (ACQ)
- Asynchronous Event Notification Queue (AENQ)

ENA device MMIO Registers are accessed only during driver
initialization and are not used during further normal device
operation.

AQ is used for submitting management commands, and the
results/responses are reported asynchronously through ACQ.

ENA introduces a small set of management commands with room for
vendor-specific extensions. Most of the management operations are
framed in a generic Get/Set feature command.

The following admin queue commands are supported:

- Create I/O submission queue
- Create I/O completion queue
- Destroy I/O submission queue
- Destroy I/O completion queue
- Get feature
- Set feature
- Configure AENQ
- Get statistics

Refer to **ena_admin_defs.h** for the list of supported Get/Set Feature
properties.

The Asynchronous Event Notification Queue (AENQ) is a uni-directional
queue used by the ENA device to send to the driver events that cannot
be reported using ACQ. AENQ events are subdivided into groups. Each
group may have multiple syndromes, as shown below

The events are:

====================    ===============
Group                   Syndrome
====================    ===============
Link state change       **X**
Fatal error             **X**
Notification            Suspend traffic
Notification            Resume traffic
Keep-Alive              **X**
====================    ===============

ACQ and AENQ share the same MSI-X vector.

Keep-Alive is a special mechanism that allows monitoring the device's health.
A Keep-Alive event is delivered by the device every second.
The driver maintains a watchdog (WD) handler which logs the current state and
statistics. If the keep-alive events aren't delivered as expected the WD resets
the device and the driver.

Data Path Interface
===================

I/O operations are based on Tx and Rx Submission Queues (Tx SQ and Rx
SQ correspondingly). Each SQ has a completion queue (CQ) associated
with it.

The SQs and CQs are implemented as descriptor rings in contiguous
physical memory.

The ENA driver supports two Queue Operation modes for Tx SQs:

- **Regular mode:**
  In this mode the Tx SQs reside in the host's memory. The ENA
  device fetches the ENA Tx descriptors and packet data from host
  memory.

- **Low Latency Queue (LLQ) mode or "push-mode":**
  In this mode the driver pushes the transmit descriptors and the
  first few bytes of the packet (negotiable parameter)
  directly to the ENA device memory space.
  The rest of the packet payload is fetched by the device.
  For this operation mode, the driver uses a dedicated PCI
  device memory BAR, which is mapped with write-combine capability.

  **Note that** not all ENA devices support LLQ, and this feature is negotiated
  with the device upon initialization. If the ENA device does not
  support LLQ mode, the driver falls back to the regular mode.

The Rx SQs support only the regular mode.

The driver supports multi-queue for both Tx and Rx. This has various
benefits:

- Reduced CPU/thread/process contention on a given Ethernet interface.
- Cache miss rate on completion is reduced, particularly for data
  cache lines that hold the sk_buff structures.
- Increased process-level parallelism when handling received packets.
- Increased data cache hit rate, by steering kernel processing of
  packets to the CPU, where the application thread consuming the
  packet is running.
- In hardware interrupt re-direction.

Interrupt Modes
===============

The driver assigns a single MSI-X vector per queue pair (for both Tx
and Rx directions). The driver assigns an additional dedicated MSI-X vector
for management (for ACQ and AENQ).

Management interrupt registration is performed when the Linux kernel
probes the adapter, and it is de-registered when the adapter is
removed. I/O queue interrupt registration is performed when the Linux
interface of the adapter is opened, and it is de-registered when the
interface is closed.

The management interrupt is named::

   ena-mgmnt@pci:<PCI domain:bus:slot.function>

and for each queue pair, an interrupt is named::

   <interface name>-Tx-Rx-<queue index>

The ENA device operates in auto-mask and auto-clear interrupt
modes. That is, once MSI-X is delivered to the host, its Cause bit is
automatically cleared and the interrupt is masked. The interrupt is
unmasked by the driver after NAPI processing is complete.

Interrupt Moderation
====================

ENA driver and device can operate in conventional or adaptive interrupt
moderation mode.

**In conventional mode** the driver instructs device to postpone interrupt
posting according to static interrupt delay value. The interrupt delay
value can be configured through ``ethtool(8)``. The following ``ethtool``
parameters are supported by the driver: ``tx-usecs``, ``rx-usecs``

**In adaptive interrupt** moderation mode the interrupt delay value is
updated by the driver dynamically and adjusted every NAPI cycle
according to the traffic nature.

Adaptive coalescing can be switched on/off through ``ethtool(8)`` using
:code:`adaptive_rx on|off` parameter.

More information about Adaptive Interrupt Moderation (DIM) can be found in
https://elixir.bootlin.com/linux/latest/source/Documentation/networking/net_dim.rst

.. _`RX copybreak`:

RX copybreak
============

The rx_copybreak is initialized by default to ENA_DEFAULT_RX_COPYBREAK
and can be configured using :code:`ethtool --set-tunable`.
This option is supported for kernel versions 3.18 and newer.
Alternatively copybreak values can be configured by the sysfs path
``/sys/bus/pci/devices/<domain:bus:slot.function>/rx_copybreak``.

This option controls the maximum packet length for which the RX
descriptor it was received on would be recycled. When a packet smaller
than RX copybreak bytes is received, it is copied into a new memory
buffer and the RX descriptor is returned to HW.

.. _`LPC`:

Local Page Cache (LPC)
======================

ENA Linux driver allows to reduce lock contention and improve CPU usage by
allocating Rx buffers from a page cache rather than from Linux memory system
(PCP or buddy allocator). The cache is created and binded per Rx queue, and
pages allocated for the queue are stored in the cache (up to cache maximum
size).

To set the cache size, one can specify ``lpc_size`` module parameter, which would
create a cache that can hold up to ``lpc_size * 1024`` pages for each Rx queue.
Setting it to 0, would disable this feature completely (fallback to regular page
allocations).

The feature can be toggled between on/off state using ethtool private flags,
e.g.

.. code-block:: shell

  ethtool --set-priv-flags eth1 local_page_cache off

The cache usage for each queue can be monitored using :code:`ethtool -S` counters. Where:

- ``rx_queue#_lpc_warm_up`` - number of pages that were allocated and stored in
  the cache
- ``rx_queue#_lpc_full`` - number of pages that were allocated without using the
  cache because it didn't have free pages
- ``rx_queue#_lpc_wrong_numa`` -  number of pages from the cache that belong to a
  different NUMA node than the CPU which runs the NAPI routine. In this case,
  the driver would try to allocate a new page from the same NUMA node instead

Note that ``lpc_size`` is set to 2 by default and cannot exceed 32. Also LPC is
disabled when using XDP or when using less than 16 queue pairs. Increasing the
cache size might result in higher memory usage, and should be handled with care.

.. _`Large LLQ`:

Large Low-Latency Queue (Large LLQ)
===================================

The standard LLQ entry size of 128 bytes allows for a maximum of 96 bytes of
packet header size which sometimes is not enough (e.g. when using tunneling).
Enabling large LLQ by increasing LLQ entry size to 256 bytes, allows a maximum
header size of 224 bytes.
This comes with the penalty of reducing the number of LLQ entries in the
TX queue by 2 (i.e. from 1024 to 512).

This feature is supported from EC2 4th generation instance-types.

Large LLQ configuration
-----------------------

Large LLQ can be configured in several ways:

- **module parameter:**

.. code-block:: shell

  sudo insmod ena.ko force_large_llq_header=1

- **ethtool:**

.. code-block:: shell

  ethtool -G [interface] tx-push-buf-len [number of bytes: 96 or 224]

Note that the minimal requirements for using ethtool are Linux Kernel v6.3 and
above and ethtool v6.4 and above.

- **sysfs:**

.. code-block:: shell

  echo 1 | sudo tee /sys/bus/pci/devices/<domain:bus:slot.function>/large_llq_header
  # for example:
  echo 1 | sudo tee /sys/bus/pci/devices/0000:00:06.0/large_llq_header

After changing LLQ configuration, a log will be printed to indicate whether Large LLQ is enabled or disabled:

.. code-block:: shell

  ENA Large is LLQ enabled/disabled

Large LLQ query
---------------

Large LLQ can be queried in several ways:

- **ethtool:**

Check the value of ``TX push buff len`` under ``Current hardware settings``

.. code-block:: shell

  ethtool -g [interface]

- **sysfs:**

.. code-block:: shell

  cat /sys/bus/pci/devices/<domain:bus:slot.function>/large_llq_header
  # for example:
  cat /sys/bus/pci/devices/0000:00:06.0/large_llq_header

.. _`PHC`:

PTP Hardware Clock (PHC)
======================
.. _`ptp-userspace-api`: https://docs.kernel.org/driver-api/ptp.html#ptp-hardware-clock-user-space-api
.. _`testptp`: https://elixir.bootlin.com/linux/latest/source/tools/testing/selftests/ptp/testptp.c

ENA Linux driver support PTP hardware clock providing timestamp reference to achieve nanosecond accuracy.

**PHC support**

PHC depends on the PTP module, which needs to be either loaded as a module or compiled into the kernel.

Verify if the PTP module is present:

.. code-block:: shell

  grep -w '^CONFIG_PTP_1588_CLOCK=[ym]' /boot/config-`uname -r`

- If no output is provided, the ENA driver cannot be loaded with PHC support.

- ``CONFIG_PTP_1588_CLOCK=y``: the PTP module is already compiled and loaded inside the kernel binary file.

- ``CONFIG_PTP_1588_CLOCK=m``: the PTP module needs to be loaded prior to loading the ENA driver:

.. code-block:: shell

  sudo modprobe ptp

**PHC compilation**

This feature is enabled only with the ``ENA_PHC_INCLUDE`` environment variable set when compiling
the driver:

.. code-block:: shell

  ENA_PHC_INCLUDE=1 make

**PHC activation**

The feature is turned off by default, in order to turn the feature on, the ENA driver
can be loaded in the following way:

- module parameter:

.. code-block:: shell

  sudo insmod ena.ko phc_enable=1

All available PTP clock sources can be tracked here:

.. code-block:: shell

  ls /sys/class/ptp

PHC support and capabilities can be verified using ethtool:

.. code-block:: shell

  ethtool -T <interface>

**PHC timestamp**

To retrieve PHC timestamp, use `ptp-userspace-api`_, usage example using `testptp`_:

.. code-block:: shell

  ~/linux/tools/testing/selftests/ptp/testptp -d /dev/ptp0 -k 1

**Notice**: PHC get time requests should be within reasonable bounds, avoid excessive utilization to ensure optimal performance and efficiency.

**PHC error bound**

PTP HW clock error bound refers to the maximum allowable difference
between the clock of the device and the reference clock.
The error bound is used to ensure that the clock of the device
remains within a certain level of accuracy relative to the reference
clock. The error bound (expressed in nanoseconds) is calculated by
the device and is retrieved and cached by the driver upon every get PHC
timestamp request.

To retrieve the cached PHC error bound value, use the following:

sysfs:

.. code-block:: shell

  cat /sys/bus/pci/devices/<domain:bus:slot.function>/phc_error_bound

**PHC statistics**

PHC can be monitored using :code:`ethtool -S` counters:

=================   ======================================================
**phc_cnt**         number of successful retrieved timestamps (below expire timeout)
**phc_exp**         number of expired retrieved timestamps (above expire timeout)
**phc_skp**         number of skipped get time attempts (during block period)
**phc_err**         number of failed get time attempts due to timestamp/error bound errors
                    (entering into block state)
                    must remain below 1% of all PHC requests to maintain the desired level of
                    accuracy and reliability
=================   ======================================================

PHC timeouts:

=================   ======================================================
**expire**          max time for a valid timestamp retrieval, passing this threshold will fail
                    the get time request and block new requests until block timeout
**block**           blocking period starts once get time request expires or fails, all get time
                    requests during block period will be skipped
=================   ======================================================

Statistics
==========
.. _`ena-express-monitor`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ena-express.html#ena-express-monitor

The user can obtain ENA device and driver statistics using ``ethtool``.
The driver can collect regular or extended statistics (including
per-queue stats) from the device.

In addition the driver logs the stats to syslog upon device reset.

On supported instance types, the statistics will also include the
ENA Express data (fields prefixed with ``ena_srd``). For a complete
documentation of ENA Express data refer to `ena-express-monitor`_

MTU
===

The driver supports an arbitrarily large MTU with a maximum that is
negotiated with the device. The driver configures MTU using the
SetFeature command (``ENA_ADMIN_MTU`` property). The user can change MTU
via ``ip(8)`` and similar legacy tools.

Stateless Offloads
==================

The ENA driver supports:

- IPv4 header checksum offload
- TCP/UDP over IPv4/IPv6 checksum offloads

RSS
===

- The ENA device supports RSS that allows flexible Rx traffic
  steering.
- Toeplitz and CRC32 hash functions are supported.
- The input to the RSS hash function is {Hash key, Source IP, Destination IP,
  Source Port, Destination Port} (ports are used only for packets that
  have a Transport layer).
- The input to the RSS hash function is not configurable (other than changing
  the hash key for drivers and devices that support it).
- The driver configures RSS settings using the AQ SetFeature command
  (``ENA_ADMIN_RSS_HASH_FUNCTION``, ``ENA_ADMIN_RSS_HASH_INPUT`` and
  ``ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG`` properties).
- If the ``NETIF_F_RXHASH`` flag is set, the 32-bit result of the hash
  function delivered in the Rx CQ descriptor is set in the received
  ``skb``.
- The user can provide a hash key, hash function, and configure the
  indirection table through ``ethtool(8)``.

DATA PATH
=========

Tx
--

:code:`ena_start_xmit()` is called by the stack. This function does the following:

- Maps data buffers (``skb->data`` and frags).
- Populates ``ena_buf`` for the push buffer (if the driver and device are
  in push mode).
- Prepares ENA bufs for the remaining frags.
- Allocates a new request ID from the empty ``req_id`` ring. The request
  ID is the index of the packet in the Tx info. This is used for
  out-of-order Tx completions.
- Adds the packet to the proper place in the Tx ring.
- Calls :code:`ena_com_prepare_tx()`, an ENA communication layer that converts
  the ``ena_bufs`` to ENA descriptors (and adds meta ENA descriptors as
  needed).

  * This function also copies the ENA descriptors and the push buffer
    to the Device memory space (if in push mode).

- Writes a doorbell to the ENA device.
- When the ENA device finishes sending the packet, a completion
  interrupt is raised.
- The interrupt handler schedules NAPI.
- The :code:`ena_clean_tx_irq()` function is called. This function handles the
  completion descriptors generated by the ENA, with a single
  completion descriptor per completed packet.

  * ``req_id`` is retrieved from the completion descriptor. The ``tx_info`` of
    the packet is retrieved via the ``req_id``. The data buffers are
    unmapped and ``req_id`` is returned to the empty ``req_id`` ring.
  * The function stops when the completion descriptors are completed or
    the budget is reached.

Rx
--

- When a packet is received from the ENA device.
- The interrupt handler schedules NAPI.
- The :code:`ena_clean_rx_irq()` function is called. This function calls
  :code:`ena_com_rx_pkt()`, an ENA communication layer function, which returns the
  number of descriptors used for a new packet, and zero if
  no new packet is found.
- :code:`ena_rx_skb()` checks packet length:

  * If the packet is small (len < rx_copybreak), the driver allocates
    an ``skb`` for the new packet, and copies the packet's payload into the
    ``skb``'s linear part.

    - In this way the original data buffer is not passed to the stack
      and is reused for future Rx packets.

  * Otherwise the function unmaps the Rx buffer, sets the first descriptor as ``skb``'s linear part
    and the other descriptors as the ``skb``'s frags.

- The new ``skb`` is updated with the necessary information (protocol,
  checksum hw verify result, etc), and then passed to the network
  stack, using the NAPI interface function :code:`napi_gro_receive()`.

Dynamic RX Buffers (DRB)
------------------------

Each RX descriptor in the RX ring is a single memory page (which is either 4KB
or 16KB long depending on system's configurations).
To reduce the memory allocations required when dealing with a high rate of small
packets, the driver tries to reuse the remaining RX descriptor's space if more
than 2KB of this page remain unused.

A simple example of this mechanism is the following sequence of events:

::

        1. Driver allocates page-sized RX buffer and passes it to hardware
           +----------------------------+
           | 4096 Bytes RX Buffer       |
           +----------------------------+

        2. A 300Bytes packet is received on this buffer

        3. The driver increases the ref count on this page and returns it back to the
           HW as an RX buffer of size 3796 Bytes (4096 - 300)
           +-----+----------------------+
           |*****| 3796 Bytes RX Buffer |
           +-----+----------------------+

This mechanism isn't used when an XDP program is loaded, or when the
RX packet is less than rx_copybreak bytes (in which case the packet is
copied out of the RX buffer into the linear part of a new skb allocated
for it and the RX buffer remains the same size, see `RX copybreak`_).

AF XDP Native Support (zero copy)
---------------------------------

ENA driver supports native AF XDP (zero copy), however the feature is still
experimental.
Please follow https://github.com/amzn/amzn-drivers/issues/221 for possible
mitigations to issues.

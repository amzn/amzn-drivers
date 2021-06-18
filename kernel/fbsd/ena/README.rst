FreeBSD kernel driver for Elastic Network Adapter (ENA) family
==============================================================

Version
-------

``2.4.0``

Supported FreeBSD Versions
--------------------------

**amd64**

* *FreeBSD release/11.0* - and so on
* *FreeBSD 12* - starting from ``rS3091111``
* *FreeBSD 13*
* *FreeBSD 14*

**aarch64**

* *FreeBSD stable/12* - starting from ``r345872``
* *FreeBSD 13* - starting from ``r345371``
* *FreeBSD 14*

Overview
--------

ENA is a networking interface designed to make good use of modern CPU
features and system architectures.

The ENA device exposes a lightweight management interface with a
minimal set of memory mapped registers and extendable command set
through an Admin Queue.

The driver supports a range of ENA devices, is link-speed independent
(i.e., the same driver is used for 10GbE, 25GbE, 40GbE, etc.), and has
a negotiated and extendable feature set.

Some ENA devices support SR-IOV. This driver is used for both the
SR-IOV Physical Function (PF) and Virtual Function (VF) devices.

ENA devices enable high speed and low overhead network traffic
processing by providing multiple Tx/Rx queue pairs (the maximum number
is advertised by the device via the Admin Queue), a dedicated MSI-X
interrupt vector per Tx/Rx queue pair, and CPU cacheline optimized
data placement.

The ENA driver supports industry standard TCP/IP offload features such
as checksum offload and TCP transmit segmentation offload (TSO).
Receive-side scaling (RSS) is supported for multi-core scaling.

The ENA driver and its corresponding devices implement health
monitoring mechanisms such as watchdog, enabling the device and driver
to recover in a manner transparent to the application, as well as
debug logs.

Some of the ENA devices support a working mode called Low-latency
Queue (LLQ), which saves several more microseconds.

Driver compilation
------------------

Prerequisites
^^^^^^^^^^^^^

In order to build and run standalone driver system, the OS sources
corresponding to currently installed OS version are required.

Depending on user configuration the sources retrieval process may vary,
however, on a fresh installation on EC2 instance, the necessary steps
may look like shown below (some may require super user privileges).

When using the latest kernel, the git tool should be used instead of the
subversion tool, as the svn support has been dropped by the FreeBSD project.
Still, some AWS AMIs could be relying on subversion kernel, so sometimes it's
more convenient to use it in order to get the right kernel. In that situation,
please refer to the "Subversion - legacy" section in this file. In all other
cases, please go to the "Git" section.

Git
"""

.. code-block:: sh

  pkg install git

  # ---- Get the sources for the current installation. ----
  # Getting sources may vary between system versions. The resources need
  # to be adjusted accordingly. Some of the examples can be found below.

  # For stable 11:
  git clone https://git.FreeBSD.org/src.git /usr/src --single-branch --branch stable/11

  # For release (FreeBSD 11.4)
  git clone https://git.FreeBSD.org/src.git /usr/src --single-branch --branch releng/11.4

  # For CURRENT (unstable)
  git clone https://git.FreeBSD.org/src.git /usr/src --single-branch --branch main

  # To get the version of the running kernel, the command below should be used:
  uname -a
  # Sample output:
  #   FreeBSD <host> 14.0-CURRENT FreeBSD 14.0-CURRENT #0 main-n246975-8d5c7813061: <current_date>
  # Where:
  #   - 'main' stands for the branch name (this should be used as argument to
  #     the '--branch' parameter in the git clone command
  #   - 'n246975' stands for the sequential commit number in the branch
  #   - '8d5c7813061' stands for the explicit commit ID from which the kernel
  #     was built. After cloning the development branch, one should checkout to
  #     this commit to be fully aligned with the running kernel.

  # In this example, we need to pull kernel tree on the 'main' branch and
  # checkout the commit with ID '8d5c7813061'. It can be achieved with the
  # commands below:
  git clone https://git.FreeBSD.org/src.git /usr/src --single-branch --branch main
  cd /usr/src
  git checkout 8d5c7813061

  # If the command output is lacking the commit and branch information, then
  # just the releng branch with the visible FreeBDS version should be used -
  # like releng/13.0, releng/12.2 etc.

Subversion - legacy
"""""""""""""""""""

.. code-block:: sh

  pkg install subversion
  mkdir /usr/src

  # ---- Get sources for the current installation. ----
  # This step may require accepting certificate.
  # Getting sources may vary between system versions. The resources need
  # to be adjusted accordingly. Some of the examples can be found below.

  # For stable:
  svn checkout https://svn.freebsd.org/base/stable/11/ /usr/src

  # For release (FreeBSD 11.1)
  svn checkout https://svn.freebsd.org/base/releng/11.1/ /usr/src

  # For -CURRENT (unstable)
  svn checkout https://svn.freebsd.org/base/head /usr/src

  # To get the version of the running kernel, the command below should be used:
  uname -a
  # Sample output:
  # FreeBSD <host> 12.0-CURRENT FreeBSD 12.0-CURRENT #0 r316750: <current_date>
  # r316750 is indicating revision of current kernel

  # In this example, we have to pull kernel tree with revision r316750 from the
  # head:
  svn checkout -r316750 https://svn.freebsd.org/base/head /usr/src
  # r316750 must be changed to the revision number from the 'uname -a' output

Compilation
^^^^^^^^^^^

Run ``make`` in the ``amzn-drivers/kernel/fbsd/ena/`` directory.
As a result of compilation ``if_ena.ko`` kernel module file is created in
the same directory.

Driver installation
-------------------

Loading the driver
^^^^^^^^^^^^^^^^^^

.. code-block:: sh

  kldload ./if_ena.ko

Automatic driver start upon OS boot
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: sh

  vi /boot/loader.conf
  # insert 'if_ena_load="YES"' in the above file

  cp if_ena.ko /boot/modules/
  sync; sleep 30;

Then restart the OS (reboot and reconnect).

Driver update - if the kernel was built with ENA
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: sh

  vi /boot/loader.conf
  # insert 'if_ena_load="YES"' in the above file

  cp if_ena.ko /boot/modules/

  # remove old module
  rm /boot/kernel/if_ena.ko
  sync; sleep 30;

Then restart the OS (reboot and reconnect).

Driver tunables
---------------

The driver's behavior can be changed using run-time or boot-time sysctl
arguments.

Boot-time arguments
^^^^^^^^^^^^^^^^^^^

The boot-time arguments can be changed in the ``/boot/loader.conf`` file (must
be edited as a ``root``). To make them go live, the system must be rebooted.

Use 9k mbufs for the Rx descriptors
"""""""""""""""""""""""""""""""""""

Node:
  ``hw.ena.enable_9k_mbufs``
Scope:
  Global for all drivers
Input values:
  ``(0|1)``
Default value:
  ``0``
Description:
  If the node value is set to 1, the 9k mbufs will be used for the
  Rx buffers. If set to 0, the page size mbufs will be used
  instead.

  Using 9k buffers for Rx can improve Rx throughput, but in low
  memory conditions it might increase allocation time, as the
  system has to look for 3 contiguous pages. This can further lead
  to OS instability, together with ENA driver reset and NVMe
  timeouts.

  If network performance is critical and memory capacity are
  sufficient, the 9k mbufs can be used.

Force the driver to use large LLQ headers
"""""""""""""""""""""""""""""""""""""""""

Node:
  ``hw.ena.force_large_llq_headers``
Scope:
  Global for all drivers
Input values:
  ``(0|1)``
Default value:
  ``0``
Description:
  If the node value is set to ``0``, the regular size LLQ header will
  be used, which is ``96B``. In some cases, the packet header can
  be bigger than this (for example - IPv6 with multiple
  extensions) and in that case, the large LLQ headers should be
  used by setting this node value to ``1``.

  This will take effect only if the device supports both LLQ and
  large LLQ headers. Otherwise, it will fallback to the no LLQ mode
  or regular header size.

  Increasing LLQ header size reduces the size of the Tx queue by
  half, so it may affect the number of dropped Tx packets.

Run-time arguments
^^^^^^^^^^^^^^^^^^

The run-time arguments can be changed anytime, using the ``sysctl(8)`` command.
They can only be modified by a user with the root privileges.

Controls extra logging verbosity of the driver
""""""""""""""""""""""""""""""""""""""""""""""

Node:
  ``hw.ena.log_level``
Scope:
  Global for all drivers
Input values:
  ``int``
Default value:
  ``2``
Description:
  The higher the logging level, the more logs will be printed out.
  Default value (``2``) reports errors, warnings and is verbose about driver
  operation.

  Value of ``0`` means that only errors essential to the driver operation will
  be printed out.

  The possible values are:

  * ``0`` - ``ENA_ERR`` - Enable driver error messages and ena_com error logs.
  * ``1`` - ``ENA_WARN`` - Enable logs for non-critical errors.
  * ``2`` - ``ENA_INFO`` - Make the driver more verbose about its action.
  * ``3`` - ``ENA_DBG`` - Enable debug logs.

  NOTE:
    In order to enable logging on the Tx/Rx data path, see the
    `Compilation flags`_ section of this document.

Example:
  To enable logs for both essential and non-critical errors, the below command
  should be used:

  .. code-block:: sh

    sysctl hw.ena.log_level=1

Number of the currently allocated and used IO queues
""""""""""""""""""""""""""""""""""""""""""""""""""""

Node:
  ``dev.ena.X.io_queues_nb``
Scope:
  Local for the interface X (X is the interface number)
Input values:
  ``[1, max_num_io_queues]``
Default value:
  ``max_num_io_queues``
Description:
  Controls the number of IO queues pairs (Tx/Rx). Currently it's
  impossible to have different number of Tx and Rx queues.
  As this call has to reallocate the queues, it will reset the
  interface and restart all the queues - it means that everything
  that was currently held in the queue will be lost, leading to
  potential packet drops.

  This call can fail if the system isn't able to provide
  the driver with enough resources. In that situation, the driver
  will try to revert the previous number of the IO queues. If this
  also fails, the device reset will be triggered.
Example:
  To use only ``2`` Tx and Rx queues for the device ``ena1``, the below command
  should be used:

  .. code-block:: sh

    sysctl dev.ena.1.io_queues_nb=2

Size of the Rx queue
""""""""""""""""""""

Node:
  ``dev.ena.X.rx_queue_size``
Scope:
  Local for the interface ``X`` (``X`` is the interface number)
Input values:
  ``[256, max_rx_ring_size]`` - must be a power of 2
Default value:
  ``1024``
Description:
  Controls the number of IO descriptors for each Rx queue.
  The user may want to increase the Rx queue size if he can observe
  high number of the Rx drops in the driver's statistics.
  For performance reasons, the Rx queue size must be a
  power of 2.

  This call can fail if the system isn't able to provide
  the driver with enough resources. In that situation, the driver
  will try to revert the previous number of the descriptors. If
  this also fails, the device reset will be triggered.
Example:
  To increase Rx ring size to 8K descriptors for the device ``ena0``, the
  below command should be used:

  .. code-block:: sh

    sysctl dev.ena.0.rx_queue_size=8192

Size of the Tx buffer ring (drbr)
"""""""""""""""""""""""""""""""""

Node:
  ``dev.ena.X.buf_ring_size``
Scope:
  Local for the interface ``X`` (``X`` is the interface number)
Input values:
  ``uint32_t`` - must be a power of 2
Default value:
  ``4096``
Description:
  Controls the number of mbufs that can be held in the Tx buffer
  ring. The drbr is being used as a multiple-producer,
  single-consumer lockless ring for buffering extra mbufs coming
  from the stack in case the Tx procedure is busy sending the
  packets or the Tx ring is full.

  Increasing size of the buffer ring may reduce the number of Tx
  packets being dropped in case of big Tx burst which can't be
  handled by the IO queue immediately.

  Each Tx queue has its own drbr.

  It is recommended to keep the drbr with at least the default
  value, but if the system lacks the resource, it can be reduced.
  This call can fail if the system isn't able to provide the driver
  with enough resources. In that situation, the driver will try to
  revert the previous number of the drbr and trigger the device
  reset.
Example:
  To make the drbr half of a size for the interface ``ena0``, the below
  command should be used:

  .. code-block:: sh

    sysctl dev.ena.0.buf_ring_size=2048

Interval in seconds for updating ENI metrics
""""""""""""""""""""""""""""""""""""""""""""

Scope:
  Local for the interface ``X`` (``X`` is the interface number)
Node:
  ``dev.ena.X.eni_metrics.sample_interval``
Input values:
  ``[0; 3600]``
Default value:
  ``0``
Description:
  Determines how often (if ever) the ENI metrics should be updated.
  The ENI metrics are being updated asynchronously in a timer
  service in order to avoid admin queue overload by sysctl node
  reading. The value in this node controls the interval between
  issuing admin command to the device which will update the ENI
  metrics value.

  If some application is periodically monitoring the eni_metrics,
  then the ENI metrics interval can be adjusted accordingly.
  ``0`` turns off the update totally. ``1`` is the minimum interval
  and is equal to 1 second. The maximum allowed update interval is
  1 hour.
Example:
  To update of the ENI metrics for the device ``ena1`` every 10 seconds,
  the below command should be used:

  .. code-block:: sh

    sysctl dev.ena.1.eni_metrics.sample_interval=10

Supported PCI vendor ID/device IDs
----------------------------------

=============   =============
``1d0f:0ec2``   ENA PF
``1d0f:1ec2``   ENA PF RSERV0
``1d0f:ec20``   ENA VF
``1d0f:ec21``   ENA VF RSERV0
=============   =============

ENA Source Code Directory Structure
-----------------------------------

* ``ena.[ch]``
    Main FreeBSD kernel driver.
* ``ena_sysctl.[ch]``
    ENA sysctl nodes for ENA configuration and statistics.
* ``ena_datapath.[ch]``
    Implementation of the main I/O path of the driver.
* ``ena_netmap.[ch]``
    Main code supporting the netmap mode in the ENA.
* ``ena_com/*``

  * ``ena_com.[ch]``
      Management communication layer. This layer is responsible for the handling
      all the management (admin) communication between the device and the
      driver.
  * ``ena_eth_com.[ch]``
      Tx/Rx data path.
  * ``ena_admin_defs.h``
      Definition of ENA management interface.
  * ``ena_eth_io_defs.h``
      Definition of ENA data path interface.
  * ``ena_common_defs.h``
      Common definitions for ena_com layer.
  * ``ena_regs_defs.h``
      Definition of ENA PCI memory-mapped (MMIO) registers.
  * ``ena_plat.h``
      Platform dependent code for FreeBSD.

Compilation flags
-----------------

The supplied Makefile provides multiple optional compilation flags, allowing
for customization of the driver operation. All of those flags are by default
disabled and require the user to manually uncomment relevant lines. Those
include:

* ``ENA_LOG_IO_ENABLE``

  The driver provides an ability to control log verbosity at runtime, through
  the sysctl interface. However, by default, the Tx/Rx data path logs remain
  compiled out, even when matching log verbosity is set. This is dictated by
  performance reasons. In order to enable logging on the data path,
  the following line should be uncommented:

  .. code-block:: Makefile

    # CFLAGS += -DENA_LOG_IO_ENABLE

* ``DEV_NETMAP``

  The driver supports the `netmap <https://github.com/luigirizzo/netmap/>`_
  framework. In order to use this feature, the following line should be
  uncommented:

  .. code-block:: Makefile

    # CFLAGS += -DDEV_NETMAP

  The kernel must also be built with ``DEV_NETMAP`` option in order to be able
  to use the driver with the netmap support, which is default for ``amd64``, but
  not for ``aarch64``.

* ``RSS``

  The driver is able to work with kernel side Receive Side Scaling support. In
  order to use this feature, the following line should be uncommented:

  .. code-block:: Makefile

    # CFLAGS += -DRSS

  This flag should only be used if ``option RSS`` is enabled in the kernel.

Management Interface
--------------------

ENA management interface is exposed by means of:

* PCIe Configuration Space
* Device Registers
* Admin Queue (AQ) and Admin Completion Queue (ACQ)
* Asynchronous Event Notification Queue (AENQ)

ENA device MMIO Registers are accessed only during driver
initialization and are not involved in further normal device
operation.

AQ is used for submitting management commands, and the
results/responses are reported asynchronously through ACQ.

ENA introduces a very small set of management commands with room for
vendor-specific extensions. Most of the management operations are
framed in a generic Get/Set feature command.

The following admin queue commands are supported:

* Create I/O submission queue
* Create I/O completion queue
* Destroy I/O submission queue
* Destroy I/O completion queue
* Get feature
* Set feature
* Configure AENQ
* Get statistics

Refer to the ``ena_admin_defs.h`` for the list of supported Get/Set Feature
properties.

The Asynchronous Event Notification Queue (AENQ) is a uni-directional
queue used by the ENA device to send to the driver events that cannot
be reported using ACQ. AENQ events are subdivided into groups. Each
group may have multiple syndromes, as shown below

The events are:

=================   ===============
Group               Syndrome
=================   ===============
Link state change   **X**
Fatal error         **X**
Notification        Suspend traffic
Notification        Resume traffic
Keep-Alive          **X**
=================   ===============

ACQ and AENQ share the same MSI-X vector.

Keep-Alive is a special mechanism that allows monitoring of the
device's health. The driver maintains a watchdog (WD) handler which,
if fired, logs the current state and statistics then resets and
restarts the ENA device and driver. A Keep-Alive event is delivered by
the device every second. The driver re-arms the WD upon reception of a
Keep-Alive event. A missed Keep-Alive event causes the WD handler to
fire.

Data Path Interface
-------------------

I/O operations are based on Tx and Rx Submission Queues (Tx SQ and Rx
SQ correspondingly). Each SQ has a completion queue (CQ) associated
with it.

The SQs and CQs are implemented as descriptor rings in contiguous
physical memory.

The ENA driver supports two Queue Operation modes for Tx SQs:

* Regular mode

  * In this mode the Tx SQs reside in the host's memory. The ENA
    device fetches the ENA Tx descriptors and packet data from host
    memory.
* Low Latency Queue (LLQ) mode or "push-mode".

  * In this mode the driver pushes the transmit descriptors and the
    first few bytes of the packet (negotiable parameter)
    directly to the ENA device memory space.
    The rest of the packet payload is fetched by the
    device. For this operation mode, the driver uses a dedicated PCI
    device memory BAR, which is mapped with write-combine capability.

The Rx SQs support only the regular mode.

Note: Not all ENA devices support LLQ, and this feature is negotiated
      with the device upon initialization. If the ENA device does not
      support LLQ mode, the driver falls back to the regular mode.

The driver supports multi-queue for both Tx and Rx. This has various
benefits:

- Reduced CPU/thread/process contention on a given Ethernet interface.
- Cache miss rate on completion is reduced, particularly for data
  cache lines that hold the mbuf structures.
- Increased process-level parallelism when handling received packets.
- Increased data cache hit rate, by steering kernel processing of
  packets to the CPU, where the application thread consuming the
  packet is running.
- In hardware interrupt re-direction.

Interrupt Modes
---------------

The driver assigns a single MSI-X vector per queue pair (for both Tx
and Rx directions). The driver assigns an additional dedicated MSI-X vector
for management (for ACQ and AENQ).

Management interrupt registration is performed when the FreeBSD kernel
attaches the adapter, and it is de-registered when the adapter is
removed. I/O queue interrupt registration is performed when the FreeBSD
interface of the adapter is opened, and it is de-registered when the
interface is closed.

The management interrupt is named:
   ``ena-mgmnt@pci:<PCI domain:bus:slot.function>``
and for each queue pair, an interrupt is named:
   ``<interface name>-TxRx-<queue index>``

The ENA device operates in auto-mask and auto-clear interrupt
modes. That is, once MSI-X is delivered to the host, its Cause bit is
automatically cleared and the interrupt is masked. The interrupt is
unmasked by the driver after cleaning all TX and Rx packets or the cleanup
routine is being called 8 times while handling single interrupt.

Statistics
----------

The user can obtain ENA device and driver statistics using sysctl.

MTU
---

The driver supports an arbitrarily large MTU with a maximum that is
negotiated with the device. The driver configures MTU using the
SetFeature command (ENA_ADMIN_MTU property). The user can change MTU
via ifconfig.

Stateless Offloads
------------------

The ENA driver supports:

* TSO over IPv4/IPv6
* IPv4 header checksum offload
* TCP/UDP over IPv4/IPv6 checksum offloads

RSS
---

* The ENA device supports RSS that allows flexible Rx traffic
  steering.
* Toeplitz and CRC32 hash functions are supported.
* Different combinations of L2/L3/L4 fields can be configured as
  inputs for hash functions.
* The driver configures RSS settings using the AQ SetFeature command
  (``ENA_ADMIN_RSS_HASH_FUNCTION``, ``ENA_ADMIN_RSS_HASH_INPUT`` and
  ``ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG`` properties).
* The driver sets default CRC32 function and it cannot be configured manually.

DATA PATH
---------

Tx
^^^

``ena_mq_start()`` is called by the stack. This function does the following:

* Assigns ``mbuf`` to proper tx queue according to hash type and ``flowid``.
* Puts packet in the ``drbr`` (multi-producer, {single, multi}-consumer
  lock-less ring buffer).
* If ``drbr`` was empty before putting packet, tries to acquire lock for ``tx``
  queue and, if succeeded, it runs ``ena_start_xmit()`` function for sending
  packet that was just added.
* If lock could not be acquired, it enqueues task ``ena_deferred_mq_start()``
  which will run ``ena_start_xmit()`` in different thread and it will
  clean all of the packets in the ``drbr``.
* ``ena_start_xmit()`` is doing following steps:

  * Checking if the Tx queue is still running - if not, then it puts ``mbuf``
    back to ``drbr`` and exits.
  * Call ``ena_xmit_mbuf()`` function for all ``mbufs`` in the ``drbr`` or until
    transmission error occurs.
  * ``ena_xmit_mbuf()`` is sending ``mbufs`` to the ENA device with given steps:

    * ``mbufs`` are mapped and defragmented if necessary for the DMA
      transactions.
    * Allocates a new request ID from the empty ``req_id`` ring. The request
      ID is the index of the packet in the Tx info. This is used for
      out-of-order TX completions.
    * The packet is added to the proper place in the TX ring.
    * The driver is checking if the doorbell needs to be issued.
    * ``ena_com_prepare_tx()`` is called, an ENA communication layer that
      converts the ``ena_bufs`` to ENA descriptors (and adds meta ENA
      descriptors as needed).

      This function also copies the ENA descriptors and the push buffer to the
      Device memory space (if in push mode).
    * Stop Tx ring if it couldn't handle any more packets.

  * Write doorbells to the ENA device if needed.
  * After emptying ``drbr``, if Tx queue was stopped due to running out of
    space, cleanup task is being enqueued.

When the ENA device finishes sending the packet, a completion
interrupt is raised:

* The interrupt handler cleans Rx and Tx descriptors in the loop until all
  descriptors are cleaned up or number of loop iteration exceeds maximum value
* The ``ena_tx_cleanup()`` function is called. This function calls
  ``ena_tx_cleanup()`` which handles the completion descriptors generated by
  the ENA, with a single completion descriptor per completed packet.

  * ``req_id`` is retrieved from the completion descriptor. The ``tx_info`` of
    the packet is retrieved via the ``req_id``. The data buffers are
    unmapped and ``req_id`` is returned to the empty ``req_id`` ring.
  * The function stops when the completion descriptors are completed or given
    budget is depleted.
  * Tx ring is being resumed if it was stopped before.

* All interrupts are being unmasked

Rx
^^^

When a packet is received from the ENA device:

* The interrupt handler cleans Rx and Tx descriptors in the loop until all
  descriptors are cleaned up or global number of loop iteration exceeds maximum
  value
* The ``ena_rx_cleanup()`` function is called. This function calls
  ``ena_com_rx_pkt()``, an ENA communication layer function, which returns the
  number of descriptors used for a new unhandled packet, and zero if
  no new packet is found.
* Then it calls the ``ena_rx_mbuf()`` function:
  The new mbuf is updated with the necessary information (protocol,
  checksum hw verify result, etc.).
* ``mbuf`` is then passed to the network stack, using the ``ifp->if_input``
  function or ``tcp_lro_rx()`` if LRO is enabled and packet is of type TCP/IP
  with TCP checksum computed by the hardware.
* The function stops when all packets are handled or given budget is depleted.

Unsupported features
--------------------

- RSS configuration by the user.

Known issues
------------

- ``FLOWTABLE`` option (per-CPU routing cache) leads to system crash on both
  FreeBSD 11 and FreeBSD 12-CURRENT system versions.

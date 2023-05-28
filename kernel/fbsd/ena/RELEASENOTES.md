# ENA FreeBSD Kernel Driver Release Notes

## Supported Kernel Versions and Distributions

ENA driver is supported on all FreeBSD releases starting from 12.0

The driver was verified on the following distributions:

**Releases:**
* FreeBSD 12.4
* FreeBSD 13.2

**Development:**
+-----------+-------------+
| Branch    | SHA         |
+-----------+-------------+
| stable/12 | dad7c072f48 |
| stable/13 | 185159f77c9 |
| HEAD      | 8020c05683f |
+-----------+-------------+

## r2.6.3 release notes
**Bug Fixes**
* Initialize statistics before the interface is available
* Fix driver unload crash

**Minor Changes**
* Mechanically convert ena(4) to DrvAPI
* Remove usage of IFF_KNOWSEPOCH

## r2.6.2 release notes
**Bug Fixes**
* Remove timer service re-arm on ena_restore_device failure.
* Re-Enable per-packet missing tx completion print

**Minor Changes**
* Remove support for FreeBSD 11 from README and RELEASE_NOTES.

## r2.6.1 release notes
**Bug Fixes**
* Fix KASAN and KMSAN kernel sanitizers build errors, by changing the
  `first_interrupt` variable type from `bool` to `uint8_t`.
* Fix LLQ descriptor reconfiguration after the device reset. This could cause
  driver misbehavior on the 6-generation AWS instances.

**Minor Changes**
* Align `req_id` and `qid` logging order.
* Fix typo in a comment.

## r2.6.0 release notes
**New Features**
* Rework the Makefile to automatically detect the kernel options. This also
  changes the supported compilation options.

**Bug Fixes**
* Fix invalid KASSERT in the netmap code.
* Fix ENI metrics probing, by moving it to a separate task. That fixes the
  issue with the sleep inside the callout.
* Prevent LLQ initialization when membar isn't exposed.

**Minor Changes**
* Remove write-only variables.
* Code style improvements.
* Align the driver with the latest upstream kernel driver changes.
* Use the generic function for setting the device description in probe.
* Documentation update regarding the TSO.
* Use atomic accessors for the first_interrupt variable.
* Store last Tx cleanup ticks for easier device reset debugging.
* Logging improvements.

## r2.5.0 release notes
**New Features**
* Add NUMA awareness to the driver which runs on the kernel with enabled option
  RSS.
* Add support for the IPv6 L4 Tx checksum offloads.

**Bug Fixes**
* Increase lifetime of the timer service to work from the attach to detach in
  order to be able to handle more device failures.
* Do not trigger the reset if the ENA device is not responsive.

**Minor Changes**
* Optimize logic of the Tx path 'req_id' value validation.
* Rework Makefile in order to read the kernel build options from the SYSDIR.
  This change makes the requirement for manual Makefile modifications redundant.

## r2.4.1 release notes
**New Features**
* Add full kernel RSS option support.
* Add RSS hash key reconfiguration feature.
* Add RSS indirection table reconfiguration feature.

**Bug Fixes**
* Fix building with gcc9.
* Trigger device reset whenever ena_com_prepare_tx fails.
* Prevent potential race condition between reset routine and ena_destroy_device.
* Add global lock for all driver instances. This fixes a race between sysctl
  procedures and driver reconfiguration routines.
* Disable meta descriptor caching when LLQ is being used. This fixes netmap
  support on ENAv3 hardware.

**Minor Changes**
* Avoid unnecessary mbuf collapse for fragmented mbufs when driver is working in
  the LLQ mode.
* Add extra log messages.
* Add locking assertions for critical sections.
* Restructure code, by moving RSS related logic to separate source files.
* Add extra statistics counters - csum_good for properly calculated checksum and
  unmask_interrupt_num for the number of interrupt unmasking events.

## r2.4.0 release notes
**New Features**
* Large LLQ headers support. In order to use LLQ with packet headers
  greater than 96B, the large LLQ headers should be enabled by modifying
  the sysctl node: hw.ena.force_large_llq_header.
* Rework logging system, to be able to compile out logs on the IO path
  and simplify the logging levels.

**Bug Fixes**
* Hide sysctl nodes for the unused queues.

**Minor Changes**
* Style adjustments.
* Convert README document into reStructuredText format and update the
  installation section regarding git.
* Remove surplus NULL checks when freeing resources.

## r2.3.1 release notes
**Bug Fixes**
* Fix resource allocation if the MSIx vector table is not sharing the
  BAR with the registers. FreeBSD requires all resources to be allocated
  by the driver, so having unavailable BAR with MSIx vector table on it,
  will prevent PCI code from allocating MSIx interrupts.

## r2.3.0 release notes
**New Features**
* Add ENI (Elastic Network Interface) metrics support. They can be
  enabled and accessed using the sysctl.
* Add support for the Rx offsets (HW feature)

**Bug Fixes**
* Fix driver when running on kernel with RSS option being enabled
  (please note, that the standalone driver should also be built with
  'CFLAGS += -DRSS' option in the Makefile).
* Fix alignment of the ena_com_io_cq descriptors.
* Fix validation of the Rx requested ID.

**Minor Changes**
* Add SPDX license identifiers.
* Add description of all driver tunables to the README file.
* Fix names/description of the device IDs supported by the driver.
* Update HAL to the newer version.
* Remove deprecated ENA_WARN macro.

## r2.2.0 release notes
**New Features**
* Add 'Tx drops' statistic which can be acquired by probing sysctl:
  dev.ena.#.hw_stats.tx_drops
  Where # stands for the interface number (0, 1, 2, ...).
* Mark the driver as epoch ready, to properly support latest FreeBSD 13
  stack changes.
* Allow changing the number of IO queues during runtime by using sysctl:
  dev.ena.#.io_queues_nb
  Where # stands for the interface number (0, 1, 2, ...).
* Generate default RSS key using RNG in order to improve security.

**Bug Fixes**
* Fix ENA IO memory read/write macros - add appropriate barriers where
  needed and remove them where they are no longer needed.
* Tx DMA mapping in case of the LLQ is being used was totally reworked
  to improve stability and simplicity.
* Rx refill optimization for low memory conditions - by default page
  size clusters are now being used. It can be reverted, as it may affect
  performance a bit, by setting sysctl "hw.ena.enable_9k_mbufs" to "1"
  in "/boot/loader.conf" file.
* Prevent driver from limiting IO queue number in case number of
  MSIx vectors changed between device resets.
* Update 'make clean' to remove 'opt_global.h' which started to appear on
  FreeBSD 13.
* Fix build for arm64 FreeBSD 12 as pmap_change_attr() isn't public
  there, yet

**Minor Changes**
* Update HAL to the newer version.
* Driver version can now be acquired probing sysctl:
  hw.ena.driver_version
* Improve locking on the configuration path - the driver was using two
  different locks and now it is unified.
* Rework Rx queue size configuration to do not use hardcoded values and
  to avoid device reset upon doing so.
* Tx buffer ring size reconfiguration was also reworked for the same
  purpose.
* Create queues with optional backoff in case the OS cannot provide
  enough resources while setting up the queues.
* Allow to disable meta caching for Tx path - it may be requested by the
  device.
* Set all sysctl nodes and procedures as MP safe.

## r2.1.1 release notes
**New Features**
* Initialize cleanup task as NET_TASK - it is required by the latest
  stack updates as NET_TASK is entering epoch mode upon execution.

**Bug Fixes**
* Fix race on multiple queues when LLQ is being enabled with Tx checksum
  offload. It could lead to data corruption.
* Fix compilation for gcc by defining validate_rx_req_id() as
  'static inline'.

## r2.1.0 release notes
**New Features**
* Add netmap support.
* Add LLQ support for the aarch64 by enabling WC.

**Bug Fixes**
* Fix infinite missing keep alive reset loop in case the reset routine
  takes too long because of the host being overloaded.

**Minor Changes**
* Split controller and datapath code into separate files.

## r2.0.0 release notes
**New Features**
* LLQ (Low Latency Queue) feature was added. New placement policy
  enables storing Tx descriptor rings and packet headers in the device
  memory. LLQs improve average and tail latencies. As part of LLQs
  enablement new admin capability was defined, device memory was mapped
  as write-combined and the transmit processing flow was updated.
* Rx checksum offloads can now be used on some of the HW.
* Optimization of the locks on both Tx and Rx paths. Rx locks were
  removed and Tx locks were optimized by adding queue management (stop
  and start).
* Additional Tx doorbells checks were added - supported only by some of
  the hardware.

**Bug Fixes**
* Suppress excessive error printouts in Tx hot path.
* Fix validation of the Rx OOO completion.
* Prevent double activation of admin interrupt (issue on A1).
* The reset is being triggered if too many Rx descriptors were received.
* The vaddr and paddr must be zeroed if allocation of the coherent
  memory fails using bus_dma_* API.
* The error is being set if allocation of IO interrupts fails.
* Driver now correctly supports partial MSI-x allocation - previously
  it was assuming, that the OS will always give enough MSI-x if the
  allocation won't return error.
* The ifp must be released detached before the last ena_down is called,
  to make sure ena_up will not be called once the ifp is released.
* The ifp should be allocated at the very end of the attach() function,
  as ifp cleanup is not working properly from the attach context.
* The validation of the Tx req_id was fixed.
* Error handling upon ENA reset is now fixed - it was causing memory
  leak and device failure.
* Tx offloads for fragmented pkt headers are fixed.
* Add check for msix_entries in case reset fails to prevent NULL pointer
  reference in ena_up().
* The driver is now checking for missing MSI-x.
* DMA synchronization calls were added to the Tx and Rx paths.

**Minor Changes**
* Add module PNP information.
* Update HAL to the newest version for ENAv2 driver.
* Logging of the ENA admin errors.
* Kernel RSS support was removed, as it was not compatible with ENA
  device.
* The host info structure is now receiving information about number of
  CPUs on the host as well as bdf (hints for the device).
* Rx refill threshold was limited, due to maximum number of Rx ring
  increased to 8k.
* ENA reset is now split into restore and destroy stages.
* New line characters in printouts were fixed.
* Make the reset triggering safer, by checking if the reset was already
  triggered.
* Checking for missing Tx completion was reworked for better readability
  and compatibility with above feature.
* AENQ notification handler was added.
* Device state is now being tracked using bitfields.

## r0.8.1 release notes
**Bug Fixes**
* Fix a mutex not owned ASSERT panic in ENA control path
* Skip setting MTU for ENA if it is not changing
* Do not pass header length to the NIC, as extracting it from the mbuf
  can be complicated in some cases
* Change BIT macro to work on unsigned value

## r0.8.0 release notes
**New Features**
* Update ena_com HAL to newest version
* Add reset reasons when triggering reset of the device
* Cover code with branch predictioning statements
* Rework counting of HW statistics
* Add RX OOO completion
* Check for RX ring state to prevent from stall - run rx cleanup in
  another thread if ring was stalled
* Allow logging system to be dynamically adjusted from the userspace
  from sysctl (hw.ena.log_level)
* Read max MTU value from the device
* Allow usage of more RX descriptors than 1

**Bug Fixes**
* Reset device after attach fails
* In the reset task. free management irq after calling ena_down. Admin
  queue can still be used before ena_down is called, or when it is being
  handled
* Do not reset device if ena_reset_task fails
* Move call of the ena_com_dev_reset() to the ena_down() routine - it
  should be called only if interface was up
* Fix error handling in attach
* Destroy admin queue resources after freeing interrupts (resources are
  used by the interrupt handler)
* Fix error handling in many places of the driver
* Lock drbr_free() call - it must be called with lock acquired
* Allow partial MSI-x allocation - update number of queues respectively
* Fix checking for the value of the DF flag
* Fix warnings that appeared when compiling driver with gcc
* Fix comparing L3 type with L4 enum on setting RX hash
* Fix calculating IO queues number
* Fix setting AENQ group to indicate only implemented handlers

**Minor Changes**
* Use different function for checking empty space on the sq ring
  (ena-com API change)
* Fix typo on ENA_TX_CLEANUP_THRESHOLD
* Change checking for EPERM with EOPNOTSUPP - change in the ena-com API
* Remove deprecated debugging methods
* Style fixes to align driver with FreeBSD style guide
* Remove deprecated and unsused counters
* Remove unsused macros and fields from ENA header file
* Update README file
* Remove NULL checks for malloc with M_WAITOK
* Do not check conditions explicitly for bool variables

## r0.7.1 release notes
**New Features**
* Add mbuf collapse feature in case the mbuf has more segments than the device
  can handle

**Bug Fixes**
* Remove unsused statement from the platform file to enable gcc
  compilation with standard warning flags
* Fix error check for Rx mbuf allocation
* Fix creation of the DMA tags and TSO settings
* Call drbr_advance() before leaving Tx routine to prevent panic
* Unmask all IO irqs after driver state is set as running, otherwise the
  interrupts can stay masked if they were in that state during interface
  initialization routine
* Acquire locks before calling drbr_flush() as it is required by the API
* Add missing lock upon initialization of the interface
* Add additional locks when releasing Tx resources and bufs
* Move hw stats updating routine to separate task to prevent from sleeping while
  unsleepable lock is held
* Add error handling if init of the reset task fails
* Add locks before each ena_up and ena_down call

**Minor Changes**
* Remove Rx mutex as it is no longer needed

## r0.7.0 release notes
**New Features**
* Acquire dma transaction width from the NIC
* Allow to control level of printouts from the Makefile
* Add watchdog service which is running periodically checks for:
  * missing keep alive
  * admin queue state
  * missing tx completions
  * if one of above fails, the reset is triggered
  * additional sysctl statistics were added
* Add reset task
* Add fast path for the TX - if drbr is empty, send packet in current thread
  instead of creating task
* Add additional TX cleanup calls on TX path if queue is almost full
* Use single ithread routine for cleanup of both TX and RX queues - it
  allows synchronization of the TX and RX queues
* Add host info feature - driver is sending information about OS to
  the device

**Bug Fixes**
* Initialize part of the io queues resources during attach to prevent from
  race condition during ena_down() call
* Fix freeing RX counters twice
* Unmask all irqs when creating queues - this fixes issue with missing MSI-x
  when reloading interface during heavy network traffic
* Add locks in ioctl call to prevent from resource concurrency when handling
  simultaneous up/down of the interface
* If sending packet to the NIC failed, leave transmitting loop instead of
  trying until success - this could cause infinite loop
* Fixed RSS handling mechanism by properly assigning flag to the mbuf
  depending on type of the packet
* Defer creation of the RSS to allow load ENA driver during boot
* Fix error handling paths:
  * ena_enable_msix: possible memory leak if pci_alloc_msix fails
  * ena_create_io_queues: memory already allocated for part of
    tx/rx queues is not released on error
  * ena_setup_rx_resources: lack of error check after initialization of
    enqueue task
  * ena_request_mgmnt_irq: ignored fail of irq resource activation
  * ena_sysctl_(dump|update)_stats: ignored errors returned by functions
  * ena_reset_task: kernel page fault happens after failed reset
* Add locks in qflush to prevent race with ena_start_xmit()
* Add additional checks for driver status to prevent driver from hanging
  in the infinite loop on TX path
* Add error checks after malloc with M_NOWAIT
* Add lock for the reset task to prevent conflict with ioctl
* Move BPF_MTAP to track packet only if it was successfully sent to the NIC

**Minor Changes**
* Update README instructions for installing driver
* Update description of the RX and TX path to reflect current state of
  the driver
* Update license to the 2-clause BSD
* Move macros describing version of the driver to the ena.h file
* Change printouts levels meaning and place from which some of them
  are called
* Use one RX buffer size for all MTU settings
* Remove link speed change code - feature is not supported by the device
* Align version system with Linux driver
* Upgrade ena_com to 1.1.4.1
* Reduce amount of DMA tags in use - one per type of the queue instead
  one per queue
* Remove transmit modes
* Remove vlan support
* Check for number of fragments before doing DMA mapping
* Synchronize device initialization routine with Linux driver
* Simplify TX cleanup routine loop
* Reword descriptions of the sysctls
* Remove references to NETMAP
* Added budget to TX cleanup

## r0.6.0 release notes

Initial commit of the FreeBSD driver.

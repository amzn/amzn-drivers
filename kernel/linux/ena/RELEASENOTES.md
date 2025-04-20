# ENA Linux Kernel Driver Release notes

## Supported Kernel Versions and Distributions

ENA driver is supported on kernels 3.10 and above.

The driver was verified on the following distributions:

**RedHat:**
* RedHat Enterprise Linux 7.0 or newer

**Ubuntu:**
* Ubuntu Trusty 14.04 amd64 server
* Ubuntu Xenial 16.04 amd64 server
* Ubuntu Yakkety 16.10 amd64 server
* Ubuntu Zesty 17.04 amd64 server
* Ubuntu Bionic Beaver 18.04 server or newer

**Amazon Linux:**
* Amazon Linux AMI 2016.09.1
* Amazon Linux AMI 2017.03
* Amazon Linux AMI 2017.09
* Amazon Linux AMI 2018.03
* Amazon Linux AMI 2
* Amazon Linux AMI 2023

**CentOS:**
* CentOS 7.0 or newer

**SUSE:**
* SUSE Linux Enterprise Server 12 SP2 or newer
* SUSE Linux Enterprise Server 15 or newer

## r2.14.0 release notes
**New Features**
* XDP multi-buffer support
* XDP bpf_xdp_adjust_tail support for multi-buffer packets
* XDP Metadata support

**Bug fixes**
* Add memory barrier on ena_xdp_xmit exit
* Resolve WARN_ON when freeing IRQs

**Minor Changes**
* Reduce stats prints on resets
* Eliminate duplication of rx request id recycling
* Separate TX/RX doorbell logic
* Add likely to llq checks
* Remove redundant checks and unused fields sets
* XDP multi-buffer TX error counters
* Fix print mismatch between qid and req id
* Upstream backports

## r2.13.3 release notes
**New Features**
* Fragment bypass support

**Bug fixes**
* Account for all bytes of recycled XDP / AF_XDP packets
* Remove unnecessary split of ENA_XDP_MAX_MTU to cases

**Minor Changes**
* Update outdated Best Practices comment about affinity hints

## r2.13.2 release notes
**New Features**
* RSS table size configuration from the device

**Bug fixes**
* Prevent a crash when accessing an uninitialized member in an error case
* Resolve adjfreq hook compilation issue
* Prevent a crash in RPS by declaring ndo_rx_flow_steer hook
* Fix max allowed queue ID for XSK pool
* Don't recycle redirected buffers on failure in AF_XDP

**Minor Changes**
* Upstream backports
* enable_bql module parameter documentation

## r2.13.1 release notes
**Bug fixes**
* Fix XDP functions hiding causing limited XDP functionality
* Update non-adaptive interrupt moderation for XDP queues
* Fix premature napi completion causing panic in XDP_TX and XDP_REDIRECT
* Add adjfine hook to PHC, required from Upstream kernel 6.2
* Don't allow ethtool to choose flow steering rule location

**Minor Changes**
* Document nitro version instead of EC2 instance generations
* Update RHEL DKMS documentation
* Reset procedure status print update
* Upstream kernel backports
* Add note about flow steering commands utilization

## r2.13.0 release notes
**New Features**
* Re-enable AF XDP zero-copy support
* Add support for flow steering
* Allow configuring LPC for less than 16 channels

**Bug fixes**
* Fix wrong memory handling of customer metrics
* Better detection of interrupt coalescing support
* Flush XDP TX queued packets in case of an error
* Hide PHC error bound sysfs file from showing when not supported
* Use flexible array in LPC

**Minor Changes**
* Fix typos and style issues
* Remove RPM installation from the main README
* Improve PHC documentation
* Dump invalid descriptors to the kernel ring to help debug issues
* Support kernel 6.10

**Notes**
* The flow steering feature will be available for newly attached ENIs

## r2.12.3 release notes
**Bug fixes**
* Remove explicit numa specification for Linux
* Fix interrupt interval change flag override
* Prevent adaptive moderation override
* Free copybreak page if NUMA is incorrect

## r2.12.2 release notes
**Bug fixes**
* Move eth_hw_addr_set to ECC to resolve compilation errors

## r2.12.1 release notes
**Bug fixes**
* Resolve a `-Wmissing-prototypes` compilation warning

## r2.12.0 release notes
**New Features**
* Add support for device reset request over AENQ
* Add NUMA aware interrupt allocation

**Bug fixes**
* Remove xdp drops from total rx drops in ena_get_stats64()
* Fix Makefile detection for header files changes
* Fix possible stuck tx packets when last tx packet is dropped in a burst
* Verify number of descriptors for copybreak
* Fix flush XDP packets on error

**Minor Changes**
* Add more info for tx timeout
* Document large LLQ enablement by default
* Featurize AF_XDP code
* RX ring submission queue optimization
* Remove redundant ena_select_queue handler
* Handle ENA_CMD_ABORTED case on admin queue interrupt mode
* Split reset reasons for missing keep alive notification
* Document best practices info for rx_overruns
* Add support for XDP in RHEL 8.5 and above

## r2.11.1 release notes
**Bug fixes**
* Free PHC info before netdev private info (adapter) is freed

## r2.11.0 release notes
**New Features**
* Support max wide LLQ depth from device

**Bug fixes**
* Count all currently missing TX completions in check
* Fix compilation issues in Oracle Linux 8 and 9
* ECC random number generation
* Fix compilation issues in Red Hat 9.3

**Minor Changes**
* Add reset reason for missing admin interrupt
* Fix rpm installation for RHEL 8.x and 9.x

## r2.10.0 release notes
**Notes**
* Devlink support has been removed from the driver. Please consult the
  documentation on the different methods available for enabling Large LLQ.
* Build system has been updated with the introduction of ECC (ENA Compatibility
  Check). The build command remained as is.

**New Features**
* ECC (ENA Compatibility Check) build system update
* Add TX/RX descriptor corruption check
* Activate PHC on supported devices and retrieve PHC timestamp
  and error bound values.
* Remove devlink support
* Allow modifying large LLQ entry with sysfs

**Bug fixes**
* Fix DMA syncing in XDP path when SWIOTLB is on
* Delete double counting in ena_tx_map_skb error flow
* Remove wrong napi id assignment in AF_XDP
* Initialize llq_policy field for all cases
* Fix skb truesize in case the RX buffer is not reused
* Correct the Makefile dependency list
* Fix crash when interrupt arrives after admin command timeout
* Fix large LLQ in ethtool when devlink is disabled

**Minor Changes**
* Add skb submission print time when freeing TX buffers
* Add TX IPv6 checksum offload support indication
* Improve reset reason statistics
* Add Large LLQ enablement documentation

## r2.9.1 release notes
**Bug fixes**
* Fix compilation issues on kernels >= 6.3

## r2.9.0 release notes
**New Features**
* Advertise TX push support on ethtool
* Sub-optimal configuration notification support
* Add ethtool support for modifying large LLQ
* Support LLQ entry size recommendation from device

**Bug fixes**
* Fix out of bound recorded RX queue
* Fix incorrect descriptor free behavior in XDP
* Fix incorrect range check when setting an XSK pool
* Fix different measurement units in comparison
* Fix wrong missing IO completions check order
* Missing TX completions mechanism rework

**Minor Changes**
* Update kernel version and distributions support
* Allow custom kernel header path for driver compilation
* Improve XDP_TX throughput by using batching
* Update dkms documentation
* Add msecs since last interrupt info for debug

## r2.8.9 release notes
**Bug fixes**
* Fix compilation issues in SLES 15 SP5
* Fix compilation issues in Fedora 37 and 38
* Fix compilation issues in kernel 5.4 and 5.10 backports

## r2.8.8 release notes
**New Features**
* Report RX overrun errors via net device stats

**Bug fixes**
* Fix compilation issue on ubuntu 16.04 instances

## r2.8.7 release notes
**Bug fixes**
* Add support for Kernels 6.2-6.3 (Devlink and PTP changes)
* Fix compilation issues in RHEL 8.8

## r2.8.6 release notes
**Bug fixes**
* Fix incorrect dma sync when SWIOTLB is on
* Fix compilation issues in RHEL 9.2

## r2.8.5 release notes
**Minor Changes**
* Compilation fixes for SLES 15SP3

## r2.8.4 release notes
**Bug fixes**
* Revert napi_consume_skb() and napi_build_skb() usage

## r2.8.3 release notes
**New Features**
* PHC module param enablement
* PHC devlink param enablement
* Add hint for interrupt moderation for the device
* Change initial static RX interrupt moderation interval
* Enable DIM by default on all CPU Architectures

**Bug Fixes**
* DMA sync for CPU before accessing buffer
* Fix ena_probe destroy order
* Validate completion descriptors consistency
* Fix TX packets missing completion counter

**Minor Changes**
* Compilation fixes for RHEL 9.0, 9.1 and SLES 15SP4
* PHC info dynamic allocation
* Publish devlink reload for RHEL 9.0 and 9.1
* Add ENA Express documentation

## r2.8.2 release notes
**Buf Fixes**
* Fix devlink large LLQ config not fully applied

## r2.8.1 release notes
**New Features**
* Add extended metrics mechanism support
* Add conntrack customer metric to ethtool

**Bug Fixes**
* Fix compilation issues on SLES 15 SP4
* Fix compilation errors in RHEL 8.7, 9.0
* Configure TX rings mem policy in reset flow

**Minor Changes**
* Add napi_build_skb support
* Add napi_consume_skb
* Align ena_alloc_map_page signature
* Move from strlcpy with unused retval to strscpy
* Add status check for strscpy calls
* Backport napi_alloc_skb usage

## r2.8.0 release notes
**Notes**
* The driver is now dependent on the ptp module for loading
  See README for more details.

**New Features**
* Add support for PTP HW clock
* Add support for SRD metrics
  Feature's enablement and documentation would be in future release

**Bug Fixes**
* Fix potential sign extension issue
* Reduce memory footprint of some structs
* Fix updating rx_copybreak issue
* Fix xdp drops handling due to multibuf packets
* Handle ena_calc_io_queue_size() possible errors
* Destroy correct amount of xdp queues upon failure

**Minor Changes**
* Remove wide LLQ comment on supported versions
* Backport uapi/bpf.h inclusion
* Add a counter for driver's reset failures
* Take xdp packets stats into account in ena_get_stats64()
* Make queue stats code cleaner by removing if block
* Remove redundant empty line
* Remove confusing comment
* Remove flag reading code duplication
* Replace ENA local ENA_NAPI_BUDGET to global NAPI_POLL_WEIGHT
* Change default print level for netif_ prints
* Relocate skb_tx_timestamp() to improve time stamping accuracy
* Backport bpf_warn_invalid_xdp_action() change
* Fix incorrect indentation using spaces
* Driver now compiles with Linux kernel 5.19

## r2.7.4 release notes
**Bug Fixes**
* Fix remaining space check in DRB

## r2.7.3 release notes
**Changes**
* Make AF XDP native support experimental
* Update supported distributions documentation

## r2.7.2 release notes
**Bug Fixes**
* Fix compilation for SLES 15 SP3
* Fix compilation for RHEL 8.6
* Fix wrong value check in copybreak sysfs code

**Minor Changes**
* Provide more information on TX timeouts
* Use the same interrupt moderation value for both TX and RX
  In XDP TX/REDIRECT channels

## r2.7.1 release notes
**Bug Fixes**
* Fix NUMA node update rate

## r2.7.0 release notes
**New Features**
* Add AF XDP with zero-copy support
* Add devlink tool support
* Add Dynamic RX Buffers (DRB) feature

**Bug Fixes**
* Fix Toepltiz initial value change after changing RSS key
* Fix compilation errors on RHEL 8 and on some old kernel version
* Fix several bugs in XDP infrastructure

**Minor Changes**
* Cosmetic code changes
* Add support for (upcoming) kernel 5.17
* Removing some dead code and redundant checks

## r2.6.1 release notes
**New Features**
* Add BQL support enabled by module parameter

**Minor Changes**
* Don't print stats on refresh capabilities reset

## r2.6.0 release notes
**New Features**
* Add "capabilities" field to negotiate device capabilities
* Add support for kernel 5.14
* Allow the device to signal the driver if features renogotiation is required

**Bug Fixes**
* Fix XDP packet fowarding on 6th generaion instances
* Prevent device reset when device isn't responsive

**Minor Changes**
* Move Local Page Cache (LPC) code to a separate file
* Reset device when receiving wrong request id on RX
* Cosmetic changes and code restructuring
* Fix typo in README
* Remove redundant code

## r2.5.0 release notes
**New Features**
* Unify skb allocation path and use build_skb()
* Support ethtool priv-flags and LPC state change

**Bug Fixes**
* Fix mapping function issues in XDP
* Fix XDP redirection related failures
* Fix page_ref_count() related checks to support older kernels correctly.
* Don't define UBUNTU_VERSION_CODE when not in Ubuntu.
* Drop unnecessary "#ifdef <function_name>" checks from kcompat.h.
* Bug fixes and code improvements in legacy poll code

**Minor Changes**
* Add debug prints to failed commands
* Minor performance improvements
* Replace pci_set_dma_mask/_coherent with dma_set_mask_and_coherent
* Change ena_increase_stat_atomic() function name
* Change variable casting in ena_com.c
* Add explicit include of ethtool.h to linux/ethtool.c
* Change LLQ fallback print from error to warning
* Remove unused ENA_DEFAULT_MIN_RX_BUFF_ALLOC_SIZE define
* Remove unused SUSPEND/RESUME defines
* Add mac OS defines.
* Use WRITE/READ_ONCE macros for first_interrupt variable
* Propagate upstream support for AF XDP busypoll
* Add Jiffies of last napi call to stats
* Add ena_ring_tx_doorbell() function
* Cosmetic changes to LPC
* Add is_lpc_page indication to help with page mapping
* Back-propagate xdp helpers from upstream kernel
* Fix RST format in README file

## r2.4.1 release notes
**Bug Fixes**
* Fix compilation error in kernels >= 5.10

**Minor Changes**
* Make all module parameters readable

## r2.4.0 release notes
**New Features**
* Implement local page cache (LPC) system (see README.rst for details)

**Bug Fixes**
* Rename README to README.rst for rpm generation

## r2.3.0 release notes
**New Features**
* Introduce XDP redirect implementation
* Add support for new power management API for kernels >= 5.8
* Provide interface and device information in logging messages

** Bug Fixes **
* Performance: set initial DMA width to avoid intel iommu issue.
* Fixed wrong expression in WARN_ON macro.
* Fix Sparse static checker errors in xdp code.
* Move napi declaration inside the loop to avoid Sparse static check warning.
* Don't init DIM work (dim.work) in case queue creation fails
* Make missed_tx stat incremental instead of reassigning it.
* Fix packet's addresses where rx_offset wasn't taken into account.
* Validate req_id in ena_com_rx_pkt().
* Make sure timer and reset routine won't be called after freeing device resources.
* Fix compilation error in RHEL 8.3

**Minor Changes**
* Initialize net_device earlier to allow its usage in netif_* and netdev_* prints.
  For more details see [https://www.spinics.net/lists/netdev/msg683250.html]
* Add function to increase stats to reduce code duplication.
* Ethtool: convert stat_offset to 8 bytes resolution to remove complex casts in the code.
* XDP: queue count check: Fix coding style nits.
* Cosmetic changes that fix alignment issues.
* Change ena license SPDX comment style in headers.
* Remove code duplication related to interrupt unmask stat.
* Fix spelling mistake in XDP stat query code.
* Move XDP_QUERY handling to the kernel for kernels >= 5.8.
* Conversion of README from markdown to rst format.

## r2.2.11 release notes
**New Features**
* Add stats printing to XDP queues
* Add queue counters for xdp actions (number of XDP_PASS/XDP_TX etc.)
* Add support for kernel v5.8
* Add interrupts unmask statistics to xdp queues
* Allow configuring RSS function and key

** Bug Fixes **
* Drop incorrect and irrelevant llq - device id mappings.
* Avoid remapping the llq mem bar every reset
* Prevent reset after device destruction
* Make DMA un-mapping BIDIRECTIONAL to RX pages to match
  their mapping. This is needed for Traffic Mirroring feature.

**Minor Changes**
* Change license string to SPDX format
* Fix some spelling mistakes
* Change variables and macros names to something more informative
* Add masking and null checking in code that requires it
* Align all log message from driver to have the driver's name at the
  beginning
* Remove code duplications
* Removed unnecessary LLQ acceleration mode negotiation
* Switch to using regular bool values instead atomics for unmask_interrupt
  (whose name was changed to masked_interrupts).

## r2.2.10 release notes
**New Features**
* Add new device statistics to ethtool command

## r2.2.9 release notes
**Bug Fixes**
* Fix memory leak in XDP_TX when TX queue is full.
* Fix napi budget accounting of XDP packets.
* Fix driver loading error in kernels >= 5.7 due to unreported
  interrupt coalescing capabilities.
* Fix is_doorbell_needed() to account for meta descriptors properly.

## r2.2.8 release notes
**New Features**
* Re-enable RX offset feature.

**Bug Fixes**
* Fix XDP PASS issue due to incorrect handling of offset in rx_info.
* Add PCI shutdown handler to allow safe kexec.
* Fix RHEL 8.2 compilation error.
* Fix kernel 5.5 compilation error.

**Minor Changes**
* Reduce driver load time.

## r2.2.7 release notes
**Minor Changes**
* Expose additional PCI device ID

## r2.2.6 release notes
**Bug Fixes**
* Disable rx offset feature

## r2.2.5 release notes
**New Features**
* Enable dynamic interrupt moderation on ARM64

## r2.2.4 release notes
**Bug Fixes**
* Use random key to configure RSS instead of static one.
* Fix multiple issues with the RSS configuration.
* Restore accidentally deleted meta-descriptor-caching-related code.

**Minor Changes**
* Set default tx interrupt moderation interval to 64, aligning it to upstream.
* Align comments surrounding create_queues_with_size_backoff() to upstream code.
* Minor cosmetic changes.
* Remove redundant print from ena_init().
* Change DRV_MODULE_VERSION to DRV_MODULE_GENERATION as in upstream code.
* Remove redefinition of ENA_HASH_KEY_SIZE in ena_netdev.h.
* Add missing row to README.
* Remove unused variable in XDP code.
* Use HAVE_NETDEV_XMIT_MORE in kcompat.h.

## r2.2.3 release notes
**Bug Fixes**
* Revert VXLAN TX checksum offloading support due to issues with other tunnel types.
* Avoid unnecessary constant rearming of interrupt vector when busy-polling.

## r2.2.2 release notes
**Bug Fixes**
* Fix compilation error in SLES 12 SP5

## r2.2.1 release notes
**Bug Fixes**
* fix incorrect parameter to ena_indirection_table_get() in kernels in
  range [3.8, 3.16)

## r2.2.0 release notes
**New Features**
* Implement XDP support for DROP and TX actions.
* Add VXLAN TX checksum offloading support.
* Map rx buffers bidirectionally to support traffic mirroring.
* Introduce disable meta descriptor caching feature required by llq
  accelerated mode.
* Revert extra_properties feature implementation via ethtool priv-flags.
* Support set_channels() callback in ethtool.

**Bug Fixes**
* Fix multiple issues with the RSS feature.
* Fix uses of round_jiffies() in timer_service.
* Add missing ethtool TX timestamping indication.
* Fix ENA_REGS_RESET_DRIVER_INVALID_STATE error during hibernation.
* Fix race condition causing an incorrect wake up of a TX queue when it is
  down.
* Fix dim exported symbols conflicts by removing all EXPORT_SYMBOL directives
  from dim files.
* Fix first interrupt accounting in XDP by adding first_interrupt field to
  napi struct.
* Fix napi handler misbehavior when the napi budget is zero.
* Add max value check in ena_set_channels() to disalow setting the number
  of queues to a higher than allowed maximum number.
* Fix race condition when setting the number of queues immediately after
  loading the driver, which caused a crash when changing the number of queues
  to a larger number than currently set.
* Fix incorrect setting of number of msix vectors according to num_io_queues,
  causing crash when changing the number of queues to a larger number after
  driver reset.
* Fix ena_tx_timeout() signature for kernels >= 5.5

**Minor Changes**
* Add RX drops and TX drops counters to ethtool -S command.
* Aggregate accelerated mode features under struct ena_admin_accel_mode_req
  currently including the new disable meta descriptor caching feature and
  the existing max tx burst size feature.
* Add debug prints to failed commands.
* Make ena rxfh support ETH_RSS_HASH_NO_CHANGE.
* Change MTU parameter to be unsigned in ena_com_set_dev_mtu().
* Remove unused ena_restore_ethtool_params() and relevant fields.
* Use combined channels instead of separate RX/TX channels in ethtool -l/L.
* Use SHUTDOWN as reset reason when closing interface.
* Change RSS default function on probe to Toeplitz.
* Enable setting the RSS hash function only without changing the key in
  ethtool.
* Remove superfulous print of number of queues during ena_up().
* Remove unnecessary parentheses to pass checkpatch.
* Add unmask interrupts statistics to ethtool.
* Arrange local variables in ena_com_fill_hash_function() in reverse christmas
  tree order.
* Separate RSS hash function retrieval and RSS key retreival into 2 different
  functions.

## r2.1.4 release notes
**New Features**
* Add support for the RX offset feature - where the device writes data
  with an offset from the beginning of an RX buffer.

## r2.1.3 release notes
**New Features**
* Replace old adaptive interrupt moderation algorithm with the DIM
  algorithm.
* Introduce driver_supported_features field in host_info, and use it
  to signal to the device that the driver supports adaptive interrupt
  moderation.

**Bug Fixes**
* Prevent NULL pointer dereference when an admin command is executed
  during the execution of ena_remove().
* Fix compilation error in early SUSE versions due to missing
  suse_version.h file.
* Fix continuous keep-alive resets.
* Avoid potential memory access violation by validating Rx descriptor
  req_id in the proper place.
* Fix issue where llq header size set by force_large_llq_header module
  parameter did not survive a device reset, causing a driver-device
  mismatch of header size.

## r2.1.2 release notes
**New Features**
* Add module parameter for setting the number of I/O queues.

**Bug Fixes**
* Add aarch64 to ExclusiveArch directive in ena.spec to allow building
  the driver using rpm-build on arm instances.
* Make ethtool -l show correct max number of queues (ethtool -L not
  implemented yet, but number of queues can be changed via the new
  num_io_queues module parameter).
* Fix compilation for both opensuse and suse 15.1

**Minor Changes**
* Remove redundant print of number of queues and placement policy in
  ena_probe().
* Make all types of variables that convey the number and sizeof
  queues to be u32, for consistency.
* Move the print to dmesg of creating io queues from ena_probe to
  ena_up.
* Rename ena_calc_queue_size() to ena_calc_io_queue_size() for clarity
  and consistency.
* Remove redundant number of io queues parameter in functions
  ena_enable_msix() and ena_enable_msix_and_set_admin_interrupts()
* Use the local variable ena_dev instead of ctx->ena_dev in
  ena_calc_io_queue_size().
* Fix multi row comment alignment.
* Change num_queues to num_io_queues for clarity and consistency

## r2.1.1 release notes
**Bug Fixes**
* kcompat: fix ndo_select_queue() signature for newer kernels
* kcompat: use netdev_xmit_more() instead of skb->xmit_more()
* Drop mmiowb() from newer kernels as done in upstream in the following comit:
  torvalds/linux@fb24ea5
* Fix compilation on RHEL 8
* ena_netdev.c: ena_update_queue_sizes: fix race condition with reset

**Minor Changes**
* Fix the kernel version indication comment in README
* Make sure all the counters that will be updated in normal case are grouped
  together so we don't end up wasting cache lines.
  cnt+bytes+csum_good+rx_copybreak_pkt should be grouped together as they will
  be used most of the time.
* Drop superflous error message from get_private_flags_strings(), the
  motive behind this is here: https://marc.info/?l=linux-netdev&m=155957215813380&w=2
* Use strlcpy instead of snprintf in get_private_flags_strings()
* Changed ena_update_queue_sizes() signature to use u32 instead of int type
  for the size arguments
* Drop inline keyword all *.c files, let the compiler decide
* Add auto_polling status print to command completion print in
  ena_com_wait_and_process_admin_cq_interrupts()
* Add unlikely() hints to conditionals where it is needed
* ena_com is generic while ethtool is Linux specific therefore
  we remove the reference to ethtool from ena_com
* Add sanity check test back to ena_com_config_dev_mode(). In
  the sanity check use == instead of <= since tx_max_header_size
  is uint32 and cannot be negative
* Fix reverse christmas tree in ena_com_write_sq_doorbell()

## r2.1.0 release notes
**New Features**
* Add force_large_llq_header module parameter to enable support
  for packets with headers larger than 96 bytes in LLQ mode
* Add support for Tx/Rx rings size modification through ethtool
* Allow rings allocation backoff when low on memory

**Bug Fixes**
* Fix race between link up and device initalization
* Free napi resources when ena_up() fails
* Make ethtool show correct current and max ring sizes
* Set freed objects to NULL to avoid failing future allocations
* kcompat: fix compilation warning in SLES 12 regarding ena_get_stats64()
* Fix error during MTU update in SLES 12 SP3
* Fix swapped parameters when calling ena_com_indirect_table_fill_entry
* Fix return value of ena_com_config_llq_info()

**Minor Changes**
* Add good checksum counter for to ethtool statistics
* Add rx_queue_size parameter documentation to README
* Arrange ena_probe() function variables in reverse christmas tree
* Replace free_tx/rx_ids union with single free_ids field in ena_ring
* Remove unnecessary calculations in ena_com_update_dev_comp_head()
  when CQ doorbell doesn't exist (the common path)
* Add new line to end of prints

## r2.0.3 release notes
**Bug Fixes**
* Fix compilation on Linux 5.0 due to the removal of dma_zalloc_coherent()
* gcc 8: fix compilation warning due to the introduction of Wstringop-truncation
* Fix compilation on RHEL 8

## r2.0.2 release notes
**New Features**
* Low Latency Queues (LLQs) - a new placement policy that enables storing
  Tx descriptor rings and packet headers in the device memory. LLQs improve
  average and tail latencies. As part of LLQs enablement new admin
  capability was defined, device memory was mapped as write-combined
  and the transmit processing flow was updated.
* Support for Rx checksum offload.
* Increase RX copybreak from 128 to 256 to improve memory and receive
  socket buffer utilization.
* Add support for maximum TX burst size.

**Bug Fixes**
* Fix NULL dereference due to untimely napi initialization.
* Fix rare bug when failed restart/resume is followed by driver removal,
  and causes memory corruption.
* Fix illegal access to previously unregistered netdev in ena_remove().
* Fix warning in rmmod caused by double iounmap.
* Fix compilation error in distributions that merged __GFP_COLD removal
  to kernels earlier than 4.15.

**Minor Changes**
* Remove support for ndo_netpoll_controller.
* Update host info structure to match the latest ENA spec.
* Remove redundant parameter in ena_com_admin_init().
* Fix indentations in ena_defs for better readability.
* Add section about predictable Network Names to the README.
* Fix small spelling mistake in RELEASE_NOTES __FGP_COLD => __GFP_COLD.

## r1.6.0 release notes
**Bug Fixes**
* fix driver compilation error in Ubuntu 14.04 kernel 3.13.0-29
* fix non-functional kernel panic on pcie hot-plug removal on EC2 bare
  metal instances
* fix compatibility issues with non-x86 platforms using PAGE_SIZE
  larger than 4K
* add memory barriers for non-x86 platforms with different memory
  ordering rules.

**New Features**
* Support different queue size for Rx and Tx. A new module param
  to configure that.
* Optimize performance for MMIO writes to  pcie - use relaxed writes
  when possible.

## r1.5.3 release notes
**Bug Fixes**
* fix driver compilation errors for RHEL 7.5.

## r1.5.2 release notes
**Bug Fixes**
* fix driver compilation errors for Oracle Linux UEK3 with kernel 3.8.13.

## r1.5.1 release notes
**Bug Fixes**
* fix driver compilation errors for centos 6.6 and 7.0.
* remove __GFP_COLD from Rx page allocation -
	__GFP_COLD was removed from kernel 4.15.
	For more information please refer to:
	https://patchwork.kernel.org/patch/10001293/
* move to the new kernel timers API -
	The Linux timer API was changed.
        use the new API.
	For more information about the improved kernel timers API:
	https://lwn.net/Articles/735887/
* reorder IO interrupt handler -
	To avoid a very rare race condition, set the interrupt indication to true
	before calling napi_schedule_irqoff().
	It is known that we are inside the interrupt routine, but the code could be moved
	outside of the interrupt routine in the future, so better have these set before
	scheduling napi.
* add missing memory barrier after setting the device reset reason and before
	setting the trigger reset flag.

**Minor Changes**
* remove redundant call to unlikely().
* fix indentation in several places.

## r1.5.0 release notes
**New Features:**
* improve driver robustness - add mechanism for detection and recovery
	from lost/misrouted interrupt.

**Bug Fixes:**
* don't enable interrupts until ENA_FLAG_DEV_UP flag is set - this
	might potentially cause a race resulting in ignored interrupts.
* add error handling to ena_down() sequence - errors, if not handled
	correctly, might affect subsequent ena_open() procedure.

## r1.4.0 release notes
**New Features:**
* refactor check_missing_com_in_queue() - improve readability.

**Bug Fixes:**
* fix driver statistics overflow - ena_get_stats() used wrong variable
	size which lead to an overflow.
* fix driver compilation under Ubuntu and RHEL.
* fix misplace call to netif_carrier_off() during probe
* fix a rare condition between device reset and link up setup -
	In rare cashes, ena driver would reset and restart the device.
	When the driver reset the device a link-up event might arrive
	before the driver completes the initialization, which lead to
	a access to unallocated memory.

## r1.3.0 release notes

**New Features:**
* add support for driver hibernation -add PM callbacks to support
	suspend/resume.
* improve driver boot time: Reduce sleeps when driver is working in
	polling mode.
* add statistics for missing tx packets.

**Bug Fixes:**
* fix kernel panic when register memory bar map fails.
* driver used to report wrong value for max Tx/Rx queues (always reported
	128 queues)
* fix compilation errors for RedHat 7.4
* fix ethtool statistics counters overflow for kernels below 3.2.36

## r1.2.0 release notes

**New Features:**
* add support for receive path filling the previously submitted free buffer
	in out of order.
* update enable PCI to use newly introduced IRQ functions in linux.
* refactor check_for_missing_tx_completions to reduce the code length of this
	function.
* remove redundant call for napi_hash_add() in kernels later than version 4.5.
* add reset reason for each device FLR.

**Bug Fixes:**
* fix compilation error in kernel 4.11
* fix uncompleted polling admin command false alarm.
* fix theoretical case - the drive might hang after consecutive open/close
	interface.
* fix super rare race condition between submit and complete admin commands.
	"Completion context is occupied" error message will appear when this
	bug is reproduce.
* unmap the device bars on driver removal
* fix memory leak in ena_enable_msix()
* fix napi_complete_done() wrong return value
* fix race in  polling mode in kernels 4.5 - 4.9.
	When CONFIG_RX_BUSY_POLL is set and busy poll is enabled,
	there is a potential case where the napi handler will not unmask the
	MSI-X interrupt leading to a case where the interrupt is left unmask
	and nobody schedule napi.
* disable admin queue msix while working in polling mode
* fix theoretical bug - on systems with extremely low memory, the napi handler
	might run out ram and failed to allocate new pages for Rx buffers.
	If the queue was empty when the scenario occur and napi completed
	his task nobody will reschedule napi and refill the Rx queue.

## r1.1.3 release notes

* Add support for RHEL 6.7/6.8 and 7.3

## r1.1.2 release notes

**New Features:**

* Add ndo busy poll callback, that will typically reduce network latency.
* Use napi_schedule_irqoff when possible
* move from ena\_trc\_ to pr\_.. functions and ENA_ASSERT to WARN
* Indentations and fix comments structure
* Add prefetch to the driver
* Add hardware hints
* Remove affinity hints in the driver, allowing the irq balancer to move
	it depending on the load.
	Developers can still override affinity using /proc/irq/*/smp_affinity

**Bug Fixes:**

* Initialized last_keep_alive_jiffies
	Can cause watchdog reset if the value isn't initialized
	After this watchdog driver reset it initiated, it will not happen again
	while the OS is running.
* Reorder the initialization of the workqueues and the timer service
	In the highly unlikely event of driver failing on probe the reset workqueue
	cause access to freed area.
* Remove redundant logic in napi callback for busy poll mode.
	Impact the performance on kernel >= 4.5 when CONFIG_NET_RX_BUSY_POLL is enable
		and socket is opened with SO_BUSY_POLL
* In RSS hash configuration add missing variable initialization.
* Fix type mismatch in structs initialization
* Fix kernel starvation when get_statistics is called from atomic context
* Fix potential memory corruption during reset and restart flow.
* Fix kernel panic when driver reset fail

**Minor changes:**

* Reduce the number of printouts
* Move printing of unsupported negotiated feature to _dbg instead of _notice
* Increase default admin timeout to 3 sec and Keep-Alive to 5 sec.
* Change the behaiver of Tx xmit in case of an error.
        drop the packet and return NETDEV_TX_OK instead of retunring NETDEV_TX_BUSY


## r1.0.2 release notes

**New Features:**

* Reduce the number of parameters and use context for ena_get_dev_stats
* Don't initialize variables if the driver don't use their value.
* Use get_link_ksettings instead get_settings (for kernels > 4.6)

**Bug Fixes:**

* Move printing of unsupported negotiated feature to _dbg instead of _notice
* Fix ethtool RSS flow configuration
* Add missing break in ena_get_rxfh

**Minor changes:**

* Remove ena_nway_reset since it only return -ENODEV
* rename small_copy_len tunable to rx_copybreak to match with main linux tree commit


## r1.0.0 release notes

Initial commit

ENA Driver supports Linux kernel version 3.2 and higher.

The driver was tested with x86_64 HVM AMIs.

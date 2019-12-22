# ENA Linux Kernel Driver Release notes

## Supported Kernel Versions and Distributions

ENA driver is supported on kernels 3.2 and above
with an addition of kernel 2.6.32.

The driver was verified on the following distributions:

**RedHat:**
* RedHat Enterprise Linux 6.7
* RedHat Enterprise Linux 6.8
* RedHat Enterprise Linux 6.9
* RedHat Enterprise Linux 7.2
* RedHat Enterprise Linux 7.3
* RedHat Enterprise Linux 7.4
* RedHat Enterprise Linux 7.5
* RedHat Enterprise Linux 7.6

**Ubuntu:**
* ubuntu Trusty 14.04 amd64 server
* Ubuntu Xenial 16.04 amd64 server
* Ubuntu Yakkety 16.10 amd64 server
* Ubuntu Zesty 17.04 amd64 server
* Ubuntu Bionic Beaver 18.04 amd64 server

**Amazon Linux:**
* Amazon Linux AMI 2
* Amazon Linux AMI 2018.03
* Amazon Linux AMI 2017.09
* Amazon Linux AMI 2017.03
* Amazon Linux AMI 2016.09.1

**CentOS:**
* CentOS 6
* CentOS 7

**SUSE:**
SUSE Linux Enterprise Server 12 SP2
SUSE Linux Enterprise Server 12 SP3

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
  https://github.com/torvalds/linux/commit/fb24ea52f78e0d595852e09e3a55697c8f442189
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

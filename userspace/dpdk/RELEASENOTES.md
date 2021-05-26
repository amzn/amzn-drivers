# ENA DPDK driver release notes
___
## Driver changes as per DPDK releases

### Normal releases

#### v21.05
_Release of the new version of the driver - r2.3.0_

Bug Fixes:
* Disable ops not supported by the secondary process, to give more verbose
  information to the caller about API violation.
* Make the ethdev references multi-process safe. As the PCI device pointer is
  process local and not valid in other processes, the `data` structure should be
  used instead.
* Indicate RSS hash presence in the mbuf, by setting the `PKT_RX_RSS_HASH` and
  setting the capability: `DEV_RX_OFFLOAD_RSS_HASH`.
* Report default ring size value and do not treat value
  `RTE_ETH_DEV_FALLBACK_[TR]X_RINGSIZE` as a hint to use the internal default.
  The fallback value is set by the ethdev layer and it is valid size value,
  which is used when the application didn't set ring size and driver doesn't
  report any size, neither.
* Fix spurious wakeups of the `pthread_cond_timedwait()` in the
  `ENA_WAIT_EVENT`, which may sometimes happen according to the POSIX.
* Fix parsing of the unsupported device arguments.
* Fix parsing of the large LLQ header device argument.
* Clear mbuf pointers on the Tx queue release function, to avoid situation when
  the application could receive duplicate mbufs for two different packets.

Minor Changes:
* Switch memcpy to optimized version on x86 and x86_64.
* Update ena_com to version from 18.09.2020.

#### v21.02
_Release of the new version of the driver - r2.2.1_

Bug Fixes:
* Prevent driver from calling doorbell twice on Tx path when Tx burst doorbell
  threshold is reached and Tx loop is being stopped right after that.
* Fix assessment of free Tx SQ space. Correct evaluation of the required space
  for each packet cannot be done effectively before the burst, because it
  depends on number of segments for each mbuf. Because of that, the driver
  should attempt to send each packet before returning error.
* Fix Tx doorbell statistics, by incrementing appropriate counter also for the
  case when doorbell is being activated after max Tx burst size is being
  achieved.
* Flush Rx buffers mempool cache after Rx queue setup to prevent mbufs from
  being stuck in the mempool cache of the configuration lcore.
* Validate Rx requested ID upon acquiring descriptor instead of doing that each
  time it's being used in the driver code. As a result this value is validated
  only once and a driver code could be simplified.

#### v20.11
_Release of the new version of the driver - r2.2.0_

New Features:
* Expose Elastic Network Interface (ENI) metrics via extended statistics
  (xstats) interface.

Bug Fixes:
* Align IO CQ allocations to 4K, as it's required by the latest generation HW
  for the best performance.
* Change name of the supported PCI device IDs.
* Fix setting Rx checksum flag in the mbuf. The driver now sets
  PKT_RX_*_CKSUM_GOOD flag.
* Fix xstats global stats offset. Previously it was read from the
  ena_stats_rx_strings instead of the ena_stats_global_strings array,
  but due to the same values held in both structures, the functionality
  of the code was still the same.
* Lock usages of the admin queue during the driver's runtime to avoid
  concurrency. The spinlock is used for that purpose.

Minor Changes:
* Mark ARMv8 as supported by the ENA PMD - the documentation was outdated.
* Remove ENA_ASSERT macro which wasn't used anywhere.
* Update ena_com to version from 26.04.2020.

#### v20.05
_Release of the new version of the driver - r2.1.0_

New Features:
* Support large LLQ headers for use cases like IPv6 with multiple extensions,
  where standard header size (96B) can be too small for the LLQ. They can be
  enabled using device parameter 'large_llq_hdr'.
* Expose 'Tx drops' statistic.
* Disable meta descriptor caching. New HW may require this feature to be
  enabled.
* Reuse zero-length descriptor. Some HW could append descriptor which length
  is 0. The driver is reusing it back on the Rx so the application doesn't have
  to handle this case.

Bug Fixes:
* Create IO rings with the valid value. Previously, the rings were created
  using maximum allowed IO ring size, making the memory management ineffective.
* Remove barriers before calling device doorbells. The doorbell function is
  already calling a barrier.
* Fix build with optimization flag O1.

Minor Changes:
* Limit minimum Rx buffer size to 1400B as some HW isn't supporting smaller
  buffers.
* Update ena_com to version from 25.09.2019.
* Refactor getting IO queues capabilities.
* Refactor Rx path.
* Refactor Tx path.
* Limit refill threshold by a fixed value. For big Rx ring, the refill function
  could be spending too much time or being called to rarely, so the maximum
  threshold value was set as 256.

#### v20.02
_Release of the new version of the driver - r2.0.3_

New Features:
* Add support for 'Rx offsets' - some HW append data to the mbuf in the given
  offset and now the driver is aware of that.

* Minor Changes:
* Update ena_com to version from 20.03.2019.

#### v19.11
_Release of the new version of the driver - r2.0.2._

Bug Fixes:
* Fix indication of bad L4 Rx checksum. If the packet was fragmented, the L4
  checksum should be set as PKT_RX_L4_CKSUM_UNKNOWN. Also, the error flag
  shouldn't be tested, unless the device indicated the L4 checksum was checked.

Minor Changes:
* Use SPDX license tags.
* Use dynamic log type for debug logging.

#### v19.08
_Release of the new version of the driver - r2.0.1._

Bug Fixes:
* Don't count Tx packets by summing up the number of buffers. It could cause
  Tx packets to be too big for mbuf chains.
* Fix Rx checksum error statistic counting. It was checked on Tx path instead of
  Rx.
* Fix assigning NUMA node to IO queues. DPDK has special API to do that from the
  application perspective, PMD shouldn't be trying to resolve appropriate NUMA
  node on it's own.
* Fix admin CQ polling mode for 32-bit applications.
* Mask mbuf flags and compare them with appropriate flags when Tx checksum
  offload is used as '&' operator may give false positives.

#### v19.05
Bug Fixes:
* Fix assignment of the Rx checksum capabilities - they were assigned to the Tx
  variable.
* Get device info statically, to make call to the get_eth_dev_info_get() safe
  for the multithread applications.

#### v19.02
_Release of the new version of the driver - r2.0.0._

New Features:
* Add Low Latency Queue v2 (LLQv2). This feature reduces the latency
  of the packets by pushing the header directly through the PCI to the
  device. This allows the NIC to start handle packet right after the doorbell
  without waiting for DMA.
* Add independent configuration of HW Tx and Rx ring depths.
* Add support for up to 8k Rx descriptors per ring.
* Add additional doorbell check on Tx, to handle Tx more efficiently for big
  bursts of packets.
* Add per queue statistics.
* Add extended statistics using xstats DPDK API.

Bug Fixes:
* The reset routine was aligned with the DPDK API, so now it can be
  handled as in other PMDs.
* Fix memory leaks due to port stops and starts in the middle of
  traffic.
* Assign to rte_errno only posivite values.
* Fix device init with multi-process.
* Update completion queue after Tx and Rx cleanups.
* Fix invalid reference to variable in union.
* Add supported RSS offloads types.
* Destroy queues in case of fail in ena_start() function.
* Skip Rx packets with invalid requested id.
* Set reset reason if Rx fails due to too many Rx descriptors.

Minor Changes:
* Update ena_com to version from 26.09.2018.
* Updated documentation and features list of the PMD.
* Rx drops are now being read in keep alive handler.
* Adjust new line characters in log messages.

#### v18.11
_Release of the new version of the driver - r1.1.1._

Bug fixes:
* Fix out of order (OOO) completion.
* Add information about VFIO support and instructions (Note - it should also
  apply to older DPDK releases, but it cannot be upstreamed for no longer
  supported versions).
* Recreate HW IO rings on start and stop to prevent descriptors memory leak
* Pass hash of the packet to the mbuf instead of queue id
* Fix Rx out of order completion - the refill was not assigning mbufs to Rx
  buffers properly

#### v18.08
_Release of the new version of the driver - r1.1.0._

New Features:
* Handle admin queue using interrupts
* Add AENQ (Asynchronous Event Notification Queue) handling which allowed to add
  new features:
  * LSC (link state change) interrupt handling
  * Handle ENA notifications
  * Reset routine, which is triggered by the DPDK application or the driver
    itself, if critical failure occurred
  * Watchdog and keepalive - to make sure it is being working, the application
    should be handling timers as described in the DPDK documentation
  * Monitor admin queue state
* Pass information about maximum number of Tx and Rx descriptors to the
  application.
* Add driver to the meson build.
* Enable write combining in the driver when the igb_uio is used.

Bug Fixes:
* Add stop and uninit routines to make device reconfigurable and prevent memory
  leaks.
* Linearize Tx mbuf if number of required descriptors is greater than supported.
* Take into consideration maximum allowed number of IO queues during their
  configuration.
* General error handling fixes.
* Update NUMA node configuration in the device.
* Add checks for pointers acquired from rte_memzone_reserve in coherent memory
  allocation, to prevent device from accessing NULL pointers.
* Do not allocate physically contiguous memory in the ENA_MEM_ALLOC_NODE, as it
  is not required. Use rte_zmalloc_socket instead.
* Fix GENMASK_ULL macro, to prevent it from causing undefined behavior when more
  or equal bits are shifted than the word length.
* Store memory handle after allocating coherent memory, so it can be used for
  release later.
* Fix SIGFPE error when number of queues is equal to 0 and the RSS is requested
  by the application.
* Remove information about the link speed, as the ENA is working only in the
  virtualized environment when link speed cannot be specified accurately.

Minor changes:
* Update ena_com to the version from 23.10.2016.
* Remove support for legacy LLQ which is no longer supported by the ENA.
* Restart only initialized queues instead of all.
* Add validation of the requested Tx buffer ID.
* Cover code with (un)likely statements to get the most from the branch
  predictor.

#### v18.05
Bug Fixes:
* Use IOVA-contiguous memzones for the hardware resources allocation.

#### v18.02
New Features:
* Use dynamic logging instead of static one.
* Use new Tx and Rx offloads API (introduced in DPDK v18.02).

Bug Fixes:
* Fix setting Tx offloads flags in the mbuf on the Rx path.
* Indicate jumbo frames support in the rx features structure which is supported
  by the device.

#### v17.08
Bug Fixes:
* Fix Tx cleanup to prevent the same buffers being released two times.

#### v17.05
Bug Fixes:
* Fix error handling in case of failure in the ena_com_set_hash_ctrl().
* Fix Rx descriptors allocation issue which was preventing user from allocating
  1024 Rx descriptors (maximum allowed size).
* Fix issue causing cleanup of the Rx descriptors being delayed by one Rx cycle.
* Fix memory leak when refilling of Rx descriptors fails.
* Calculate partial checksum if DF bit is disabled when Tx checksum offload is
  enabled (HW requirement).

#### v17.02
New Features:
* Add Tx preparation routine (can be used by DPDK apps to prepare Tx packets
  before sending them).
* Add TSO support for the driver (if the HW is supporting it). To make use of
  that, the preparation routine must be called before sending packet.

Bug Fixes:
* Use correct field for checking Rx offloads (the Tx field was used before).
* Fix error handling and configuration for host_info structure.

Minor changes:
* Use I/O device memory read/write API for I/O manipulations.

#### v16.11
Bug Fixes:
* Use unmasked head and tail indices to prevent using descriptors above ring
  size. From now on, the ring size must be power of 2.
* Check for available buffers prior to Tx.
* Improve safety of string handling.

#### v16.07
_Release of the new version of the driver - r1.0.0._

New Features:
* Configure debug memory area in the ENA which can be used for storing
  additional information.
* Configure host info structure for ENA device.
* Make coherent memory allocations NUMA-aware.

Bug Fixes:
* Use correct API for freeing the memory zones.
* Fix build for icc.

Minor changes:
* Update ena_com to the version from 05.06.16.
* Use readless communication only if the HW supports it.
* Doorbells optimization.

#### v16.04
_Release of the driver (unversioned)_


### Stable releases

There comes only backported patches.

#### v20.11.1
Bug fixes:
* Prevent driver from calling doorbell twice on Tx path when Tx burst doorbell
  threshold is reached and Tx loop is being stopped right after that. (v21.02)
* Fix assessment of free Tx SQ space. Correct evaluation of the required space
  for each packet cannot be done effectively before the burst, because it
  depends on number of segments for each mbuf. Because of that, the driver
  should attempt to send each packet before returning error. (v21.02)
* Fix Tx doorbell statistics, by incrementing appropriate counter also for the
  case when doorbell is being activated after max Tx burst size is being
  achieved. (v21.02)
* Flush Rx buffers mempool cache after Rx queue setup to prevent mbufs from
  being stuck in the mempool cache of the configuration lcore. (v21.02)
* Validate Rx requested ID upon acquiring descriptor instead of doing that each
  time it's being used in the driver code. As a result this value is validated
  only once and a driver code could be simplified. (v21.02)

#### v19.11.7
Bug fixes:
* Flush Rx buffers mempool cache after Rx queue setup to prevent mbufs from
  being stuck in the mempool cache of the configuration lcore.

#### v19.11.6
Bug fixes:
* HAL fixes (v20.11):
  - Fix release of wait event.
  - Specify delay operations.
  - Use min/max macros with type conversion.
* Align IO CQ allocations to 4K, as it's required by the latest generation HW
  for the best performance. (v20.11)
* Fix setting Rx checksum flag in the mbuf. The driver now sets
  PKT_RX_*_CKSUM_GOOD flag. (v20.11)
* Fix xstats global stats offset. Previously it was read from the
  ena_stats_rx_strings instead of the ena_stats_global_strings array,
  but due to the same values held in both structures, the functionality
  of the code was still the same. (v20.11)

Minor changes:
* Remove ENA_ASSERT macro which wasn't used anywhere. (v20.11)

#### v19.11.3
Bug fixes:
* HAL fixes (v20.05):
  - Make allocations thread safe.
  - Prevent allocation of zero sized memory.
  - Fix testing for supported RSS hash function.
* Create IO rings with the valid value. Previously, the rings were created
  using maximum allowed IO ring size, making the memory management
  ineffective. (v20.05)
* Fix build with optimization flag O1. (v20.05)

#### v18.11.11
Bug fixes:
* HAL fixes (v20.11):
  - Fix release of wait event.
  - Specify delay operations.
  - Use min/max macros with type conversion.

Minor changes:
* Remove ENA_ASSERT macro which wasn't used anywhere. (v20.11)

#### v18.11.9
Bug fixes:
* HAL fixes (v20.05):
  - Fix indentation of multiple defines.
  - Fix indentation in CQ polling.
  - Fix documentation of functions.
  - Fix testing for supported hash function.
  - Prevent allocation of zero sized memory.
  - Make allocation macros thread-safe.
* Create IO rings with the valid value. Previously, the rings were created
  using maximum allowed IO ring size, making the memory management
  ineffective. (v20.05)

#### v18.11.3
Bug fixes:
* Fix Rx checksum error statistic counting. It was checked on Tx path instead of
  Rx. (v19.08)
* Fix assigning NUMA node to IO queues. DPDK has special API to do that from the
  application perspective, PMD shouldn't be trying to resolve appropriate NUMA
  node on it's own. (v19.08)
* Fix admin CQ polling mode for 32-bit applications. (v19.08)
* Mask mbuf flags and compare them with appropriate flags when Tx checksum
  offload is used as '&' operator may give false positives. (v19.08)

#### v18.11.1
Bug fixes:
* Assign to rte_errno only posivite values. (v19.02)
* Fix device init with multi-process. (v19.02)

#### v18.08.1
Bug fixes:
* Fix setting hash in the received mbuf. Instead of queue ID, value received
  from the descriptor should be passed there. (v18.11)
* Fix out of order (OOO) completion. (v18.11)
* Recreate HW IO rings on start and stop to prevent descriptors memory leak
  (v18.11)
* Fix Rx out of order completion - the refill was not assigning mbufs to Rx
  buffers properly (v18.11)

#### v18.05.1
Bug fixes:
* Add checks for pointers acquired from rte_memzone_reserve in coherent memory
  allocation, to prevent device from accessing NULL pointers. (v18.08)
* Do not allocate physically contiguous memory in the ENA_MEM_ALLOC_NODE, as it
  is not required. Use rte_zmalloc_socket instead. (v18.08)
* Fix GENMASK_ULL macro, to prevent it from causing undefined behavior when more
  or equal bits are shifted than the word length. (v18.08)
* Fix SIGFPE error when number of queues is equal to 0 and the RSS is requested
  by the application. (v18.08)
* Remove information about the link speed, as the ENA is working only in the
  virtualized environment when link speed cannot be specified accurately.
  (v18.08)

#### v17.11.6
Bug fixes:
* Assign to rte_errno only posivite values. (v19.02)
* Fix device init with multi-process. (v19.02)
* Update completion queue after Tx and Rx cleanups. (v19.02)
* Add supported RSS offloads types. (v19.02)

#### v17.11.5
Bug fixes:
* Fix setting hash in the received mbuf. Instead of queue ID, value received
  from the descriptor should be passed there. (v18.11)

#### v17.11.4
Bug fixes:
* Add checks for pointers acquired from rte_memzone_reserve in coherent memory
  allocation, to prevent device from accessing NULL pointers. (v18.08)
* Do not allocate physically contiguous memory in the ENA_MEM_ALLOC_NODE, as it
  is not required. Use rte_zmalloc_socket instead. (v18.08)
* Fix GENMASK_ULL macro, to prevent it from causing undefined behavior when more
  or equal bits are shifted than the word length. (v18.08)
* Fix SIGFPE error when number of queues is equal to 0 and the RSS is requested
  by the application. (v18.08)
* Remove information about the link speed, as the ENA is working only in the
  virtualized environment when link speed cannot be specified accurately.
  (v18.08)

#### v17.11.1
Bug fixes:
* Fix setting Tx offloads flags in the mbuf on the Rx path. (v18.02)

#### v17.05.2
Bug fixes:
* Fix Tx cleanup to prevent the same buffers being released two times. (v17.08)

#### v17.02.1
Bug fixes:
* Fix error handling in case of failure in the ena_com_set_hash_ctrl(). (v17.05)
* Fix Rx descriptors allocation issue which was preventing user from allocating
  1024 Rx descriptors (maximum allowed size). (v17.05)
* Fix issue causing cleanup of the Rx descriptors being delayed by one Rx cycle.
  (v17.05)
* Fix memory leak when refilling of Rx descriptors fails. (v17.05)

#### v16.11.9
Bug fixes:
* Add information about VFIO support and instructions (Note - it should also
  apply to older DPDK releases, but it cannot be upstreamed for no longer
  supported version).
* Fix setting hash in the received mbuf. Instead of queue ID, value received
  from the descriptor should be passed there. (v18.11)

#### v16.11.8
Bug fixes:
* Add checks for pointers acquired from rte_memzone_reserve in coherent memory
  allocation, to prevent device from accessing NULL pointers. (v18.08)
* Do not allocate physically contiguous memory in the ENA_MEM_ALLOC_NODE, as it
  is not required. Use rte_zmalloc_socket instead. (v18.08)
* Fix GENMASK_ULL macro, to prevent it from causing undefined behavior when more
  or equal bits are shifted than the word length. (v18.08)
* Fix SIGFPE error when number of queues is equal to 0 and the RSS is requested
  by the application. (v18.08)
* Remove information about the link speed, as the ENA is working only in the
  virtualized environment when link speed cannot be specified accurately.
  (v18.08)

#### v16.11.5
Bug fixes:
* Fix setting Tx offloads flags in the mbuf on the Rx path. (v18.02)

#### v16.11.3
Bug fixes:
* Fix Tx cleanup to prevent the same buffers being released two times. (v17.08)

#### v16.11.2
Bug fixes:
* Fix error handling in case of failure in the ena_com_set_hash_ctrl(). (v17.05)
* Fix Rx descriptors allocation issue which was preventing user from allocating
  1024 Rx descriptors (maximum allowed size). (v17.05)
* Fix issue causing cleanup of the Rx descriptors being delayed by one Rx cycle.
  (v17.05)
* Fix memory leak when refilling of Rx descriptors fails. (v17.05)

#### v16.11.1
Bug fixes:
* Fix error handling and configuration for host_info structure. (v17.02)

#### v16.07.2
Bug fixes:
* Improve safety of string handling. (v16.11)

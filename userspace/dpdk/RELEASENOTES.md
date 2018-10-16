# ENA DPDK driver release notes
___
## Driver changes as per DPDK releases

### Normal releases

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

#### v16.11
Bug Fixes:
* Use unmasked head and tail indices to prevent using descriptors above ring
  size. From now on, the ring size must be power of 2.
* Check for available buffers prior to Tx.
* Improve safety of string handling.

#### v16.07
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
_Release of the driver (r1.0.0)_

Bug Fixes:
* Use correct field for checking Rx offloads (the Tx field was used before).
* Fix error handling and configuration for host_info structure.

Minor changes:
* Use I/O device memory read/write API for I/O manipulations.

Bug Fixes:
* Fix setting Tx offloads flags in the mbuf on the Rx path.
* Indicate jumbo frames support in the rx features structure which is supported
  by the device.

### dpdk-next-net - upcoming release
Bug fixes:
* Add information about VFIO support and instructions (Note - it should also
  apply to older DPDK releases, but it cannot be upstreamed for no longer
  supported versions).

### Stable releases

There comes only backported patches.

#### v18.05.1
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

#### v17.11.4
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

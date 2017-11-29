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

**Ubuntu:**
* ubuntu Trusty 14.04 amd64 server
* Ubuntu Xenial 16.04 amd64 server
* Ubuntu Yakkety 16.10 amd64 server
* Ubuntu Zesty 17.04 amd64 server

**Amazon Linux:**
* Amazon Linux AMI 2017.09
* Amazon Linux AMI 2017.03
* Amazon Linux AMI 2016.09.1

**CentOS:**
* CentOS 6
* CentOS 7

**SUSE:**
SUSE Linux Enterprise Server 12 SP2

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

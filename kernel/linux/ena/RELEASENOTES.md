==== ENA Driver Release notes ====

---- r1.1.2 ----

New Features:
* Add ndo busy poll callback, that will typically reduce network latency.
* Use napi_schedule_irqoff when possible
* move from ena_trc_* to pr_* functions and ENA_ASSERT to WARN
* Indentations and fix comments structure
* Add prefetch to the driver
* Add hardware hints
* Remove affinity hints in the driver, allowing the irq balancer to move
	it depending on the load.
	Developers can still override affinity using /proc/irq/*/smp_affinity

Bug Fixes:
* Initialized last_keep_alive_jiffies
	Can cause watchdog reset if the value isn't initialized
	After this watchdog driver reset it initiated, it will not happen again
	while the OS is running.
* Reorder the initialization of the workqueues and the timer service
	In the highly unlikely event of driver failing on probe the reset workqueue
	cause access to freed aread.
* Remove redundant logic in napi callback for busy poll mode.
	Impact the performance on kernel >= 4.5 when CONFIG_NET_RX_BUSY_POLL is enable
		and socket is openned with SO_BUSY_POLL
* In RSS hash configuration add missing variable initialization.
* Fix type mismatch in structs initialization
* Fix kernel starvation when get_statistics is called from atomic context
* Fix potential memory corruption during reset and restart flow.
* Fix kernel panic when driver reset fail

Minor changes:
* Reduce the number of printouts
* Move printing of unsupported negotiated feature to _dbg instead of _notice
* Increase default admin timeout to 3 sec and Keep-Alive to 5 sec.
* Change the behaiver of Tx xmit in case of an error.
        drop the packet and return NETDEV_TX_OK instead of retunring NETDEV_TX_BUSY

---- r1.0.2 ----

New Features:
* Reduce the number of parameters and use context for ena_get_dev_stats
* Don't initialize variables if the driver don't use their value.
* Use get_link_ksettings instead get_settings (for kernels > 4.6)

Bug Fixes:
* Move printing of unsupported negotiated feature to _dbg instead of _notice
* Fix ethtool RSS flow configuration
* Add missing break in ena_get_rxfh

Minor changes:
* Remove ena_nway_reset since it only return -ENODEV
* rename small_copy_len tunable to rx_copybreak to match with main linux tree commit

---- r1.0.0 ----

Initial commit

ENA Driver supports Linux kernel version 3.2 and higher.

The driver was tested with x86_64 HVM AMIs.

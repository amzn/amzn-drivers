.. SPDX-License-Identifier: GPL-2.0

==================================================================
ENA Linux Driver Best Practices and Performance Optimization Guide
==================================================================

This document attempts to answer the most frequent ENA users’ questions and
provides guidelines for achieving the best performance with ENA network device
at EC2 instances.

**This document covers ENA LINUX DRIVER ONLY**


General FAQs
============

.. _GitHub: https://github.com/amzn/amzn-drivers/tree/master/kernel/linux/ena

**Q:** I’m using a 1-year old driver version. Shall I upgrade?

**A:** Definitely!  There are number of reasons why you should upgrade ASAP

* Outdated drivers are prone to functional bugs, they lack performance
  optimizations and mechanisms for efficient management of system resources
  that were introduced in later versions
* Old and outdated driver versions might not be compatible with future
  generations of ENA devices
* Outdated drivers miss important features

Always aim to use the latest and greatest ENA driver in your AMI. The driver
can be downloaded from `GitHub`_, Linux upstream tree, and also included with
major Linux distributions.

**Q:** What is ENAv3? How do I know that my instance type uses ENAv3 device? Do
I have to upgrade my driver in order to take advantage of ENAv3?

**A**: ENAv3 is a new version of ENA device that introduces additional
improvements in latency and performance.
ENAv3 is available on the majority of the 6th generation instance types.
ENAv3 can be differentiated from ENAv2 by the presence of an additional BAR
(BAR1) on the PCI bus.

Important! ENAv3 is supported by the ENA driver starting with v2.2.9.
Instances using AMIs with older drivers would experience performance
degradation on ENAv3 devices. Instances with very old drivers (older than v1.2)
will fail to attach ENAv3 ENI.
If you use the default ENA driver preinstalled with the Linux distributions
AMIs, these are the distribution and kernel versions where ENAv3 support was
introduced:

* Amazon Linux - 4.14.186
* RHEL8.3 - 4.18.0-240.1.1.el8_3
* Ubuntu 20.04 - 5.4.0-1025-aws
* SLES 15 - 5.3.18-24.15

**Q:** How do I make sure I’m using the latest/correct driver?

**A:** Please follow one of the options below:

* Make sure your AMI is up-to-date with the latest updates of the Linux distro
  you use
* Install the latest version of the ENA GitHub driver

**Q:** What is ENA queue?

**A:** ENA queue is composed of Tx Submission and Completion ring and Rx
Submission and Completion ring

**Q:** How many ENA queues are available?

**A:** It depends on the instance type and instance size you are using. Usually the
number of queues exposed per ENI is calculated as :code:`MIN(MAX_NUM_QUEUES_PER_ENI, NUM_OF_VCPUS)`
MAX_NUM_QUEUES_PER_ENI is 8 for most of the instance types and
is 32 only for network accelerated instances.

**Q:** What is ENA LLQ aka ENAv2?

**A:** ENA LLQ is a Tx submission ring that is located in ENA device DRAM
behind PCI BAR2. The driver maps this area as Write Combined, and pushes there
the Tx submission ring descriptors along with packet headers. LLQs improves
latency and PPS. Starting with ENA driver v2.0 LLQ is the default mode of
operation of majority of the instance types.

**Q:** How many IRQs does the driver allocate for each ENI?

**A:** For each ENI the driver allocates 1 IRQ for management (Admin CQ, AENQ)
and one IRQ for each ENA queue.
Please note ENA queue an IRQ is shared between Tx and Rx Completion rings of the
same queue.

**Q:** What are default Rx and Tx ring sizes? Can they be changed?

**A:** The default size is 1K entries for both Tx and Rx. Ring size can be
adjusted using :code:`ethtool -G` command.
Rx ring sizes can vary between 256 to up to 16K entries (max value depends on
instance type/size).
Tx ring size varies between 256 to 1K entries.
Ring size value must be a power of 2. Please run

.. code-block:: bash

     $ ethtool -g DEVNAME to see the

to check the supported Tx/Rx rings max size.

**Q:** What offloads are enabled on an ENA device ?

**A:** This might depend on instance type. Please run :code:`ethtool -k` to determine.

**Q:** Is custom RSS hash key supported?

**A:** This might depend on the instance type. Please run :code:`ethtool -x` to
determine. If RSS key modification is supported please refer to `Configuring
RSS`_ section below, explaining how ENA calculates RSS hash.

Performance Optimizations FAQs
==============================
.. _`ec2 instance network bandwidth`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-instance-network-bandwidth.html
.. _`net_dim.rst`: https://elixir.bootlin.com/linux/latest/source/Documentation/networking/net_dim.rst
.. _`taskset`: https://man7.org/linux/man-pages/man1/taskset.1.html
.. _`numactl`: https://linux.die.net/man/8/numactl

**Q:** How much bandwidth shall I expect for single TCP/UDP flow?

**A:** EC2 infrastructure limits single flow BW to 5-10 Gbps (depending on
protocol type and placement group settings). Please refer to
`ec2 instance network bandwidth`_ for further details.

**Q:** ifconfig shows increasing number of dropped Rx packets. What should I do?

**A:** ENA device would drop packets if Rx ring becomes full. This usually
happens because instance's vCPUs don't keep up with the incoming traffic. There
are several options you might want to consider. Please refer to `CPU
Starvation`_ section below.

**Q:** I spotted ENA driver “Tx Completion Timeout” messages in the kernel logs.
What does it mean and what should I do?

**A:** It means that Tx packets weren’t completed within reasonable time. If a
certain threshold of such uncompleted packets is crossed, the driver resets the
device.
It’s part of ENA self-healing mechanism and would cure the situation
for extremely rare cases of an unresponsive device. However usually this
message is an indicator of vCPU starvation in your instance. Therefore it is
advised to investigate vCPU load.
Please refer to `CPU starvation`_ section below.

**Q:** What is ENA device reset?

**A:** ENA device reset is a self healing mechanism that is triggered when the
driver detects unexpected device behavior. Example of such behavior could be an
unresponsive device, missing keep-alive events from the device, Tx completions
timeouts, netdev timeout etc. The device reset is a rare event, lasts less than
a millisecond and might incur loss of traffic during this time, which is
expected to be recovered by the transport protocol in the instance kernel.

**Q:** I want fewer ENA queues, I’d prefer only a portion of my instance's vCPUs
to handle network processing.

**A:** No problem, please use :code:`ethtool -l` option to see the number of
available ENA queues. To adjust the number of queues to N instantaneously,
please use:

.. code-block:: bash

    $ sudo ethtool -L DEVNAME combined N

Please note that changing the number of queues, as well as the rings' sizes
might cause a short-lasting (less than a millisecond) traffic interruption.

**Q:** I want more ENA queues, I’d prefer to expose a dedicated ENA queue for
each instance vCPU?

**A:** Depending on the instance type ENA ENI supports up to 32 queues. If you
desire to expose more ENA queues to the instance, please attach to it an
additional ENI.

**Q:** Host vCPU utilization by ENA IRQ processing seems to be too high. I
suspect high interrupt rate.

**A:** Interrupt moderation is supported on the majority of Nitro powered
instances types.
For Tx, the static interrupt delay is set to 64 usec by default.
As for Rx moderation rate, its settings might vary depending on the instance
type. On some instance types Rx moderation is disabled by default, on others it
is enabled in adaptive mode.
Please use

.. code-block::bash

    $ ethtool -c DEVNAME

to determine interrupt moderation mode on your instance.
If you suspect high interrupt rate, we recommend to enable adaptive Rx
moderation.
The ENA device implements Dynamic Interrupt Moderation (DIM) mechanism (more
details can be found here: `net_dim.rst`_).
To enable adaptive Rx interrupt moderation:

.. code-block:: bash

    $ sudo ethtool -C DEVNAME adaptive-rx on

**Q:** I notice low BW and throughput. What could be possible reasons?

**A:** Please check vCPUs utilization (top/htop) on your instance and refer to
`CPU Starvation`_ section below. Also we recommend to validate that egress
traffic is evenly distributed across Tx rings: :code:`ethtool -S` can be used
to observe per ring stats.

**Q:** Where can I see the ENA device stats

**A:** :code:`ethtool -S DEVNAME`

**Q:** I noticed multiple ``queue_stops`` reported by device stats. What does it
mean?

**A:** There might be various reasons for that:

1. Packets were submitted to the Tx rings faster than they can be processed.
   This usually happens if the submission rate across your instance queues
   exceeds PPS rate limit.
   If this happens and Tx packets are dropped
   ``pps_allowance_exceeded``/``bw_out_allowance_exceeded`` stats would
   indicate it. Consider moving to a larger instance size or to a newer
   generation of the instance family.

2. Tx Completions weren’t processed in time by the driver and hence Tx
   submission ring entries weren’t freed. Please refer to `CPU Starvation`_
   section below for potential causes of vCPU starvation and ways to handle
   it.

3. Packets were submitted to a certain Tx ring at a higher rate than it can
   process it. In this case try to take advantage of multi-queue ENA
   capability and distribute traffic across multiple Tx queues

**Q:** What are the optimal settings for achieving the best latency

**A:** These are the measures that help improve latency:

1. Make sure CPU power state is set to avoid deep sleep states (see
   `CPU Power State`_ section for the details)

2. Consider enabling busy poll mode:

   .. code-block:: bash

    $ echo 70 > /proc/sys/net/core/busy_read
    $ echo 70 > /proc/sys/net/core/busy_poll

3. If possible consider setting the affinity of your program to the same vCPU
   as the ENA IRQ processing its traffic.

4. Make sure vCPUs handling ENA IRQs are not overloaded with other unrelated
   tasks (use `taskset`_ or `numactl`_ to move heavy tasks to other vCPUs)

**Q:** Part of my network traffic uses IPv6 header with extensions and also TCP
header with options. I suspect my Tx packets are not sent out.

**A:** ENA LLQs in default mode support network headers size up to 96 bytes. If
header size is larger, the packet would be dropped.
To resolve this issue we recommend to reload the ENA driver with module
parameter ``force_large_llq_header=1``. This will increase the supported header
size to a maximum of 224 bytes. Please note that this option reduces the max Tx
ring size form 1K to 512.
Please also note that this feature is only supported by the GitHub version of
ENA driver and by AL2 distro.

CPU starvation
==============

.. _perf: https://man7.org/linux/man-pages/man1/perf.1.html

Overloaded or unevenly used instance vCPUs might cause delays in network traffic
processing leading to packet drops on the Rx side and completion timeouts on the
Tx side. This will result in low performance and increased and highly variable
latency.

In order to achieve high and stable performance, the user should make sure the
instance vCPUs in charge of the network traffic are available and given
sufficient processing time for this task. Most of the network processing happens
in NAPI routine that runs in softirq context. vCPUs involved in NAPI processing
can be identified by running

.. code-block:: bash

  $ sudo cat /proc/interrupts | grep Tx-Rx


vCPU starvation can be caused by multiple reasons. The following course of
actions is recommended if network performance degrades:

1. Check kernel log for vCPU lockups or other signs of vCPU starvation.
   ENA packet drops might be a side effect of the global system issue that
   consumes vCPUs.
   Usually utilities like ``htop`` help observe this. Users can also use linux
   `perf`_ tool to determine where vCPUs spend most of their time.

2. Sometimes CPU utilization has a spiky nature resulting in short-lasting
   peaks.
   This might be enough to cause ingress packet drops for network
   intensive workloads. In this case we recommend to increase the size of the Rx
   ring in order to compensate for temporary vCPU unavailability. This would
   compensate for vCPU short-lasting unavailability.
   The default size of the ENA Rx ring is 1K entries, however it can be
   dynamically increased up to 16K entries using :code:`ethtool -G` option. For
   example to increase the Rx ring size on ``eth0`` interface to 4096, please
   run

   .. code-block:: bash

     $ sudo ethtool -G eth0 rx 4096

   Please note, ring resize operation might cause short-lasting packet drops,
   that are expected to be recovered by the transport protocol in the instance
   kernel.

3. If vCPUs responsible for network processing are constantly overloaded and
   approach 100% utilization this might indicate uneven load distribution across
   available vCPUs. The following options might be considered to improve load
   balancing:

   1. Reassign other tasks running on the overloaded vCPUs to other less
      loaded vCPUs that don’t participate in network processing. This can
      be achieved by `taskset`_ or `numactl`_ Linux utilities

   2. Alternatively steer away network interrupts from already overloaded vCPU.
      It can be done by:

      1. setting ``IRQBALANCE_BANNED_CPUS`` variable in
         ``/etc/sysconfig/irqbalance`` to the CPU mask indicating CPUs
         that you want to exclude

      2. restarting irqbalance service

         .. code-block:: bash

           $ sudo service irqbalance restart

      3. Exampe: ``IRQBALANCE_BANNED_CPUS=00000001,00000f00`` will exclude CPUs 8-11 and 33

      4. Note: we do not recommend disabling irqbalance service.
         ENA driver doesn’t provide affinity hints, and if device reset
         happens while irqbalance is disabled, this might cause undesirable
         IRQ distribution with multiple IRQs landing on the same CPU core.

   3. If there are more vCPUs in your instance than ENA queues, consider
      enabling receive packet steering (RPS) in order to offload part of
      the Rx traffic processing to other vCPUs.
      It is advised to keep RPS vCPU cores at the same NUMA node as the vCPU
      nodes processing ENA IRQs. Also avoid having RPS vCPU on sibling cores of
      IRQ vCPUs.

      1. To figure out NUMA cores distribution:

         .. code-block:: bash

           $ lscpu | grep NUMA

           The output:
           NUMA node(s): 2
           NUMA node0 CPU(s): 0-15,32-47 //cores 32-47 are siblings of cores 0-15
           NUMA node1 CPU(s): 16-31,48-63 //cores 48-63 are siblings of cores 16-31

      2. Example of RPS activation:

         .. code-block:: bash

           $ for i in `seq 0 7`; do echo $(printf "00000000,0000ff00") | sudo tee /sys/class/net/eth0/queues/rx-$i/rps_cpus; done

         This would assign cores 8-15 to RPS.

         Please note that if irqbalance service is enabled, IRQ processing
         might migrate to different vCPUs and make RPS less effective.
         We do not recommend disabling irqbalance service (See FAQ above),
         but rather indicate what CPU cores should be excluded by irqbalance
         service from IRQs processing (please see the point above)

   4. Instances with multiple ENIs and intensive traffic might encounter cases
      where vCPUs get heavily contended by ``skbuf`` allocation/deallocation
      mechanism.
      This would usually manifest in a way of
      ``native_queued_spin_lock_slowpath()`` function consuming most of
      processing time. To overcome this issue ENA driver introduces
      `Local Page Cache (LPC)`_ that allocates a page cache for each
      Rx ring and helps relieve allocation contention. LPC size by default is 2K
      pages, however it might be increased using module load parameter. Please
      see `Local Page Cache (LPC)`_ section below for more  for more details.

   5. If you suspect elevated CPU utilization due to high interrupt rate please enable Rx adaptive moderation as explained in the FAQs above:

      .. code-block:: bash

        $ sudo ethtool -C DEVNAME adaptive-rx on

   6. For some workloads it makes sense to reduce the number of vCPUs handling
      ENA IRQs, and thus free up more vCPU resources for other
      purposes. This can be achieved by reducing the number of ENA queues

      .. code-block:: bash

        $ sudo ethtool -L DEVNAME combined N

       where N is a desired number of queues.

Reserving sufficient kernel memory
==================================

Ensure that your reserved kernel memory is sufficient to sustain a high rate of
packet buffer allocations (the default value may be too small).

- Open (as root or with sudo) the ``/etc/sysctl.conf`` file with the editor of
  your choice.

- Add the ``vm.min_free_kbytes`` line to the file with the reserved kernel
  memory value (in kilobytes) for your instance type.
  As a rule of thumb, you should set this value to between 1-3% of available
  system memory, and adjust this value up or down to meet the needs of your
  application.

  alternatively one can use

  .. code-block:: bash

    $ sudo vm.min_free_kbytes = 1048576

- Apply this configuration with the following command:

  .. code-block:: bash

    $ sudo sysctl -p

- Verify that the setting was applied with the following command:

  .. code-block:: bash

    $ sudo sysctl -a | grep min_free_kbytes

Local Page Cache (LPC)
======================

ENA Linux driver allows to reduce lock contention and improve CPU usage by
allocating Rx buffers from a page cache rather than from Linux memory system
(PCP or buddy allocator). The cache is created and bound to Rx queue, and pages
allocated for the queue are stored in the cache (up to cache maximum size).

To set the cache size, one can specify ``lpc_size`` module parameter, which
would create a cache that can hold up to ``lpc_size * 1024`` pages for each Rx
queue. Setting it to 0, would disable this feature completely (fallback to
regular page allocations).

The feature can be toggled between on/off state using ethtool private flags, e.g.

.. code-block:: bash

  $ ethtool --set-priv-flags eth1 local_page_cache off

The cache usage for each queue can be monitored using ethtool -S counters. Where:

- ``rx_queue#_lpc_warm_up`` - number of pages that were allocated and stored in
  the cache

- ``rx_queue#_lpc_full`` - number of pages that were allocated without using the
  cache because it didn't have free pages

- ``rx_queue#_lpc_wrong_numa`` -  number of pages from the cache that belong to a
  different NUMA node than the CPU which runs the NAPI routine. In this case,
  the driver would try to allocate a new page from the same NUMA node instead

Note that ``lpc_size`` is set to 2 by default and cannot exceed 32. Also LPC is disabled when using XDP or when using less than 16 queues. Increasing the cache size might result in higher memory usage, and should be handled with care.

CPU Power State
===============

.. _`Processor state control for your EC2 instance`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/processor_state_control.html
.. _`High performance and low latency by limiting deeper C-states`: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/processor_state_control.html#c-states

If your instance type is listed as supported on `Processor state control for
your EC2 instance`_, one can prevent the system from using deeper C-states to
ensure low-latency system performance.
For more information, see `High performance and low latency by limiting deeper
C-states`_.

- Edit the GRUB configuration and add ``intel_idle.max_cstate=1`` to the kernel
  boot options For Amazon Linux 2, edit the /etc/default/grub file and add this
  option to the ``GRUB_CMDLINE_LINUX_DEFAULT`` line, as shown below::

    > GRUB_CMDLINE_LINUX_DEFAULT="console=tty0 console=ttyS0,115200n8 net.ifnames=0 biosdevname=0 nvme_core.io_timeout=4294967295 xen_nopvspin=1 clocksource=tsc intel_idle.max_cstate=1"

    > GRUB_TIMEOUT=0

  For Amazon Linux AMI, edit the /boot/grub/grub.conf file and add this option
  to the kernel line, as shown below::

    > kernel /boot/vmlinuz-4.14.62-65.117.amzn1.x86_64 root=LABEL=/ console=tty1 console=ttyS0 selinux=0 nvme_core.io_timeout=4294967295 xen_nopvspin=1 clocksource=tsc intel_idle.max_cstate=1

- (Amazon Linux 2 only) Rebuild your GRUB configuration file to pick up these
  changes:

  .. code-block:: bash

    $ sudo grub2-mkconfig -o /boot/grub2/grub.cfg

.. _`Configuring RSS`:

Configuring RSS
===============

The ENA device supports RSS, and depending on the instance type, allows
to configure the hash function, hash key and indirection table.
Please note that hash function/key configuration is supported by the 5th
generation network accelerated instances (c5n, m5n, r5n etc) and all 6th
generation instances.
Also Linux kernel 5.9 or newer is required for hash function/key configuration
support but the major Linux distributions ported the driver support to kernels
older than v5.9 (For example Amazon Linux 2 supports it since kernel 4.14.209).
You can also manually install GitHub driver v2.2.11g or newer to get this
support if your instance doesn't come with it.

The device supports Toeplitz and CRC32 hash functions and ``ethtool -X`` command
can be used to modify hash function/key and indirection table.

The Toeplitz hash implementation of the ENA device differs from the standard
implementation.

An example for the standard Toeplitz hash implementation can be found in:
https://gist.github.com/joongh/16867705b03b49e393cbf91da3cb42a7

In order to get the the same hash result as the one calculated by the ENA
device, the following 2 changes need to be made to the standard implementation:

* The hash key bytes need to be reversed in order.
* Initial result value needs to be changed from 0 to 0xffffffff.

In the provided Toeplitz implementation these changes translate to the
following code changes:

.. code-block:: diff

  @@ -9,11 +9,11 @@ KEY=[]
   def reset_key():
       global KEY
       KEY = [
  -        0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
  -        0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
  -        0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
  -        0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
  -        0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
  +        0xfa, 0x01, 0xac, 0xbe, 0x3b, 0xb7, 0x42, 0x6a,
  +        0x0c, 0xf2, 0x30, 0x80, 0xa3, 0x2d, 0xcb, 0x77,
  +        0xb4, 0x30, 0x7b, 0xae, 0xcb, 0x2b, 0xca, 0xd0,
  +        0xb0, 0x8f, 0xa3, 0x43, 0x3d, 0x25, 0x67, 0x41,
  +        0xc2, 0x0e, 0x5b, 0x25, 0xda, 0x56, 0x5a, 0x6d,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00
       ]

  @@ -38,7 +38,7 @@ def shift_key():

   def compute_hash(input_bytes, N):
       reset_key()
  -    result = 0;
  +    result = 0xffffffff;

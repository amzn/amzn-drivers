ENA with VFIO-PCI driver
========================

Enable WC in VFIO-PCI driver
----------------------------

VFIO-PCI driver does not support write combine.
To activate this feature, the patch that checks if PCI BAR is prefetchable must
be added.

In such case it must be mapped with memory marked as WC:

- `pci_iomap_wc` must be used instead `pci_iomap`
    (for Kernel version that does not have it `ioremap_wc` could be used),
- `pgprot_noncached` needs to be changed to `pgprot_writecombine`.

Also, as not all AWS instances support IOMMU, `VFIO No-IOMMU support` must be
enabled in the kernel configuration.

The VFIO driver can be downloaded and patched-up by using `get-vfio-with-wc.sh`
script. It should be executed with root privileges.

The script performs 3 steps:

1. Download appropriate kernel sources
1. Patch and build VFIO driver
1. Replace default VFIO module

As the patched module is installed in the default location, it can be loaded
just by executing:

```sh
modprobe vfio-pci
```

Script was tested on:

- Amazon Linux 2 AMI (HVM), SSD Volume Type - ami-0bb3fad3c0286ebd5
- Amazon Linux AMI 2018.03.0 (HVM), SSD Volume Type - ami-015232c01a82b847b
- Red Hat Enterprise Linux 8 (HVM), SSD Volume Type - ami-08f4717d06813bf00
- Ubuntu Server 20.04 LTS (HVM), SSD Volume Type - ami-06fd8a495a537da8b
- Ubuntu Server 18.04 LTS (HVM), SSD Volume Type - ami-0823c236601fef765

IOMMU support
-------------

Most of AWS instances do not support IOMMU.
In such case noiommu mode in driver must be enabled:

```sh
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
```

Some instances (for example i3.metal) support this feature.
It needs to be enabled, by adding parameters `iommu=1 intel_iommu=on`
to the kernel's boot line. It could be done in `/etc/default/grub`:

```sh
GRUB_CMDLINE_LINUX="console=ttyS0,115200n8 console=tty0 net.ifnames=0 crashkernel=auto iommu=1 intel_iommu=on"
```

Then update GRUB configuration:

```sh
grub2-mkconfig > /boot/grub2/grub.cfg
```

Now, the reboot is needed.

If IOMMU is active, `/sys/kernel/iommu_groups/` should not be empty.

For example:

```sh
ls /sys/kernel/iommu_groups/
0    103  14  2   25  30  36  41  47  52  58  63  69  74  8   85  90  96
1    104  15  20  26  31  37  42  48  53  59  64  7   75  80  86  91  97
10   105  16  21  27  32  38  43  49  54  6   65  70  76  81  87  92  98
100  11   17  22  28  33  39  44  5   55  60  66  71  77  82  88  93  99
101  12   18  23  29  34  4   45  50  56  61  67  72  78  83  89  94
102  13   19  24  3   35  40  46  51  57  62  68  73  79  84  9   95
```

Binding
-------

NIC could be bound with DPDK tool:

```sh
usertools/dpdk-devbind.py --bind=vfio-pci PCI-ID
```

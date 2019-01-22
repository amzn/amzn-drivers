#!/bin/bash
# Enable WC in VFIO-PCI driver
# Tested on:
#  * Amazon Linux 2 AMI (HVM), SSD Volume Type - ami-030aae8cba933aede
#  * Amazon Linux AMI 2018.03.0 (HVM), SSD Volume Type - ami-0233214e13e500f77
#  * Red Hat Enterprise Linux 7.5 (HVM), SSD Volume Type - ami-c86c3f23
#  * Ubuntu Server 18.04 LTS (HVM), SSD Volume Type - ami-0bdf93799014acdc4

TMP_DIR="tmp"
PATCH="wc_enable.patch"

# Kernel modules location:
P1="/usr/lib/modules/`uname -r`/kernel/drivers/vfio"
P2="/lib/modules/`uname -r`/kernel/drivers/vfio"

function download_yum {
	yum install -q -y gcc kernel-`uname -r` kernel-devel-`uname -r` git

	# Download kernel source
	yumdownloader --source kernel-devel-$(uname -r)
	rpm2cpio kernel*.src.rpm | cpio -idmv
	rm *patches.tar
	tar xf linux-*.tar*
}

function download_apt {
	apt-get -qq update
	apt-get -qq install dpkg-dev build-essential git -y
	apt-get -qq source linux-image-$(uname -r)
	# remove all non-directory files
	# allows to run cd linux-*
	rm *
}

function download {
	echo "Downloading prerequisites..."
	mkdir $TMP_DIR
	cd $TMP_DIR

	apt-get -v >/dev/null 2>/dev/null
	if [ $? -eq 0 ]; then
		download_apt
	else
		download_yum
	fi
	cd linux-*
}

function create_patch {
cat > $PATCH <<- EOM
diff --git a/vfio_pci.c b/vfio_pci.c
index be0af18..9307b5c 100644
--- a/vfio_pci.c
+++ b/vfio_pci.c
@@ -1124,7 +1124,12 @@ static int vfio_pci_mmap(void *device_data, struct vm_area_struct *vma)
 		if (ret)
 			return ret;
 
-		vdev->barmap[index] = pci_iomap(pdev, index, 0);
+		if (pci_resource_flags(pdev, index) & IORESOURCE_PREFETCH)
+			vdev->barmap[index] = ioremap_wc(
+			    pci_resource_start(pdev, index),
+			    pci_resource_len(pdev, index));
+		else
+			vdev->barmap[index] = pci_iomap(pdev, index, 0);
 		if (!vdev->barmap[index]) {
 			pci_release_selected_regions(pdev, 1 << index);
 		return -ENOMEM;
@@ -1133,7 +1138,10 @@ static int vfio_pci_mmap(void *device_data, struct vm_area_struct *vma)
 
 	vma->vm_private_data = vdev;
 	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
-	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
+	if (pci_resource_flags(pdev, index) & IORESOURCE_PREFETCH)
+		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
+	else
+		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
 	vma->vm_pgoff = (pci_resource_start(pdev, index) >> PAGE_SHIFT) + pgoff;
 
 	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
EOM
}

function compile_driver {
	# Adjust VFIO-PCI driver
	cd drivers/vfio/pci

	sed -i '/vfio_pci_driver_ptr/d' ./vfio_pci.c

	create_patch
	git apply $PATCH --ignore-whitespace
	if [ $? -ne 0 ]; then
		echo "Cannot apply patch. Tring to ignore contex with -C 0"
		git apply $PATCH --ignore-whitespace -C 0
		if [ $? -ne 0 ]; then
			echo "Cannot apply patch..."
			exit
		fi
	fi
	cd ..

	# Enable VFIO_NOIOMMU mode
	sed -i '1i#ifndef CONFIG_VFIO_NOIOMMU \n#define CONFIG_VFIO_NOIOMMU \n#endif' vfio.c

	# Configure makefile
	echo 'all:' >> Makefile
	echo '	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules' >> Makefile

	make
	if [ $? -ne 0 ]; then
		echo "Compilation error"
		exit
	fi
}

function check_module_location {
	path=""

	for p in $P1 $P2
	do
		ls $p/vfio.* >/dev/null 2>/dev/null
		if [ $? -eq 0 ]; then
			path=$p
			break
		fi
	done

	if [ "$path" = "" ]; then
		echo "Cannot find kernel modules location..."
		exit
	fi
}

function check_module_compression {
	ls $path/vfio.ko.xz >/dev/null 2>/dev/null
	if [ $? -eq 0 ]; then
		xz=".xz"
	else
		xz=""
	fi
}

function replace_module {
	check_module_location
	check_module_compression

	for name in "pci/vfio-pci.ko" "vfio.ko"
	do
		if [ "$xz" != "" ]; then
			xz $name -c > ${name}${xz}
		fi
		mv $path/${name}$xz $path/${name}${xz}_no_wc
		cp ${name}$xz $path/${name}$xz
	done
}

download
compile_driver
replace_module

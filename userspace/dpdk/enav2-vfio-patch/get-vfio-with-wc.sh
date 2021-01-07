#!/usr/bin/env bash
# Enable WC in VFIO-PCI driver
# Tested on:
#  * Amazon Linux 2 AMI (HVM), SSD Volume Type - ami-0bb3fad3c0286ebd5
#  * Amazon Linux AMI 2018.03.0 (HVM), SSD Volume Type - ami-015232c01a82b847b
#  * Red Hat Enterprise Linux 8 (HVM), SSD Volume Type - ami-08f4717d06813bf00
#  * Ubuntu Server 20.04 LTS (HVM), SSD Volume Type - ami-06fd8a495a537da8b
#  * Ubuntu Server 18.04 LTS (HVM), SSD Volume Type - ami-0823c236601fef765

set -e

TMP_DIR="tmp"

# Kernel modules location:
P1="/usr/lib/modules/`uname -r`/kernel/drivers/vfio"
P2="/lib/modules/`uname -r`/kernel/drivers/vfio"

# This may return an error if executed from inside the script
set +e
RED="$(tput setaf 1)"
GREEN="$(tput setaf 2)"

BOLD="$(tput bold)"
NORMAL="$(tput sgr0)"
set -e

function bold {
	echo -e "${BOLD}${@}${NORMAL}"
}

function err {
	bold "${RED}ERROR: ${@}"
}

function green {
	bold "${GREEN}${@}"
}

function get_kernel_version {
	local ver=$(uname -r | cut -f 1 -d '-')
	local ver_major=$(echo $ver | cut -f1 -d '.')
	local ver_minor=$(echo $ver | cut -f2 -d '.')
	local ver_subminor=$(echo $ver | cut -f3 -d '.')

	printf "%d%02d%04d" "${ver_major}" "${ver_minor}" "${ver_subminor}"
}

function download_kernel_src_yum {
	echo "Use yum to get the kernel sources"

	bold "\nInstall required applications and kernel headers"
	yum install -y gcc "kernel-$(uname -r)" "kernel-devel-$(uname -r)" \
	    git make elfutils-libelf-devel patch
	green Done

	# Download kernel source
	bold "\nDownload kernel source with vfio"
	yumdownloader --source "kernel-devel-$(uname -r)"
	rpm2cpio kernel*.src.rpm | cpio -idmv
	green Done

	rm -f *patches.tar
	tar xf linux-*.tar*
	rm -f linux-*.tar* linux-*.patch
}

function download_kernel_src_apt {
	echo "Use apt-get to get the kernel sources"
	apt-get -q -y update
	green Done

	bold "\nInstall required applications"
	apt-get -q -y install dpkg-dev build-essential git
	green Done

	bold "\nDownload Linux kernel source with vfio"
	if ! apt-get -q -y source linux-image-$(uname -r); then
		err "Cannot download Linux kernel source.\nPlease uncomment appropriate 'deb-src' line in the /etc/apt/sources.list file"
		exit 1
	fi
	green Done

	rm -f linux-*.dsc linux-*.gz
}

function download_kernel_src {
	bold "[1] Downloading prerequisites..."
	rm -rf "${TMP_DIR}"
	mkdir -p "${TMP_DIR}"
	cd "${TMP_DIR}"

	if apt-get -v >/dev/null 2>/dev/null; then
		download_kernel_src_apt
	else
		download_kernel_src_yum
	fi
	cd linux-*
}

function apply_wc_patch {
	if [ "${KERNEL_VERSION}" -ge 5080000 ]; then
		echo "Using patch for kernel version 5.8"
		local wc_patch="${BASE_PATH}/patches/linux-5.8-vfio-wc.patch"
	elif [ "${KERNEL_VERSION}" -ge 4100000 ]; then
		echo "Using patch for kernel version 4.10"
		local wc_patch="${BASE_PATH}/patches/linux-4.10-vfio-wc.patch"
	else
		err "Unsupported kernel version - required at least v4.10"
		exit 1
	fi

	if ! patch --ignore-whitespace -p1 < "${wc_patch}"; then
		err "Cannot apply patch: ${wc_patch}!"
		exit 1
	fi
}

function compile_vfio_driver {
	bold "\n[2] Patch and build the vfio driver"
	# Adjust VFIO-PCI driver

	bold "Apply patch for the write combining to the vfio-pci"
	apply_wc_patch
	green Done

	cd drivers/vfio
	# Configure Makefile - build VFIO with support for NOIOMMU mode
	bold "\nConfigure Makefile for standalone vfio build and noiommu mode support"
	echo "ccflags-y := -DCONFIG_VFIO_NOIOMMU=1" >> Makefile
	echo 'all:' >> Makefile
	echo '	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules' >> Makefile
	green Done

	bold "\nBuild the driver"
	if ! make; then
		err "Compilation error."
		exit 1
	fi
	green Done
}

function get_module_location {
	for p in ${P1} ${P2}; do
		if find "${p}" -name "vfio.*" >/dev/null 2>/dev/null; then
			MOD_PATH="${p}"
			break
		fi
	done

	if [ -z "${MOD_PATH}" ]; then
		err "Cannot find kernel modules location..."
		exit
	fi
}

function get_module_compression {
	if ls "${MOD_PATH}/vfio.ko.xz" >/dev/null 2>/dev/null; then
		XZ=".xz"
	else
		XZ=""
	fi
}

function replace_module {
	bold "\n[3] Install module"
	get_module_location
	get_module_compression

	for name in "pci/vfio-pci.ko" "vfio.ko"; do
		if [ -n "${XZ}" ]; then
			xz "${name}" -c > "${name}${XZ}"
		fi
		mv "${MOD_PATH}/${name}${XZ}" "${MOD_PATH}/${name}${XZ}_no_wc"
		cp "${name}${XZ}" "${MOD_PATH}/${name}${XZ}"
	done
	green "Module installed at: ${MOD_PATH}/${name}${XZ}"
}

###############################################
# Main script code
###############################################

if [ "$(id -u)" -ne 0 ]; then
	err 'Please execute script as a root'
	exit 1
fi

cd $(dirname ${0})
BASE_PATH=$(pwd)

KERNEL_VERSION=$(get_kernel_version)

if [ "${KERNEL_VERSION}" -lt 4100000 ]; then
	err "Kernel version: $(uname -r) is not supported by the script. Please upgrade kernel to at least v4.10."
	exit 1
fi

download_kernel_src
compile_vfio_driver
replace_module

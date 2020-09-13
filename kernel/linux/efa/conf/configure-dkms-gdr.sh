#!/bin/bash
# Auxiliary script for GDR driver packaging

kernelver=$1
dkms_source_tree=$2
kernel_source_dir=$3

nvidia_path=$(find $dkms_source_tree -maxdepth 1 -type d -name '*nvidia-*' | sort | tail -n 1)
if [ -z ${nvidia_path} ]; then
	echo "== Error: unable to find NVIDIA driver source in DKMS tree."
	exit 1
fi

echo "== Using NVIDIA driver path $nvidia_path"

echo "== Building Module.symvers"
pushd $nvidia_path
SYSSRC="${kernel_source_dir}" make -j$(nproc) || exit 1
popd

./configure --with-kerneldir=/lib/modules/${kernelver} --with-gdr=${nvidia_path}

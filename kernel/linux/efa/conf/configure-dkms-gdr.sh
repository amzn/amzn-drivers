#!/bin/bash
# Auxiliary script for GDR driver packaging

kernelver=$1
dkms_source_tree=$2

nvidia_path=$(find $dkms_source_tree -maxdepth 1 -type d -name '*nvidia-*' | sort | tail -n 1)
if [ -z ${nvidia_path} ]; then
	echo "== Error: unable to find NVIDIA driver source in DKMS tree."
	exit 1
fi

echo "== Using NVIDIA driver path $nvidia_path"

echo "== Building Module.symvers"
pushd $nvidia_path
make KERNEL_UNAME="${kernelver}" clean || exit 1
make KERNEL_UNAME="${kernelver}" -j$(nproc) || exit 1
popd

mkdir -p build
pushd build
cmake -DKERNEL_VER=${kernelver} -DNVIDIA_DIR=${nvidia_path} ..
popd

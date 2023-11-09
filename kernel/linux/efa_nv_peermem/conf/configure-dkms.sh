#!/bin/bash
# Auxiliary script for driver packaging

kernelver=$1
dkms_source_tree=$2
extra_flags=

nvidia_path=$(find $dkms_source_tree -maxdepth 1 -type d -name '*nvidia-*' | sort | tail -n 1)
if [ $nvidia_path ]; then
	echo "== Building NVIDIA Module.symvers"
	pushd $nvidia_path
	make KERNEL_UNAME="$kernelver" clean || failed_nvidia=true
	make KERNEL_UNAME="$kernelver" -j$(nproc) || failed_nvidia=true
	popd

	if [ $failed_nvidia ]; then
		echo "== Failed building NVIDIA Module.symvers, proceeding without"
	else
		echo "== Using NVIDIA driver path $nvidia_path"
		extra_flags="-DNVIDIA_DIR=$nvidia_path"
	fi
fi

mkdir -p build
pushd build
cmake -DKERNEL_VER=$kernelver $extra_flags ..
popd

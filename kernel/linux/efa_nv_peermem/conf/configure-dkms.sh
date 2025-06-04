#!/bin/bash
# Auxiliary script for driver packaging

kernelver=$1
dkms_source_tree=$2
extra_flags=

nvidia_path=$(find $dkms_source_tree -maxdepth 1 -type d -name '*nvidia-*' | sort | tail -n 1)
if [ $nvidia_path ]; then
	echo "== Building NVIDIA Module.symvers"
	pushd $nvidia_path
	make KERNEL_UNAME="$kernelver" clean &> nvidia.log || failed_nvidia=true
	make KERNEL_UNAME="$kernelver" -j$(nproc) &>> nvidia.log || failed_nvidia=true
	popd

	if [ $failed_nvidia ]; then
		echo "== Couldn't build NVIDIA Module.symvers, proceeding without. For additional info see $nvidia_path/nvidia.log"
	else
		echo "== Using NVIDIA driver path $nvidia_path"
		extra_flags="-DNVIDIA_DIR=$nvidia_path"
	fi
fi

if hash cmake3 2>/dev/null; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi

mkdir -p build
pushd build
$CMAKE -DKERNEL_VER=$kernelver $extra_flags ..
popd

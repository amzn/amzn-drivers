#!/bin/bash
# Auxiliary script for driver packaging

kernelver=$1

if hash cmake3 2>/dev/null; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi

mkdir -p build
pushd build
$CMAKE -DKERNEL_VER=${kernelver} ..
popd

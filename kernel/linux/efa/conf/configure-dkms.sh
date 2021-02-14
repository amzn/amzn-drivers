#!/bin/bash
# Auxiliary script for driver packaging

kernelver=$1

mkdir -p build
pushd build
cmake -DKERNEL_VER=${kernelver} ..
popd

# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.

cmake_minimum_required(VERSION 2.8.11)
project(efa C)

set(KERNEL_VER "" CACHE STRING "Kernel version to build for")
if(NOT KERNEL_VER)
  execute_process(COMMAND uname -r OUTPUT_VARIABLE uname_r
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(KERNEL_DIR "/lib/modules/${uname_r}/build")
else()
  set(KERNEL_DIR "/lib/modules/${KERNEL_VER}/build")
endif()

unset(KERNEL_MAKEFILE CACHE)
find_file(KERNEL_MAKEFILE Makefile PATHS ${KERNEL_DIR} NO_DEFAULT_PATH)
if(NOT KERNEL_MAKEFILE)
  message(FATAL_ERROR "No kernel Makefile")
endif()
message("-- Kernel directory - ${KERNEL_DIR}")

set(NVIDIA_DIR "" CACHE PATH "Path to NVIDIA directory")
if(NVIDIA_DIR)
  unset(NVIDIA_MODULE_SYMVERS CACHE)
  find_file(NVIDIA_MODULE_SYMVERS Module.symvers PATHS ${NVIDIA_DIR} REQUIRED NO_DEFAULT_PATH)
  unset(NVIDIA_P2P_H CACHE)
  find_file(NVIDIA_P2P_H nv-p2p.h PATHS ${NVIDIA_DIR}/nvidia REQUIRED NO_DEFAULT_PATH)
  if(NOT NVIDIA_MODULE_SYMVERS OR NOT NVIDIA_P2P_H)
    message(FATAL_ERROR "No NVIDIA")
  else()
    message("-- NVIDIA directory - ${NVIDIA_DIR}")
  endif()
endif()

set(GCOV_PROFILE OFF CACHE BOOL "Enable GCOV profiling")

add_subdirectory(src)

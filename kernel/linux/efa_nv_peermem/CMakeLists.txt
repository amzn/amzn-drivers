# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2023-2025 Amazon.com, Inc. or its affiliates. All rights reserved.

cmake_minimum_required(VERSION 3.10)
project(efa_nv_peermem C)

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
  file(GLOB_RECURSE NVIDIA_MODULE_SYMVERS "${NVIDIA_DIR}/Module.symvers")
  if(NOT NVIDIA_MODULE_SYMVERS)
    message(FATAL_ERROR "No NVIDIA Module.symvers found under ${NVIDIA_DIR}")
  endif()
  list(GET NVIDIA_MODULE_SYMVERS 0 NVIDIA_MODULE_SYMVERS)
  message("-- NVIDIA directory - ${NVIDIA_DIR}")
  message("-- NVIDIA module symbols - ${NVIDIA_MODULE_SYMVERS}")
endif()

set(GCOV_PROFILE OFF CACHE BOOL "Enable GCOV profiling")

add_subdirectory(src)

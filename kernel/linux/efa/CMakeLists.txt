# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021-2026 Amazon.com, Inc. or its affiliates. All rights reserved.

cmake_minimum_required(VERSION 3.10)
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

# Detect kernel toolchain
execute_process(COMMAND grep -c CONFIG_CC_IS_CLANG=y ${KERNEL_DIR}/.config
  OUTPUT_VARIABLE kernel_is_clang OUTPUT_STRIP_TRAILING_WHITESPACE)
if(kernel_is_clang)
  set(KERNEL_LLVM ON)
  message("-- Kernel built with Clang/LLVM")
endif()

set(GCOV_PROFILE OFF CACHE BOOL "Enable GCOV profiling")
set(ENABLE_P2P ON CACHE BOOL "Enable Peer-to-peer memory")
set(ENABLE_KVERBS ON CACHE BOOL "Enable kernel verbs support")

add_subdirectory(src)

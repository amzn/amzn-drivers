# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2023-2024 Amazon.com, Inc. or its affiliates. All rights reserved.

list(APPEND efa_nv_peermem_sources
  nv-p2p.h efa_nv_peermem.h efa_nv_peermem_main.c)

string(REPLACE ";" " " efa_nv_peermem_sources_string "${efa_nv_peermem_sources}")
configure_file(Kbuild.in Kbuild @ONLY)

if(NVIDIA_DIR)
  file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/Kbuild
"KBUILD_EXTRA_SYMBOLS = ${NVIDIA_MODULE_SYMVERS}")
endif()

foreach(src ${efa_nv_peermem_sources})
  configure_file(${src} ${src} COPYONLY)
endforeach()

set(module_cmd make -C ${KERNEL_DIR} M=${CMAKE_CURRENT_BINARY_DIR})
if(GCOV_PROFILE)
  set(module_cmd ${module_cmd} GCOV_PROFILE=y)
endif()
add_custom_command(OUTPUT efa_nv_peermem.ko
 COMMAND ${module_cmd} modules
 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
 DEPENDS ${efa_nv_peermem_sources} ${CMAKE_CURRENT_BINARY_DIR}/Kbuild
 VERBATIM)

add_custom_target(modules ALL DEPENDS efa_nv_peermem.ko)
add_custom_target(modules_clean COMMAND ${module_cmd} clean)

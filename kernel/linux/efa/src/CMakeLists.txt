# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.

list(APPEND efa_sources
  efa_com_cmd.c efa_com.c efa_main.c efa_verbs.c efa_sysfs.c
  efa_admin_cmds_defs.h efa_admin_defs.h efa_com_cmd.h efa_com.h
  efa_common_defs.h efa_regs_defs.h efa_sysfs.h
  efa-abi.h efa.h kcompat.h)
if(ENABLE_P2P)
  list(APPEND efa_sources efa_p2p.c efa_p2p.h efa_gdr.c nv-p2p.h
    efa_neuron.c neuron_p2p.h)
endif()

string(REPLACE ";" " " efa_sources_string "${efa_sources}")
configure_file(Kbuild.in Kbuild @ONLY)

foreach(src ${efa_sources})
  configure_file(${src} ${src} COPYONLY)
endforeach()

# Generate config.h
include(${CMAKE_SOURCE_DIR}/config/efa.cmake)
if(ENABLE_P2P)
  config_define(HAVE_EFA_P2P)
  message("-- Peer-to-peer memory enabled")
endif()

set(module_cmd make -C ${KERNEL_DIR} M=${CMAKE_CURRENT_BINARY_DIR})
if(GCOV_PROFILE)
  set(module_cmd ${module_cmd} GCOV_PROFILE=y)
endif()
add_custom_command(OUTPUT efa.ko
  COMMAND ${module_cmd} modules
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${efa_sources} ${CMAKE_CURRENT_BINARY_DIR}/Kbuild ${CMAKE_CURRENT_BINARY_DIR}/config.h
  VERBATIM)

add_custom_target(modules ALL DEPENDS efa.ko)
add_custom_target(modules_clean COMMAND ${module_cmd} clean)

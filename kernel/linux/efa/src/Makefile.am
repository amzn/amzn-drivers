# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

EXTRA_PROGRAMS = efa
efa_SOURCES = efa_com_cmd.c efa_com.c efa_main.c efa_verbs.c efa_sysfs.c
if HAVE_EFA_GDR
efa_SOURCES += efa_gdr.c
endif
module_DATA = efa.ko
include ../Makefile.common

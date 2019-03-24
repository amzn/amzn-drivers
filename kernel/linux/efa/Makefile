# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.

DRIVER_NAME=efa

obj-m += $(DRIVER_NAME).o
$(DRIVER_NAME)-objs := efa_com_cmd.o \
	efa_com.o \
	efa_main.o \
	efa_verbs.o \
	efa_sysfs.o

ccflags-y := -I$(src)

KERNEL_VERSION ?= $(shell uname -r)

ccflags-y += -Wfatal-errors
all:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(CURDIR) modules

clean:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(CURDIR) clean

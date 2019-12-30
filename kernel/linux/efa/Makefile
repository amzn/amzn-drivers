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

nvidia_ver=$(shell modinfo -F version nvidia 2>/dev/null)
nvidia_src=$(shell ls -d /usr/src/nvidia-$(nvidia_ver)/ 2>/dev/null)
ifeq ($(shell test -d "$(nvidia_src)" && echo "true" || echo "" ),true)
NV_P2P_H=$(shell ls -1 $(nvidia_src)/nvidia/nv-p2p.h 2>/dev/null | tail -1)
NV_SYMVERS=$(shell ls -1 $(nvidia_src)/Module.symvers 2>/dev/null | tail -1)
GDR=true
KBUILD_EXTRA_SYMBOLS := $(NV_SYMVERS)
KBUILD_CPPFLAGS += -DHAVE_EFA_GDR
$(DRIVER_NAME)-objs += efa_gdr.o
endif

ccflags-y += -Wfatal-errors
modules:
ifeq ($(shell test -n "$(GDR)" && echo "true" || echo ""),true)
	cp -f $(NV_P2P_H) $(PWD)/nv-p2p.h
endif
	$(MAKE) -C /lib/modules/$(KERNEL_VERSION)/build M=$(CURDIR) modules

install: modules
	$(MAKE) -C /lib/modules/$(KERNEL_VERSION)/build M=$(CURDIR) modules_install

clean:
	$(MAKE) -C /lib/modules/$(KERNEL_VERSION)/build M=$(CURDIR) clean
ifeq ($(shell test -n "$(GDR)" && echo "true" || echo ""),true)
	rm -f nv-p2p.h
endif

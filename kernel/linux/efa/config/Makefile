# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

DRIVER_NAME = conftest
ccflags-y += -Werror -Wno-unused-variable -Wno-uninitialized
ifdef CONFIG_CC_IS_CLANG
ccflags-y += -Wno-unused-value
else
ccflags-y += -Wno-maybe-uninitialized
endif

obj-m += $(DRIVER_NAME).o
$(DRIVER_NAME)-objs := main.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

MAKE_OPTS :=
# When LLVM is set, don't pass CC/LD to let the kernel build use LLVM tools
ifeq ($(LLVM),)
  ifneq ($(origin CC),default)
    MAKE_OPTS += CC=$(CC)
  endif
  ifneq ($(origin LD),default)
    MAKE_OPTS += LD=$(LD)
  endif
endif
ifneq ($(LLVM),)
MAKE_OPTS += LLVM=$(LLVM)
endif

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) $(MAKE_OPTS) modules

ENA_COM_PATH=../common/ena_com/

obj-m += ena.o
ena-objs := ena_netdev.o \
		ena_ethtool.o \
		ena_lpc.o	\
		ena_xdp.o \
		dim.o \
		ena_devlink.o \
		net_dim.o \
		$(ENA_COM_PATH)/ena_com.o \
		$(ENA_COM_PATH)/ena_eth_com.o

ena-$(CONFIG_SYSFS) += ena_sysfs.o

ccflags-y := -I$(src)
ccflags-y += -I$(src)/$(ENA_COM_PATH)/


BUILD_KERNEL ?= $(shell uname -r)

ccflags-y += -Wfatal-errors

GENERATED_EXISTS := $(shell if [ -d include/generated ]; then echo True; fi)

ifneq ($(strip $(GENERATED_EXISTS)),)
  IS_UEK=$(shell grep uek include/generated/utsrelease.h)

  ifneq ($(strip $(IS_UEK)),)
    IS_3_8_13=$(shell grep -F 3.8.13 include/generated/utsrelease.h)

    ifneq ($(strip $(IS_3_8_13)),)
      ccflags-y += -DUEK3_RELEASE
    endif
  endif
endif

ifneq ("$(wildcard /etc/os-release)","")
  IS_DEBIAN=$(shell grep Debian /etc/os-release)

  ifneq ($(strip $(IS_DEBIAN)),)
    DEBIAN_VERSION_LINE=$(shell grep VERSION_ID /etc/os-release)
    REMOVE_QUOTES = $(subst $\",,$(DEBIAN_VERSION_LINE))
    REMOVE_EQUAL_SIGN = $(subst $\=,,$(REMOVE_QUOTES))
    DEBIAN_VERSION = $(subst VERSION_ID,,$(REMOVE_EQUAL_SIGN))
    ccflags-y += -DDEBIAN_VERSION=$(DEBIAN_VERSION)
  endif
endif

ifdef UBUNTU_ABI
	ccflags-y += -DUTS_UBUNTU_RELEASE_ABI=$(UBUNTU_ABI)
endif

ifdef TEST_AF_XDP
       ccflags-y += -DENA_TEST_AF_XDP
endif

all:
	$(MAKE) -C /lib/modules/$(BUILD_KERNEL)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(BUILD_KERNEL)/build M=$(CURDIR) clean

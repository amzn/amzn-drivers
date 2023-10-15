ENA_COM_PATH=../common/ena_com/

DRIVER_NAME := ena

$(DRIVER_NAME)-srcs := ena_netdev.c \
		ena_ethtool.c \
		ena_lpc.c	\
		ena_phc.c \
		ena_xdp.c \
		dim.c \
		ena_devlink.c \
		net_dim.c \
		$(ENA_COM_PATH)/ena_com.c \
		$(ENA_COM_PATH)/ena_eth_com.c

$(DRIVER_NAME)-sysfs-srcs := ena_sysfs.c

obj-m += $(DRIVER_NAME).o
$(DRIVER_NAME)-objs := $(patsubst %.c,%.o,$($(DRIVER_NAME)-srcs))
$(DRIVER_NAME)-$(CONFIG_SYSFS) += $(patsubst %.c,%.o,$($(DRIVER_NAME)-sysfs-srcs))

ccflags-y := -I$(src)
ccflags-y += -I$(src)/$(ENA_COM_PATH)/


BUILD_KERNEL ?= $(shell uname -r)
# allow user to override the build directory
KERNEL_BUILD_DIR ?= /lib/modules/$(BUILD_KERNEL)/build

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

get_kernel_subversions = $(shell \
  subversions_array_tmp=(`uname -r | sed 's/[^0-9]\+/ /g ; s/\s$$//'`) && \
  for (( i = 0; i < 6; i++ )); do \
    subversions_array[$$i]="ENA_KERNEL_SUBVERSION_$$(($$i+1))=$${subversions_array_tmp[$$i]:-0}"; \
  done && \
  echo $${subversions_array[*]})

ifneq ("$(wildcard /etc/os-release)","")
  IS_DEBIAN=$(shell grep Debian /etc/os-release)

  ifneq ($(strip $(IS_DEBIAN)),)
    DEBIAN_VERSION_LINE=$(shell grep VERSION_ID /etc/os-release)
    REMOVE_QUOTES = $(subst $\",,$(DEBIAN_VERSION_LINE))
    REMOVE_EQUAL_SIGN = $(subst $\=,,$(REMOVE_QUOTES))
    DEBIAN_VERSION = $(subst VERSION_ID,,$(REMOVE_EQUAL_SIGN))
    ccflags-y += -DDEBIAN_VERSION=$(DEBIAN_VERSION)
  endif

  IS_SUSE=$(shell grep SUSE /etc/os-release)
  IS_UEK=$(shell uname -r | grep uek)
  IS_FEDORA=$(shell grep Fedora /etc/os-release)
  ifneq ($(strip $(IS_FEDORA)),)
    ccflags-y += -DFEDORA_RELEASE
  endif

  IS_KERNEL_SUBVERSIONS_NEEDED=$(IS_SUSE)$(IS_UEK)

  ifneq ($(strip $(IS_KERNEL_SUBVERSIONS_NEEDED)),)
    ifneq ($(strip $(IS_UEK)),)
      ccflags-y += -DIS_UEK
    endif

    ENA_KERNEL_SUBVERSIONS := $(call get_kernel_subversions)
    $(foreach subversion,$(ENA_KERNEL_SUBVERSIONS),$(eval ccflags-y += -D$(subversion)))
  endif

endif

ifdef UBUNTU_ABI
	ccflags-y += -DUTS_UBUNTU_RELEASE_ABI=$(UBUNTU_ABI)
endif

ifdef TEST_AF_XDP
	ccflags-y += -DENA_TEST_AF_XDP
endif

ifdef ENA_PHC_INCLUDE
	ccflags-y += -DENA_PHC_INCLUDE
endif

all: $(DRIVER_NAME).ko

$(DRIVER_NAME).ko: config.h $($(DRIVER_NAME)-srcs) $($(DRIVER_NAME)-sysfs-srcs)
	$(MAKE) -C $(KERNEL_BUILD_DIR) M=$(CURDIR) modules

config.h:
	./configure.sh --kernel-dir $(KERNEL_BUILD_DIR) --verbose

clean:
	$(MAKE) -C $(KERNEL_BUILD_DIR) M=$(CURDIR) clean
	rm -f config.h
	rm -rf cc_test
	rm -f $(ENA_COM_PATH)/*.o $(ENA_COM_PATH)/.*.o.cmd

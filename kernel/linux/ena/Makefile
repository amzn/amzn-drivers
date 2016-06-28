ENA_COM_PATH=../common/ena_com/

obj-m += ena.o
ena-objs := ena_netdev.o \
		ena_ethtool.o \
		$(ENA_COM_PATH)/ena_com.o \
		$(ENA_COM_PATH)/ena_eth_com.o

ena-$(CONFIG_SYSFS) += ena_sysfs.o

ccflags-y := -I$(src)
ccflags-y += -I$(src)/$(ENA_COM_PATH)/


BUILD_KERNEL ?= $(shell uname -r)

ccflags-y += -Wfatal-errors
all:
	make -C /lib/modules/$(BUILD_KERNEL)/build M=$(CURDIR) modules

clean:
	make -C /lib/modules/$(BUILD_KERNEL)/build M=$(CURDIR) clean

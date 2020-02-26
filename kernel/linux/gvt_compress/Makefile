#obj-$(CRYPTO_DEV_AWS_GVT_COMPRESS) += gvt_compress.o
obj-m += gvt_compress.o

gvt_compress-objs := gvt_compress_pci.o
gvt_compress-objs += gvt_compress_core.o
gvt_compress-objs += gvt_compress_alg.o
gvt_compress-objs += gvt_compress_device.o
gvt_compress-objs += gvt_compress_device_init.o
gvt_compress-objs += gvt_compress_sysfs.o

ccflags-y := -Werror

BUILD_KERNEL ?= $(shell uname -r)
LINUX_KERNEL_PATH ?= /lib/modules/$(BUILD_KERNEL)/build

all:
	$(MAKE) -C $(LINUX_KERNEL_PATH) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(LINUX_KERNEL_PATH) M=$(CURDIR) clean

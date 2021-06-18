#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2015-2020 Amazon.com, Inc. or its affiliates.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# # Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# # Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in
# the documentation and/or other materials provided with the
# distribution.
# # Neither the name of copyright holder nor the names of its
# contributors may be used to endorse or promote products derived
# from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


.include <bsd.own.mk>

.PATH:  ${.CURDIR}
.PATH:  ${.CURDIR}/ena_com
KMOD    = if_ena

# Shared source
SRCS    = device_if.h bus_if.h pci_if.h
SRCS	+= ena_com.c ena_eth_com.c
SRCS	+= ena.c ena_sysctl.c ena_datapath.c ena_netmap.c

CFLAGS  += -DSMP -O2
CFLAGS  += -I${.CURDIR}/ena_com

# Enable debug messages and sysctls
# CFLAGS  += -DENA_DEBUG

# Enable logging on Tx/Rx data path.
# When disabled, ena_log_io calls remain compiled out for performance purposes.
# CFLAGS += -DENA_LOG_IO_ENABLE

# Add netmap support in the driver.
# Uncomment this only if the netmap is enabled in the kernel.
# CFLAGS += -DDEV_NETMAP

# Add kernel side RSS support in the driver.
# Uncomment this only if RSS is enabled in the kernel.
# CFLAGS += -DRSS

clean:
	rm -f opt_global.h opt_bdg.h device_if.h bus_if.h pci_if.h setdef* *_StripErr
	rm -f *.o *.kld *.ko .*.o
	rm -f @ export_syms machine x86

.include <bsd.kmod.mk>

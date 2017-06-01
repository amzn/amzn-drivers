#
# BSD LICENSE
#
# Copyright (c) 2015-2017 Amazon.com, Inc. or its affiliates.
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
SRCS	+= ena.c ena_com.c ena_eth_com.c ena_sysctl.c

CFLAGS  += -DSMP -O2

# Enable debug messages and sysctls
# CFLAGS  += -DENA_DEBUG

# Print additional driver info
# CFLAGS  += -DENA_TRACE

# Level of the additional driver infromation that will be printed.
# Requires ENA_TRACE flag for working. If not set, default value will be 0x003.
# It is equal to ENA_ALERT and ENA_WARNING flags enabled.
# We are supporting 4 main level of debug info and 5 detailed flags. To use
# multiple of them, they must be summed.
# ENA_ALERT   (0x001) - Alerts are providing additional info about errors.
# ENA_WARNING (0x002) - Driver output is more error sensitive.
# ENA_INFO    (0x004) - Provides additional driver info.
# ENA_DBG     (0x008) - Driver output for debugging purposes.
# Detailed info that will be printed with ENA_INFO or ENA_DEBUG flag
# ENA_TXPTH   (0x010) - Allows TX path tracing.
# ENA_RXPTH   (0x020) - Allows RX path tracing.
# ENA_RSC     (0x040) - Must go with TXPTH or RXPTH. Info about freeing or
#	allocating resources. It will make driver initialization very slow.
# ENA_IOQ     (0x080) - Detailed info about IO queues.
# ENA_ADMQ    (0x100) - Detailed info about admin queue.
#CFLAGS  += -DENA_DEBUG_LEVEL=0x003

# NOTE: you can only define RSS if the OS version has support (11.0)
#CFLAGS  += -DRSS

clean:
	rm -f opt_bdg.h device_if.h bus_if.h pci_if.h setdef* *_StripErr
	rm -f *.o *.kld *.ko .*.o
	rm -f @ export_syms machine x86

.include <bsd.kmod.mk>

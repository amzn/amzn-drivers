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
.PATH:  ${.CURDIR}/ena-com
KMOD    = if_ena

# Shared source
SRCS    = device_if.h bus_if.h pci_if.h
SRCS	+= ena_com.c ena_eth_com.c
SRCS	+= ena.c ena_sysctl.c ena_datapath.c ena_netmap.c ena_rss.c

CFLAGS  += -DSMP -O2
CFLAGS  += -I${.CURDIR}/ena-com

KERNEL_VERSION := `sysctl -n kern.osreldate`
CFLAGS  += -D__FreeBSD_version=$(KERNEL_VERSION)

.if defined(DEBUG)
# Enable debug messages and sysctls
CFLAGS  += -DENA_DEBUG
# Enable logging on Tx/Rx data path.
# When disabled, ena_log_io calls remain compiled out for performance purposes.
CFLAGS += -DENA_LOG_IO_ENABLE
.endif

# Try to determine if the driver should be built with the DEV_NETMAP option.
# It will be enabled if:
# - user doesn't define DEV_NETMAP variable and the current kernel has been
#   built with the 'device netmap'
# - user defines DEV_NETMAP variable and it's value is not 0.
.if !make(clean) && ((!defined(DEV_NETMAP) && \
    ${:!sysctl dev.netmap > /dev/null 2>&1; echo \$?!} == 0) || \
    (defined(DEV_NETMAP) && ${DEV_NETMAP} != 0))
.info Enabling DEV_NETMAP for the driver.
CFLAGS += -DDEV_NETMAP
.endif

# Try to determine if the driver should be built with the RSS option.
# The requirements are similar as for the DEV_NETMAP option, except that the
# script checks for the RSS option and variable instead.
.if !make(clean) && ((!defined(RSS) && \
    ${:!sysctl net.inet.rss > /dev/null 2>&1; echo \$?!} == 0) || \
    (defined(RSS) && ${RSS} != 0))
.info Enabling kernel RSS for the driver.
CFLAGS += -DRSS
.endif

# Try to determine if the driver should be built with the extra debugging
# facilities which also must be provided by the kernel.
# As those options may affect driver's performance by adding extra sanity
# checks, they must be explicitely enabled by passing the DEBUG variable to
# the make command.
# Please note that enabling those options in the kernel will affect the driver's
# performance anyway.
.if !make(clean) && defined(DEBUG) && ((!defined(INVARIANT_SUPPORT) && \
    ${:!sysctl kern.features.invariant_support > /dev/null 2>&1; echo \$?!} == 0) || \
    (defined(INVARIANT_SUPPORT) && ${INVARIANT_SUPPORT} != 0))
.info Enabling INVARIANT_SUPPORT debug flag.
CFLAGS += -DINVARIANT_SUPPORT
.endif
.if !make(clean) && defined(DEBUG) && ((!defined(INVARIANTS) && \
    ${:!sysctl kern.features.invariants > /dev/null 2>&1; echo \$?!} == 0) || \
    (defined(INVARIANTS) && ${INVARIANTS} != 0))
.info Enabling INVARIANTS debug flag.
CFLAGS += -DINVARIANTS
.endif
.if !make(clean) && defined(DEBUG) && ((!defined(WITNESS) && \
    ${:!sysctl kern.features.witness > /dev/null 2>&1; echo \$?!} == 0) || \
    (defined(WITNESS) && ${WITNESS} != 0))
.info Enabling WITNESS debug flag.
CFLAGS += -DWITNESS
.endif
.if !make(clean) && defined(DEBUG) && ((!defined(WITNESS_SKIPSPIN) && \
    ${:!sysctl debug.witness.skipspin 2> /dev/null | grep '1' > /dev/null; echo \$?!} == 0) || \
    (defined(WITNESS_SKIPSPIN) && ${WITNESS_SKIPSPIN} != 0))
.info Enabling WITNESS_SKIPSPIN debug flag.
CFLAGS += -DWITNESS_SKIPSPIN
.endif

clean:
	rm -f opt_global.h opt_bdg.h device_if.h bus_if.h pci_if.h setdef* *_StripErr
	rm -f *.o *.kld *.ko .*.o
	rm -f @ export_syms machine x86

.include <bsd.kmod.mk>

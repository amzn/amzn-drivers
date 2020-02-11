# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

AC_INIT([efa], [1.5.1], [])

AC_CONFIG_SRCDIR([src/efa_main.c])
AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC

AC_ARG_WITH(kerneldir,
	AC_HELP_STRING([--with-kerneldir=DIR], [path to kernel directory]),
	[kerneldir=$withval], [kerneldir=/lib/modules/$(uname -r)])
AC_SUBST([kerneldir])

AC_ARG_WITH(gdr,
	AC_HELP_STRING([--with-gdr=DIR], [path to nvidia driver for GPUDirect RDMA support]),
	[nvidia=$withval], [])
AC_SUBST([nvidia])

AS_IF([test -n "$nvidia"], [
	AS_IF([test -f $nvidia/Module.symvers && test -f $nvidia/nvidia/nv-p2p.h],
	[
		AC_DEFINE([HAVE_EFA_GDR], 1, efa gdr enabled)
		gdr="yes"
	],
	[AC_MSG_ERROR([NVIDIA P2P path incorrect or doesn't contain Module.symvers and nvidia/nv-p2p.h])])
], [gdr="no"])
AM_CONDITIONAL([HAVE_EFA_GDR], [test "x$gdr" = "xyes"])

EFA_CONFIG_RDMA

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
cat <<EOF
***
*** efa driver has been configured
*** Kernel directory: $kerneldir
*** GPUDirect RDMA: $gdr
***
EOF

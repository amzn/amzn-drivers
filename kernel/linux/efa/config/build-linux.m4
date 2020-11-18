# Copyright (C) 1996-2014 Free Software Foundation, Inc.
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

# EFA_LANG_PROGRAM(C)([PROLOGUE], [BODY])
# --------------------------------------
m4_define([EFA_LANG_PROGRAM],
[
#include <linux/kernel.h>
#include <rdma/ib_verbs.h>
$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}])

m4_define([EFA_TRY_COMPILE2], [
	{
	tmpdir=$(mktemp -d /tmp/XXXX)
	/bin/cp config/Makefile $tmpdir
	echo "AC_LANG_SOURCE([EFA_LANG_PROGRAM([[$1]], [[$2]])])" > $tmpdir/main.c
	AS_IF([make -C $tmpdir KERNEL_DIR=$kerneldir/build >/dev/null 2>$tmpdir/output.log], [$3], [$4])
	cat $tmpdir/output.log >> config/output.log
	rm -fr $tmpdir
	}
])

AC_DEFUN([EFA_PARALLEL_INIT_ONCE],
[
if [[ "X${RAN_EFA_PARALLEL_INIT_ONCE}" != "X1" ]]; then
	MAX_JOBS=$(nproc)
	RAN_EFA_PARALLEL_INIT_ONCE=1
	rm -f config/output.log
	declare -i RUNNING_JOBS=0
fi

AC_MSG_CHECKING([if basic compilation works])
EFA_TRY_COMPILE2([
], [
], [
	AC_MSG_RESULT(yes)
], [
	AC_MSG_RESULT(no)
	AC_MSG_ERROR(basic conftest compilation not working)
])
])

AC_DEFUN([EFA_TRY_COMPILE],
[
# wait if there are MAX_JOBS tests running
if [[ $RUNNING_JOBS -eq $MAX_JOBS ]]; then
	wait
	RUNNING_JOBS=0
else
	let RUNNING_JOBS++
fi

# run test in background if MAX_JOBS > 1
if [[ $MAX_JOBS -eq 1 ]]; then
EFA_TRY_COMPILE2([$1], [$2], [$3], [$4])
RUNNING_JOBS=0
else
{
EFA_TRY_COMPILE2([$1], [$2], [$3], [$4])
}&
fi
])

AC_DEFUN([EFA_TRY_COMPILE_DEV_OR_OPS_FUNC],
[
	EFA_TRY_COMPILE([
		$3
	], [
		struct ib_device_ops ops = {
			.$1 = $2,
		};
	], [
		$4
	], [
		$5
	])

	EFA_TRY_COMPILE([
		$3
	], [
		struct ib_device ibdev = {
			.$1 = $2,
		};
	], [
		$4
	], [
		$5
	])
])

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021-2026 Amazon.com, Inc. or its affiliates. All rights reserved.

tmpdir=$1
kerneldir=$2
success=$3
fail=$4

make -C $tmpdir KERNEL_DIR=$kerneldir >/dev/null 2>$tmpdir/output.log
err=$?

if [[ "$err" -eq 0 ]] && [[ -n "$success" ]]; then
	{ flock 1; echo "#define $success 1"; } >> config.h
fi

if [[ "$err" -ne 0 ]] && [[ -n "$fail" ]]; then
	{ flock 1; echo "#define $fail 1"; } >> config.h
fi

{ flock 1; cat $tmpdir/output.log; } >> ../output.log
rm -fr $tmpdir

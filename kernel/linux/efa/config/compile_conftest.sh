#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021-2026 Amazon.com, Inc. or its affiliates. All rights reserved.

tmpdir=$1
make_opts=$2
success=$3
fail=$4

make -C $tmpdir $make_opts >/dev/null 2>$tmpdir/output.log
err=$?

if [[ "$err" -eq 0 ]] && [[ -n "$success" ]]; then
	echo "#define $success 1" >> config.h
fi

if [[ "$err" -ne 0 ]] && [[ -n "$fail" ]]; then
	echo "#define $fail 1" >> config.h
fi

cat $tmpdir/output.log >> ../output.log
rm -fr $tmpdir

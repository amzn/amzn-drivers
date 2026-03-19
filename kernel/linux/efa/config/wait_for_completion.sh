#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.

donefile=$1

while [ ! -f "$donefile" ]; do
	sleep 0.2
done
rm -f "$donefile"

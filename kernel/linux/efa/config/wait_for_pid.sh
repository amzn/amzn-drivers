#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.

pid=$1

while true; do
	ls /proc/$pid &>/dev/null
	err=$?
	if [[ $err -ne 0 ]]; then
		break
	else
		# Docker doesn't reap zombies by default, handle them explicitly
		if [[ $(cat /proc/$pid/stat 2>/dev/null | awk '{ print $3 }') == "Z" ]]; then
			break
		fi
	fi
	sleep 0.2
done

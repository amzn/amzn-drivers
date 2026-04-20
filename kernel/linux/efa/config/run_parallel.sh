#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2026 Amazon.com, Inc. or its affiliates. All rights reserved.

# Execute commands from a file in parallel and wait for all to complete.

while IFS= read -r cmd; do
	eval "$cmd" &
done < "$1"

wait

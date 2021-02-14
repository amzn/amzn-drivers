#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.

nohup "$@" &>/dev/null </dev/null &
echo $!

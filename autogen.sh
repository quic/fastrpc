#!/bin/sh

# Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

autoreconf --verbose --force --install || {
	echo 'autogen.sh failed';
	exit 1;
}

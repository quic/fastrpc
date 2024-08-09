// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __FASTRPC_ERROR_H__
#define __FASTRPC_ERROR_H__

// Kernel errno start
#define MIN_KERNEL_ERRNO 0
// Kernel errno end
#define MAX_KERNEL_ERRNO 135

int convert_dsp_error_to_user_error(int err);
int check_rpc_error(int err);
int convert_kernel_to_user_error(int nErr, int err_no);

#endif /* __FASTRPC_ERROR_H__ */
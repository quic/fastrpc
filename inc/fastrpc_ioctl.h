// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_INTERNAL_UPSTREAM_H
#define FASTRPC_INTERNAL_UPSTREAM_H

#include <sys/ioctl.h>
#include <include/misc/fastrpc.h>
/* File only compiled  when support to upstream kernel is required*/



#define ADSPRPC_DEVICE "/dev/fastrpc-adsp"
#define SDSPRPC_DEVICE "/dev/fastrpc-sdsp"
#define MDSPRPC_DEVICE "/dev/fastrpc-mdsp"
#define CDSPRPC_DEVICE "/dev/fastrpc-cdsp"
#define ADSPRPC_SECURE_DEVICE "/dev/fastrpc-adsp-secure"
#define SDSPRPC_SECURE_DEVICE "/dev/fastrpc-sdsp-secure"
#define MDSPRPC_SECURE_DEVICE "/dev/fastrpc-mdsp-secure"
#define CDSPRPC_SECURE_DEVICE "/dev/fastrpc-cdsp-secure"

#define FASTRPC_ATTR_NOVA (256)

/* Secure and default device nodes */
#if DEFAULT_DOMAIN_ID==ADSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-adsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-adsp"
#elif DEFAULT_DOMAIN_ID==MDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-mdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-mdsp"
#elif DEFAULT_DOMAIN_ID==SDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-sdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-sdsp"
#elif DEFAULT_DOMAIN_ID==CDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-cdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-cdsp"
#else
	#define SECURE_DEVICE ""
	#define DEFAULT_DEVICE ""
#endif

#define INITIALIZE_REMOTE_ARGS(total)	struct fastrpc_invoke_args* args = NULL; \
					int *pfds = NULL; \
					unsigned *pattrs = NULL; \
					args = (struct fastrpc_invoke_args*) calloc(sizeof(*args), total);	\
					if(args==NULL) { 	\
						goto bail;	\
					}

#define DESTROY_REMOTE_ARGS()		if(args) {	\
						free(args);	\
					}

#define set_args(i, pra, len, filedesc, attrs)	args[i].ptr = (uint64_t)pra; \
						args[i].length = len;	\
						args[i].fd = filedesc;	\
						args[i].attr = attrs;

#define set_args_ptr(i, pra)		args[i].ptr = (uint64_t)pra
#define set_args_len(i, len)		args[i].length = len
#define set_args_attr(i, attrs)		args[i].attr = attrs
#define set_args_fd(i, filedesc)	args[i].fd = filedesc
#define get_args_ptr(i)			args[i].ptr
#define get_args_len(i)			args[i].length
#define get_args_attr(i)		args[i].attr
#define get_args_fd(i)			args[i].fd
#define append_args_attr(i, attrs)	args[i].attr |= attrs
#define get_args()			args
#define is_upstream()			1

//Utility macros for reading the ioctl structure
#define NOTIF_GETDOMAIN(response)	-1;
#define NOTIF_GETSESSION(response)	-1;
#define NOTIF_GETSTATUS(response)	-1;

#define FASTRPC_INVOKE2_STATUS_NOTIF		2	//TODO: Temporary change (Bharath to fix)
#define FASTRPC_INVOKE2_KERNEL_OPTIMIZATIONS	1	//TODO: Temporary change (Bharath to fix)
#ifndef FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK
#define FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK  1
#endif

struct fastrpc_ioctl_control {
	__u32 req;
	union {
		struct fastrpc_ctrl_latency lp;
		struct fastrpc_ctrl_smmu smmu;
		struct fastrpc_ctrl_kalloc kalloc;
		struct fastrpc_ctrl_wakelock wl;
		struct fastrpc_ctrl_pm pm;
	};
};

#endif // FASTRPC_INTERNAL_H

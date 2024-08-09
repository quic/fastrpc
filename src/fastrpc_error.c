// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include "AEEstd.h"
#include "AEEStdErr.h"
#include "fastrpc_error.h"

/*
 * convert_dsp_error_to_user_error() - Convert DSP error to user error
 * @err: DSP error code
 * Returns: user error code
 */
int convert_dsp_error_to_user_error(int err) {
	int nErr = err;
	uint32_t dsperr = (uint32_t)err;

	if (err == AEE_EUNKNOWN || err == AEE_SUCCESS) {
		return nErr;
	} else if (dsperr > DSP_AEE_EOFFSET) {
		dsperr = dsperr - DSP_AEE_EOFFSET;
		switch (dsperr) {
			case AEE_EUNSUPPORTED:
			case AEE_EUNSUPPORTEDAPI:
			case AEE_EVERSIONNOTSUPPORT:
				nErr = AEE_EUNSUPPORTED + DSP_AEE_EOFFSET;
				break;
			case AEE_EBADPARM:
				nErr = AEE_EBADPARM + DSP_AEE_EOFFSET;
				break;
			case AEE_ENOMEMORY:
				nErr = AEE_ENOMEMORY + DSP_AEE_EOFFSET;
				break;
			case AEE_ENORPCMEMORY:
				nErr = AEE_ENORPCMEMORY + DSP_AEE_EOFFSET;
				break;
			case AEE_EUNABLETOLOAD:
				nErr = AEE_EUNABLETOLOAD + DSP_AEE_EOFFSET;
				break;
			case AEE_EINTERRUPTED:
				nErr = AEE_EINTERRUPTED + DSP_AEE_EOFFSET;
				break;
			case AEE_EFAILED:
				nErr = AEE_EFAILED + DSP_AEE_EOFFSET;
				break;
			case AEE_EBADSTATE:
				nErr = AEE_EBADSTATE + DSP_AEE_EOFFSET;
				break;
			case AEE_ENOSUCH:
				nErr = AEE_ENOSUCH + DSP_AEE_EOFFSET;
				break;
			default:
				if(dsperr > AEE_EOFFSET && dsperr <= AEE_EOFFSET+1024) {
					nErr = AEE_ERPC + DSP_AEE_EOFFSET;
				}
				/* nErr will be equal to err for user defined errors */
				break;
		}
	}
	return nErr;
}

int check_rpc_error(int err) {
  if (check_error_code_change_present() == 1) {
    if (err > KERNEL_ERRNO_START && err <= HLOS_ERR_END) // driver or HLOS err
      return 0;
    else if (err > (int)DSP_AEE_EOFFSET &&
             err <= (int)DSP_AEE_EOFFSET + 1024) // DSP err
      return 0;
    else if (err == AEE_ENOSUCH ||
             err == AEE_EINTERRUPTED) // common DSP HLOS err
      return 0;
    else
      return -1;
  } else
    return 0;
}

/**
  * @brief Convert kernel to user error.
  * @nErr: Error from ioctl
  * @err_no: errno from kernel
  * returns user error
  **/

int convert_kernel_to_user_error(int nErr, int err_no) {
	if (!(nErr == AEE_EUNKNOWN && err_no && (err_no >= MIN_KERNEL_ERRNO && err_no <= MAX_KERNEL_ERRNO))) {
		return nErr;
	}

	switch (err_no) {
	case EIO:  /* EIO 5 I/O error */
	case ETOOMANYREFS: /* ETOOMANYREFS 109 Too many references: cannot splice */
	case EADDRNOTAVAIL: /* EADDRNOTAVAIL 99 Cannot assign requested address */
	case ENOTTY: /* ENOTTY 25 Not a typewriter */
	case EBADRQC: /* EBADRQC 56 Invalid request code */
		nErr = AEE_ERPC;
		break;
	case EFAULT: /* EFAULT 14 Bad address */
	case ECHRNG: /* ECHRNG 44 Channel number out of range */
	case EBADFD: /* EBADFD 77 File descriptor in bad state */
	case EINVAL: /* EINVAL 22 Invalid argument */
	case EBADF: /* EBADF 9 Bad file number */
	case EBADE: /* EBADE 52 Invalid exchange */
	case EBADR: /* EBADR 53 Invalid request descriptor */
	case EOVERFLOW: /* EOVERFLOW 75 Value too large for defined data type */
	case EHOSTDOWN: /* EHOSTDOWN 112 Host is down */
	case EEXIST: /* EEXIST 17 File exists */
	case EBADMSG: /* EBADMSG 74 Not a data message */
		nErr = AEE_EBADPARM;
		break;
	case ENXIO: /* ENXIO 6 No such device or address */
	case ENODEV: /* ENODEV 19 No such device*/
	case ENOKEY: /* ENOKEY 126 Required key not available */
		nErr = AEE_ENOSUCHDEVICE;
		break;
	case ENOBUFS: /* ENOBUFS 105 No buffer space available */
	case ENOMEM: /* ENOMEM 12 Out of memory */
		nErr = AEE_ENOMEMORY;
		break;
	case ENOSR: /* ENOSR 63 Out of streams resources */
	case EDQUOT: /* EDQUOT 122 Quota exceeded */
	case ETIMEDOUT: /* ETIMEDOUT 110 Connection timed out */
	case EUSERS: /* EUSERS 87 Too many users */
	case ESHUTDOWN: /* ESHUTDOWN 108 Cannot send after transport endpoint shutdown */
		nErr = AEE_EEXPIRED;
		break;
	case ENOTCONN:  /* ENOTCONN 107 Transport endpoint is not connected */
	case ECONNREFUSED: /* ECONNREFUSED 111 Connection refused */
		nErr = AEE_ECONNREFUSED;
		break;
	case ECONNRESET: /* ECONNRESET 104 Connection reset by peer */
	case EPIPE: /* EPIPE 32 Broken pipe */
		nErr = AEE_ECONNRESET;
		break;
	case EPROTONOSUPPORT: /* EPROTONOSUPPORT 93 Protocol not supported */
		nErr = AEE_EUNSUPPORTED;
		break;
	case EFBIG: /* EFBIG 27 File too large */
		nErr = AEE_EFILE;
		break;
	case EACCES: /* EACCES 13 Permission denied */
	case EPERM: /* EPERM 1 Operation not permitted */
		nErr = AEE_EBADPERMS;
		break;
	default:
		nErr = AEE_ERPC;
		break;
	}

	return nErr;
}
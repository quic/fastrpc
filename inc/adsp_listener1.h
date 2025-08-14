// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _ADSP_LISTENER1_H
#define _ADSP_LISTENER1_H
#include <AEEStdDef.h>
#include <remote.h>
#include <stdlib.h>
#include <string.h>

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE
#ifndef _QAIC_ENV_H
#define _QAIC_ENV_H

#include <stdio.h>

#ifdef __GNUC__
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#else
#pragma GCC diagnostic ignored "-Wpragmas"
#endif
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifndef _ATTRIBUTE_UNUSED

#ifdef _WIN32
#define _ATTRIBUTE_UNUSED
#else
#define _ATTRIBUTE_UNUSED __attribute__((unused))
#endif

#endif // _ATTRIBUTE_UNUSED

#ifndef _ATTRIBUTE_VISIBILITY

#ifdef _WIN32
#define _ATTRIBUTE_VISIBILITY
#else
#define _ATTRIBUTE_VISIBILITY __attribute__((visibility("default")))
#endif

#endif // _ATTRIBUTE_VISIBILITY

#ifndef __QAIC_REMOTE
#define __QAIC_REMOTE(ff) ff
#endif //__QAIC_REMOTE

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE

#ifndef __QAIC_STUB
#define __QAIC_STUB(ff) ff
#endif //__QAIC_STUB

#ifndef __QAIC_STUB_EXPORT
#define __QAIC_STUB_EXPORT
#endif // __QAIC_STUB_EXPORT

#ifndef __QAIC_STUB_ATTRIBUTE
#define __QAIC_STUB_ATTRIBUTE
#endif // __QAIC_STUB_ATTRIBUTE

#ifndef __QAIC_SKEL
#define __QAIC_SKEL(ff) ff
#endif //__QAIC_SKEL__

#ifndef __QAIC_SKEL_EXPORT
#define __QAIC_SKEL_EXPORT
#endif // __QAIC_SKEL_EXPORT

#ifndef __QAIC_SKEL_ATTRIBUTE
#define __QAIC_SKEL_ATTRIBUTE
#endif // __QAIC_SKEL_ATTRIBUTE

#ifdef __QAIC_DEBUG__
#ifndef __QAIC_DBG_PRINTF__
#include <stdio.h>
#define __QAIC_DBG_PRINTF__(ee)                                               \
	do {                                                                  \
		printf ee;                                                    \
	} while(0)
#endif
#else
#define __QAIC_DBG_PRINTF__(ee) (void)0
#endif

#define _OFFSET(src, sof) ((void *)(((char *)(src)) + (sof)))

#define _COPY(dst, dof, src, sof, sz)                                         \
	do {                                                                  \
		struct __copy {                                               \
			char ar[sz];                                          \
		};                                                            \
		*(struct __copy *)_OFFSET(dst, dof)                           \
		    = *(struct __copy *)_OFFSET(src, sof);                    \
	} while(0)

#define _COPYIF(dst, dof, src, sof, sz)                                       \
	do {                                                                  \
		if(_OFFSET(dst, dof) != _OFFSET(src, sof)) {                  \
			_COPY(dst, dof, src, sof, sz);                        \
		}                                                             \
	} while(0)

_ATTRIBUTE_UNUSED
static __inline void
_qaic_memmove(void *dst, void *src, int size)
{
	int i = 0;
	for(i = 0; i < size; ++i) {
		((char *)dst)[i] = ((char *)src)[i];
	}
}

#define _MEMMOVEIF(dst, src, sz)                                              \
	do {                                                                  \
		if(dst != src) {                                              \
			_qaic_memmove(dst, src, sz);                          \
		}                                                             \
	} while(0)

#define _ASSIGN(dst, src, sof)                                                \
	do {                                                                  \
		dst = OFFSET(src, sof);                                       \
	} while(0)

#define _STD_STRLEN_IF(str) (str == 0 ? 0 : strlen(str))

#include "AEEStdErr.h"

#ifdef _WIN32
#define _QAIC_FARF(level, msg, ...) (void)0
#else
#define _QAIC_FARF(level, msg, ...)                                           \
	do {                                                                  \
		if(0 == (HAP_debug_v2)) {                                     \
			(void)0;                                              \
		} else {                                                      \
			FARF(level, msg, ##__VA_ARGS__);                      \
		}                                                             \
	} while(0)
#endif //_WIN32 for _QAIC_FARF

#define _TRY(ee, func)                                                        \
	do {                                                                  \
		if(AEE_SUCCESS != ((ee) = func)) {                            \
			__QAIC_DBG_PRINTF__((__FILE__ ":%d:error:%d:%s\n",    \
			                     __LINE__, (int)(ee), #func));    \
			goto ee##bail;                                        \
		}                                                             \
	} while(0)

#define _TRY_FARF(ee, func)                                                   \
	do {                                                                  \
		if(AEE_SUCCESS != ((ee) = func)) {                            \
			goto ee##farf##bail;                                  \
		}                                                             \
	} while(0)

#define _CATCH(exception) exception##bail : if(exception != AEE_SUCCESS)

#define _CATCH_FARF(exception)                                                \
	exception##farf##bail : if(exception != AEE_SUCCESS)

#define _ASSERT(nErr, ff) _TRY(nErr, 0 == (ff) ? AEE_EBADPARM : AEE_SUCCESS)

#ifdef __QAIC_DEBUG__
#define _ALLOCATE(nErr, pal, size, alignment, pv)                             \
	_TRY(nErr, _allocator_alloc(pal, __FILE_LINE__, size, alignment,      \
	                            (void **)&pv));                           \
	_ASSERT(nErr, pv || !(size))
#else
#define _ALLOCATE(nErr, pal, size, alignment, pv)                             \
	_TRY(nErr, _allocator_alloc(pal, 0, size, alignment, (void **)&pv));  \
	_ASSERT(nErr, pv || !(size))
#endif

#endif // _QAIC_ENV_H

#ifdef __cplusplus
extern "C" {
#endif
#define _const_adsp_listener1_handle 7
/**
 * Opens the handle in the specified domain.  If this is the first
 * handle, this creates the session.  Typically this means opening
 * the device, aka open("/dev/adsprpc-smd"), then calling ioctl
 * device APIs to create a PD on the DSP to execute our code in,
 * then asking that PD to dlopen the .so and dlsym the skel function.
 *
 * @param uri, <interface>_URI"&_dom=aDSP"
 *    <interface>_URI is a QAIC generated uri, or
 *    "file:///<sofilename>?<interface>_skel_handle_invoke&_modver=1.0"
 *    If the _dom parameter is not present, _dom=DEFAULT is assumed
 *    but not forwarded.
 *    Reserved uri keys:
 *      [0]: first unamed argument is the skel invoke function
 *      _dom: execution domain name, _dom=mDSP/aDSP/DEFAULT
 *      _modver: module version, _modver=1.0
 *      _*: any other key name starting with an _ is reserved
 *    Unknown uri keys/values are forwarded as is.
 * @param h, resulting handle
 * @retval, 0 on success
 */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_open)(
    const char *uri, remote_handle64 *h) __QAIC_HEADER_ATTRIBUTE;
/**
    * Closes a handle.  If this is the last handle to close, the
   session
    * is closed as well, releasing all the allocated resources.

    * @param h, the handle to close
    * @retval, 0 on success, should always succeed
    */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_close)(remote_handle64 h)
    __QAIC_HEADER_ATTRIBUTE;
typedef struct _adsp_listener1_buffer__seq_uint8
    _adsp_listener1_buffer__seq_uint8;
typedef _adsp_listener1_buffer__seq_uint8 adsp_listener1_buffer;
struct _adsp_listener1_buffer__seq_uint8 {
	uint8_t *data;
	int dataLen;
};
typedef uint32_t adsp_listener1_remote_handle;
typedef uint32_t adsp_listener1_invoke_ctx;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_next_invoke)(
    remote_handle64 _h, adsp_listener1_invoke_ctx prevCtx, int prevResult,
    const adsp_listener1_buffer *outBufs, int outBufsLen,
    adsp_listener1_invoke_ctx *ctx, adsp_listener1_remote_handle *handle,
    uint32_t *sc, adsp_listener1_buffer *inBuffers, int inBuffersLen,
    int *inBufLenReq, int inBufLenReqLen, int *routBufLenReq,
    int routBufLenReqLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_invoke_get_in_bufs)(
    remote_handle64 _h, adsp_listener1_invoke_ctx ctx,
    adsp_listener1_buffer *inBuffers,
    int inBuffersLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_init)(remote_handle64 _h)
    __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_init2)(
    remote_handle64 _h) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_next2)(
    remote_handle64 _h, adsp_listener1_invoke_ctx prevCtx, int prevResult,
    const uint8_t *prevbufs, int prevbufsLen, adsp_listener1_invoke_ctx *ctx,
    adsp_listener1_remote_handle *handle, uint32_t *sc, uint8_t *bufs,
    int bufsLen, int *bufsLenReq) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_listener1_get_in_bufs2)(
    remote_handle64 _h, adsp_listener1_invoke_ctx ctx, int offset,
    uint8_t *bufs, int bufsLen, int *bufsLenReq) __QAIC_HEADER_ATTRIBUTE;
#ifndef adsp_listener1_URI
#define adsp_listener1_URI                                                    \
	"file:///"                                                            \
	"libadsp_listener1_skel.so?adsp_listener1_skel_handle_invoke&_"       \
	"modver=1.0"
#endif /*adsp_listener1_URI*/
#ifdef __cplusplus
}
#endif
#endif //_ADSP_LISTENER1_H

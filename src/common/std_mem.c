// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
=======================================================================

FILE:         std_mem.c

SERVICES:     apiOne std lib memory operations stuff

*/

#include <string.h>
#include "AEEstd.h"
#include "AEEStdErr.h"

#if defined __hexagon__
#include "stringl/stringl.h"

//Add a weak reference so shared objects work with older images
#pragma weak memscpy
#pragma weak memsmove
#endif /*__hexagon__*/

void* std_memset(void* p, int c, int nLen)
{
   if (nLen < 0) {
      return p;
   }
   return memset(p, c, (size_t)nLen);
}

void* std_memmove(void* pTo, const void* cpFrom, int nLen)
{
   if (nLen <= 0) {
      return pTo;
   }
#ifdef __hexagon__
   std_memsmove(pTo, (size_t)nLen, cpFrom, (size_t)nLen);
   return pTo;
#else
   return memmove(pTo, cpFrom, (size_t)nLen);
#endif
}

int std_memscpy(void *dst, int dst_size, const void *src, int src_size){
    size_t copy_size = 0;

    if(dst_size <0 || src_size <0){
        return AEE_ERPC;
    }

#if defined (__hexagon__)
    if (memscpy){
        return memscpy(dst,dst_size,src,src_size);
    }
#endif

    copy_size = (dst_size <= src_size)? dst_size : src_size;
    memcpy(dst, src, copy_size);
    return copy_size;
}

int std_memsmove(void *dst, int dst_size, const void *src, int src_size){
    size_t copy_size = 0;

    if(dst_size <0 || src_size <0){
        return AEE_ERPC;
    }

#if defined (__hexagon__)
    if (memsmove){
        return memsmove(dst,dst_size,src,src_size);
    }
#endif

    copy_size = (dst_size <= src_size)? dst_size : src_size;
    memmove(dst, src, copy_size);
    return copy_size;
}


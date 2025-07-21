// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

//
// someday, drop this #include, implement our own memmove()
//
#include <stddef.h>
#include "AEEstd.h"
#include "version.h"

static void* std_memrchr(const void* p, int c, int nLen)
{
   const char* cpc = (const char*)p - 1;

   if (nLen > 0) {
      do {
         if (cpc[nLen] == c) {
            return (void*) (cpc + nLen);
         }
      } while (--nLen);
   }

   return 0;
}

void* std_memrchrbegin(const void* p, int c, int n)
{
   void *pOut = std_memrchr(p, c, n);

   return (pOut ? pOut : (void*)p);
}

char* std_strbegins(const char* cpsz, const char* cpszPrefix)
{
   for (;;) {
      if ('\0' == *cpszPrefix) {
         return (char*)cpsz;
      }

      if (*cpszPrefix != *cpsz) {
         return 0;
      }

      ++cpszPrefix;
      ++cpsz;
   }
   // not reached
}

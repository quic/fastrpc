// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEstd.h"
#include "AEEsmath.h"



static int xMinSize(int a, int b)
{
   if (b < a) {
      a = b;
   }
   return (a >= 0 ? a : 0);
}


static void xMoveBytes(byte *pbDest, const byte *pbSrc, int cb)
{
   if (pbDest != pbSrc) {
      (void) std_memmove(pbDest, pbSrc, cb);
   }
}


#ifdef AEE_BIGENDIAN
#  define STD_COPY       std_CopyBE
#  define STD_COPY_SWAP  std_CopyLE
#else
#  define STD_COPY       std_CopyLE
#  define STD_COPY_SWAP  std_CopyBE
#endif


// See std_CopyLE/BE for documentation.  This function implements the case
// where host ordering != target byte ordering.
//
int STD_COPY_SWAP(void *      pvDest, int nDestSize,
                  const void *pvSrc,  int nSrcSize,
                  const char *pszFields)
{
   byte* pbDest = (byte*)pvDest;
   byte* pbSrc  = (byte*)pvSrc;
   int cbCopied = xMinSize(nDestSize, nSrcSize);
   const char * pszNextField;
   int cb, nSize;

   nSize = 0;  // avoid warning when using RVCT2.2 with -O1

   pszNextField = pszFields;

   for (cb = cbCopied; cb > 0; cb -= nSize) {
      char  ch;

      ch = *pszNextField++;
      if ('\0' == ch) {
         ch = *pszFields;
         pszNextField = pszFields+1;
      }

      if (ch == 'S') {

         // S = 2 bytes

         nSize = 2;
         if (cb < nSize) {
            break;
         } else {
            byte by   = pbSrc[0];
            pbDest[0] = pbSrc[1];
            pbDest[1] = by;
         }
      } else if (ch == 'L') {

         // L = 4 bytes

         nSize = 4;
         if (cb < nSize) {
            break;
         } else {
            byte by   = pbSrc[0];
            pbDest[0] = pbSrc[3];
            pbDest[3] = by;
            by        = pbSrc[1];
            pbDest[1] = pbSrc[2];
            pbDest[2] = by;
         }
      } else if (ch == 'Q') {

         // Q = 8 bytes

         nSize = 8;
         if (cb < nSize) {
            break;
         } else {
            byte by   = pbSrc[0];
            pbDest[0] = pbSrc[7];
            pbDest[7] = by;
            by        = pbSrc[1];
            pbDest[1] = pbSrc[6];
            pbDest[6] = by;
            by        = pbSrc[2];
            pbDest[2] = pbSrc[5];
            pbDest[5] = by;
            by        = pbSrc[3];
            pbDest[3] = pbSrc[4];
            pbDest[4] = by;
         }
      } else {

         // None of the above => read decimal and copy without swap

         if (ch >= '0' && ch <= '9') {
            nSize = (int) (ch - '0');
            while ( (ch = *pszNextField) >= '0' && ch <= '9') {
               nSize = nSize*10 + (int)(ch - '0');
               ++pszNextField;
            }
            // Check bounds & ensure progress
            if (nSize > cb || nSize <= 0) {
               nSize = cb;
            }
         } else {
            // Unexpected character: copy rest of data
            nSize = cb;
         }

         xMoveBytes(pbDest, pbSrc, nSize);
      }

      pbDest += nSize;
      pbSrc += nSize;
   }

   if (cb > 0) {

      // Swap could not be completed:  0 < cb < nSize <= 8

      byte byBuf[8];

      // If entire value is available in source, use swapped version
      if (nSrcSize - (pbSrc - (byte*)pvSrc) >= nSize) {
         int i;
         for (i=0; i<cb; ++i) {
            byBuf[i] = pbSrc[nSize-1-i];
         }
         pbSrc = byBuf;
      }
      std_memmove(pbDest, pbSrc, cb);
   }

   return cbCopied;
}


// See std_CopyLE/BE for documentation.  This function implements the case
// where host ordering == target byte ordering.
//
int STD_COPY(void *pvDest, int nDestSize,
             const void *pvSrc,  int nSrcSize,
             const char *pszFields)
{
   int cb = xMinSize(nDestSize, nSrcSize);
   (void)pszFields;
   xMoveBytes(pvDest, pvSrc, cb);
   return cb;
}



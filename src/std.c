// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

//
// someday, drop this #include, implement our own memmove()
//
#include <stddef.h>
#include "AEEstd.h"
#include "version.h"

int  std_getversion(char *pcDst, int nDestSize)
{
   return std_strlcpy(pcDst, VERSION_STRING, nDestSize);
}


char std_tolower(char c)
{
   if ((c >= 'A') && (c <= 'Z')) {
      c |= 32;
   }
   return c;
}

char std_toupper(char c)
{
   if ((c >= 'a') && (c <= 'z')) {
      c &= ~32;
   }
   return c;
}


static __inline int x_casecmp(unsigned char c1, unsigned char c2)
{
   int diff = c1 - c2;
   if (c1 >= 'A' && c1 <= 'Z') {
      diff += 32;
   }
   if (c2 >= 'A' && c2 <= 'Z') {
      diff -= 32;
   }
   return diff;
}


int std_strncmp(const char* s1, const char* s2, int n)
{
   if (n > 0) {
      int i = 0;

      do {
         unsigned char c1 = (unsigned char)s1[i];
         unsigned char c2 = (unsigned char)s2[i];
         int  diff = c1 - c2;

         if (diff) {
            return diff;
         }

         if ('\0' == c1) {
            break;
         }
         i++;
      } while (i < n);
   }

   return 0;
}

int std_strcmp(const char* s1, const char* s2)
{
   return std_strncmp(s1, s2, MAX_INT32);
}

int std_strnicmp(const char* s1, const char* s2, int n)
{
   if (n > 0) {
      int i = -n;

      s1 += n;
      s2 += n;

      do {
         unsigned char c1 = (unsigned char)s1[i];
         unsigned char c2 = (unsigned char)s2[i];

         int diff = x_casecmp(c1,c2);
         if (diff) {
            return diff;
         }
         if ('\0' == c1) {
            break;
         }
      } while (++i);
   }
   return 0;
}

int std_stricmp(const char* s1, const char* s2)
{
   return std_strnicmp(s1, s2, MAX_INT32);
}

int std_strlcpy(char* pcDst, const char* cpszSrc, int nDestSize)
{
   int nLen = std_strlen(cpszSrc);

   if (0 < nDestSize) {
      int n;

      n = STD_MIN(nLen, nDestSize - 1);
      (void)std_memmove(pcDst, cpszSrc, n);

      pcDst[n] = 0;
   }

   return nLen;
}

int std_strlcat(char* pcDst, const char* cpszSrc, int nDestSize)
{
   int nLen = 0;

   while ((nLen < nDestSize) && (0 != pcDst[nLen])) {
      ++nLen;
   }

   return nLen + std_strlcpy(pcDst+nLen, cpszSrc, nDestSize-nLen);
}

char* std_strstr(const char* cpszHaystack, const char* cpszNeedle)
{
   /* Check the empty needle string as a special case */
   if ('\0' == *cpszNeedle ) {
      return (char*)cpszHaystack;
   }

   while ('\0' != *cpszHaystack) {
      /* Find the first character of the needle string in the haystack string */
      if (*cpszHaystack == *cpszNeedle) {
         /* check if the rest of the string matches */
         const char* pHaystack = cpszHaystack;
         const char* pNeedle = cpszNeedle;
         do {
            if ('\0' == *++pNeedle) {
               /* Found a match */
               return (char*)cpszHaystack;
            }
         } while (*++pHaystack == *pNeedle);
      }
      cpszHaystack++;
   }

   return 0;
}


int std_memcmp(const void* p1, const void* p2, int length)
{
   const unsigned char *cpc1 = p1;
   const unsigned char *cpc2 = p2;

   while (length-- > 0) {
      int diff = *cpc1++ - *cpc2++;

      if (0 != diff) {
         return diff;
      }
   }
   return 0;
}

int std_wstrlen(const AECHAR* s)
{
   const AECHAR *sEnd = s;

   if (! *sEnd)
      return 0;

   do {
      ++sEnd;
   } while (*sEnd);

   return sEnd - s;
}


int std_wstrlcpy(AECHAR* pwcDst, const AECHAR* cpwszSrc, int nDestSize)
{
   int nLen = std_wstrlen(cpwszSrc);

   if (0 < nDestSize) {
      int n;

      n = STD_MIN(nLen, nDestSize - 1);
      /* call memmove, in case n is larger than 1G */
      (void)std_memsmove(pwcDst, nDestSize*sizeof(AECHAR),
                     cpwszSrc, ((size_t)n)*sizeof(AECHAR));

      pwcDst[n] = 0;
   }

   return nLen;
}

int std_wstrlcat(AECHAR* pwcDst, const AECHAR* cpwszSrc, int nDestSize)
{
   int nLen = 0;

   while ((nLen < nDestSize) && (0 != pwcDst[nLen])) {
      ++nLen;
   }

   return nLen + std_wstrlcpy(pwcDst+nLen, cpwszSrc, nDestSize-nLen);
}

char* std_strchrend(const char* cpsz, char c)
{
   while (*cpsz && *cpsz != c) {
      ++cpsz;
   }
   return (char*)cpsz;
}

char* std_strchr(const char* cpszSrch, int c)
{
   const char *pc = std_strchrend(cpszSrch, (char)c);

   return (*pc == c ? (char*)pc : 0);
}

void* std_memstr(const char* cpHaystack, const char* cpszNeedle,
                 int nHaystackLen)
{
   int nLen = 0;

   /* Handle empty needle string as a special case */
   if ('\0' == *cpszNeedle ) {
      return (char*)cpHaystack;
   }

   /* Find the first character of the needle string in the haystack string */
   while (nLen < nHaystackLen) {
      if (cpHaystack[nLen] == *cpszNeedle) {
         /* check if the rest of the string matches */
         const char* cpNeedle = cpszNeedle;
         int nRetIndex = nLen;
         do {
            if ('\0' == *++cpNeedle) {
               /* Found a match */
               return (void*)(cpHaystack + nRetIndex);
            }
            nLen++;
         } while(cpHaystack[nLen] == *cpNeedle);
      }
      else {
         nLen++;
      }
   }

   return 0;
}

void* std_memchrend(const void* p, int c, int nLen)
{
   const char* cpc = (const char*)p + nLen;
   int i = -nLen;

   if (nLen > 0) {
      do {
         if (cpc[i] == c) {
            break;
         }
      } while (++i);
   }
   return (void*) (cpc + i);
}

void* std_memchr(const void* s, int c, int n)
{
   const char *pEnd = (const char*)std_memchrend(s,c,n);
   int nEnd = pEnd - (const char*)s;

   if (nEnd < n) {
      return (void*)pEnd;
   }
   return 0;
}

void* std_memrchr(const void* p, int c, int nLen)
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


char* std_strrchr(const char* cpsz, int c)
{
   return std_memrchr(cpsz, c, std_strlen(cpsz) + 1);
}


void* std_memrchrbegin(const void* p, int c, int n)
{
   void *pOut = std_memrchr(p, c, n);

   return (pOut ? pOut : (void*)p);
}


// x_scanbytes: internal function;  WARNING:  nLen must be >0
//
// cStop = character at which to stop (in addition to cpszChars[...])
//
// Using a bit mask provides a constant-time check for a terminating
// character: 10 instructions for inner loop on ADS12arm9.  Initialization
// overhead is increased, but this is quickly made up for as searching begins.
//
//
static char *x_scanbytes(const char *pcBuf, const char* cpszChars,
                         int nLen, unsigned char cStop, boolean bTestEqual)
{
   int n;
   unsigned a[8];

   // Initialize bit mask based on the input flag that specifies whether
   // we are looking for a character that matches "any" or "none"
   // of the characters in the search string

   #define ENTRY(c)   a[((c)&7)]   // c's bit lives here
   #define SHIFT(c)   ((c)>>3)     // c's bit is shifted by this much

   if (bTestEqual) {
      std_memset(a, 0, STD_SIZEOF(a));
      do {
         ENTRY(cStop) |= (0x80000000U >> SHIFT(cStop));
         cStop = (unsigned char)*cpszChars++;
      } while (cStop);
   }
   else {
      std_memset(a, 0xFF, STD_SIZEOF(a));

      while (0 != (cStop = (unsigned char)*cpszChars++)) {
         ENTRY(cStop) ^= (0x80000000U >> SHIFT(cStop));
      }
   }


   // Search buffer

   pcBuf += nLen;
   n = -nLen;
   do {
      unsigned char uc = (unsigned char)pcBuf[n];
      // testing for negative after shift is quicker than comparison
      if ( (int)(ENTRY(uc) << SHIFT(uc)) < 0) {
         break;
      }
   } while (++n);

   return (char*)(pcBuf+n);
}


void* std_memchrsend(const void* pBuf, const char* cpszChars, int nLen)
{
   if (nLen <= 0) {
      return (void*)pBuf;
   }
   if ('\0' == *cpszChars) {
      return (char*)pBuf + nLen;
   }

   return x_scanbytes((const char*)pBuf, cpszChars+1, nLen,
                      (unsigned char)*cpszChars, TRUE);
}


char* std_strchrsend(const char* cpszSrch, const char* cpszChars)
{
   return x_scanbytes(cpszSrch, cpszChars, MAX_INT32, '\0', TRUE);
}


char *std_strchrs(const char* cpszSrch, const char* cpszChars)
{
   const char *pc = std_strchrsend(cpszSrch, cpszChars);

   return (*pc ? (char*)pc : 0);
}


char* std_striends(const char* cpsz, const char* cpszSuffix)
{
   int nOffset = std_strlen(cpsz) - std_strlen(cpszSuffix);

   if ((0 <= nOffset) &&
       (0 == std_stricmp(cpsz+nOffset, cpszSuffix))) {

      return (char*)(cpsz+nOffset);
   }

   return 0;
}


char* std_strends(const char* cpsz, const char* cpszSuffix)
{
   int nOffset = std_strlen(cpsz) - std_strlen(cpszSuffix);

   if ((0 <= nOffset) &&
       (0 == std_strcmp(cpsz+nOffset, cpszSuffix))) {

      return (char*)(cpsz + nOffset);
   }

   return 0;
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

char* std_stribegins(const char* cpsz, const char* cpszPrefix)
{
   for (;;) {
      if ('\0' == *cpszPrefix) {
         return (char*)cpsz;
      }

      if (x_casecmp((unsigned char)*cpszPrefix, (unsigned char)*cpsz)) {
         return 0;
      }

      ++cpszPrefix;
      ++cpsz;
   }
   // not reached
}

int std_strcspn(const char* cpszSrch, const char* cpszChars)
{
   const char *pc = x_scanbytes(cpszSrch, cpszChars, MAX_INT32, '\0', TRUE);

   return (pc - cpszSrch);
}

int std_strspn(const char* cpszSrch, const char* cpszChars)
{
   const char *pc = x_scanbytes(cpszSrch, cpszChars, MAX_INT32, '\0', FALSE);

   return (pc - cpszSrch);
}

int std_wstrncmp(const AECHAR* s1, const AECHAR* s2, int nLen)
{
   if (nLen > 0) {
      int i;

      s1 += nLen;
      s2 += nLen;
      i = -nLen;
      do {
         AECHAR c1 = s1[i];
         AECHAR c2 = s2[i];
         int  diff = c1 - c2;

         if (diff) {
            return diff;
         }

         if ('\0' == c1) {
            break;
         }
      } while (++i);
   }

   return 0;
}

int std_wstrcmp(const AECHAR* s1, const AECHAR* s2)
{
   return std_wstrncmp(s1, s2, MAX_INT32);
}

AECHAR* std_wstrchr(const AECHAR* cpwszText, AECHAR ch)
{
   for (; ; cpwszText++) {
      AECHAR chn = *cpwszText;

      if (chn == ch) {
         return (AECHAR *)cpwszText;
      }
      else if ( chn == (AECHAR)0 ) {
         return 0;
      }
   }
}

AECHAR* std_wstrrchr(const AECHAR* cpwszText, AECHAR ch)
{
   const AECHAR* p = 0;

   do {
      if (*cpwszText == ch) {
         p = cpwszText;
      }
   } while (*cpwszText++ != (AECHAR)0);

   return (AECHAR*)p;
}

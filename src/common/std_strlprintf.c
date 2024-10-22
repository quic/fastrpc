// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEstd.h"
#include "AEEBufBound.h"
#include "AEEsmath.h"
#include "AEEStdErr.h"
#include "std_dtoa.h"
//#include "math.h"

//==============================================================================
//   Macro definitions
//==============================================================================

#define  ISDIGIT(c)              ( (c) >= '0' && (c) <= '9')
#define  TOLOWER(c)              ( (c) | 32 )   // works only for letters
#define  FAILED(b)               ( (b) != AEE_SUCCESS ? TRUE : FALSE )
#define  CLEANUP_ON_ERROR(b,l)   if( FAILED( b ) ) { goto l; }
#define  ROUND(d, p)             fp_round( d, p )
#define  FP_POW_10(n)            fp_pow_10(n)

//==============================================================================
//   Type definitions
//==============================================================================


// Formatting flags

#define FF_PLUS     1    // '+'
#define FF_MINUS    2    // '-'
#define FF_POUND    4    // '#'
#define FF_BLANK    8    // ' '
#define FF_ZERO    16    // '0'

typedef struct {

   // Parsed values (from "%..." expression)

   int      flags;          // FF_PLUS, FF_MINUS, etc.
   char     cType;          // d, s, c, x, X, etc.
   int32    nWidth;         // number preceding '.' : controls padding
   int32    nPrecision;     // number following '.'  (-1 if not given)

   // Computed values

   const char *  pszStr;         // string holding prefix + value
   int           nPrefix;        // # of numeric prefix bytes in pszStr[]
   int           nLen;           // length of string (after prefix)
   int           nNumWidth;      // minimum numeric value size (pad with '0')

} FieldFormat;

typedef int (*pfnFormatFloat)(FieldFormat* me, double dNumber, char* pcBuffer);

//==============================================================================
//   Function definitions
//==============================================================================

// Read an unsigned decimal integer
//
static int ScanDecimal(const char **ppsz)
{
   int n = 0;
   const char *psz;

   for (psz = *ppsz; ISDIGIT(*psz); ++psz) {
      n = n*10 + (int) (*psz - '0');
   }
   *ppsz = psz;
   return n;
}


#define FORMATNUMBER_SIZE   24   // octal: 22 + '0' + null ;  decimal: 20 + sign + null


// Convert number to string, setting computed fields in FieldFormat.
//
//  pcBuf[] must have room for at least FORMATNUMBER_SIZE characters
//  return value: length of string.
//
static __inline void
FormatNumber(FieldFormat *me, char pcBuf[FORMATNUMBER_SIZE], uint64 uNum64)
{
   char cType = me->cType;
   const char *cpszDigits;
   char *pc = pcBuf;
   int nBase;
   char *pcRev;

   if (cType == 'p') {
      cType = 'X';
      me->nPrecision = 8;
   }

   if (me->nPrecision >= 0) {
      me->nNumWidth = me->nPrecision;
      // Odd thing: '0' flag is ignored for numbers when precision is
      // specified.
      me->flags &= ~FF_ZERO;
   } else {
      me->nNumWidth = 1;
   }

   // Output prefix

   if (( 'd' == cType || 'i' == cType)) {
      if ((int64)uNum64 < 0) {
         *pc++ = '-';
         uNum64 = (uint64)-(int64)uNum64;
      } else if (me->flags & FF_PLUS) {
         *pc++ = '+';
      } else if (me->flags & FF_BLANK) {
         *pc++ = ' ';
      }
   }

   if ((me->flags & FF_POUND) && 0 != uNum64) {
      if ('x' == TOLOWER(cType)) {
         *pc++ = '0';
         *pc++ = cType;
      } else if ('o' == cType) {
         *pc++ = '0';
         // Odd thing about libc printf: "0" prefix counts as part of minimum
         // width, but "0x" prefix does not.
         --me->nNumWidth;
      }
   }
   me->nPrefix = pc - pcBuf;

   // Output unsigned numeric value

   nBase = ('o' == cType          ? 8 :
            'x' == TOLOWER(cType) ? 16 :
            10);
   cpszDigits = ((cType == 'X') ? "0123456789ABCDEF"
                                : "0123456789abcdef");

   pcRev = pc;

   while (uNum64) {
      *pc++ = cpszDigits[uNum64 % (unsigned)nBase];
      uNum64 /= (unsigned)nBase;
   }

   *pc = '\0';

   me->pszStr = pcBuf;
   me->nLen = pc - pcRev;

   // Reverse string

   --pc;
   for (; pcRev < pc; ++pcRev, --pc) {
      char c = *pc;
      *pc = *pcRev;
      *pcRev = c;
   }
}

//
// This function converts the input floating point number dNumber to an
// ASCII string using either %f or %F formatting. This functions assumes
// that dNumer is a valid floating point number (i.e., dNumber is NOT
// +/-INF or NaN). The size of the output buffer pcBuffer should be at
// least STD_DTOA_FORMAT_FLOAT_SIZE.
//
static int ConvertFloat(FieldFormat* me, double dNumber, char* pcBuffer,
                        int nBufSize)
{
   int nError = AEE_SUCCESS;
   int32 nPrecision = 0;
   int nIndex = 0;
   BufBound OutBuf;
   char szIntegerPart[STD_DTOA_FORMAT_INTEGER_SIZE] = {0};
   char szFractionPart[STD_DTOA_FORMAT_FRACTION_SIZE] = {0};
   int nExponent = 0;
   char cType = TOLOWER(me->cType);

   // Set the precision for conversion
   nPrecision = me->nPrecision;
   if (nPrecision < 0) {
      // No precision was specified, set it to the default value if the
      // format specifier is not %a
      if (cType != 'a') {
         nPrecision = STD_DTOA_DEFAULT_FLOAT_PRECISION;
      }
   }
   else if ((0 == nPrecision) && ('g' == cType)) {
      nPrecision = 1;
   }

   if (cType != 'a') {
      // For %g, check whether to use %e of %f formatting style.
      // Also, set the precision value accordingly since in this case the user
      // specified value is really the number of significant digits.
      // These next few steps should be skipped if the input number is 0.
      if (dNumber != 0.0) {
         nExponent = fp_log_10(dNumber);
         if ('g' == cType) {
            if ((nExponent < -4) || (nExponent >= nPrecision)) {
               cType = 'e';
               nPrecision = nPrecision - 1;
            }
            else {
               cType = 'f';
               nPrecision = nPrecision - nExponent - 1;
            }
         }

         // For %e, convert the number to the form d.ddd
         if ('e' == cType) {
            dNumber = dNumber / FP_POW_10(nExponent);
         }

         // Now, round the number to the specified precision
         dNumber = ROUND(dNumber, nPrecision);

         // For %e, the rounding operation may have resulted in a number dd.ddd
         // Reconvert it to the form d.ddd
         if (('e' == cType) && ((dNumber >= 10.0) || (dNumber <= -10.0))) {
            dNumber = dNumber / 10.0;
            nExponent++;
         }
      }

      // Convert the decmial number to string
      nError = std_dtoa_decimal(dNumber, nPrecision, szIntegerPart, szFractionPart);
      CLEANUP_ON_ERROR(nError, bail);
   }
   else
   {
      // Conver the hex floating point number to string
      nError = std_dtoa_hex(dNumber, nPrecision, me->cType, szIntegerPart,
                            szFractionPart, &nExponent);
      CLEANUP_ON_ERROR(nError, bail);
   }


   //
   // Write the output as per the specified format.
   // First: Check for any prefixes that need to be added to the output.
   // The only possible prefixes are '-', '+' or ' '. The following rules
   // are applicable:
   // 1. One and only one prefix will be applicable at any time.
   // 2. If the number is negative, then '+' and ' ' are not applicable.
   // 3. For positive numbers, the prefix '+' takes precedence over ' '.
   //
   // In addition, we were dealing with a hex floating point number (%a),
   // then we need to write of the 0x prefix.
   //
   BufBound_Init(&OutBuf, pcBuffer, nBufSize);
   if (dNumber < 0.0) {
      // The '-' sign would have already been added to the szIntegerPart by
      // the conversion function.
      me->nPrefix = 1;
   }
   if (dNumber >= 0.0){
      if (me->flags & FF_PLUS) {
         BufBound_Putc(&OutBuf, '+');
         me->nPrefix = 1;
      }
      else if(me->flags & FF_BLANK) {
         BufBound_Putc(&OutBuf, ' ');
         me->nPrefix = 1;
      }
   }

   // For %a, write out the 0x prefix
   if ('a' == cType) {
      BufBound_Putc(&OutBuf, '0');
      BufBound_Putc(&OutBuf, ('a' == me->cType) ? 'x' : 'X');
      me->nPrefix += 2;
   }

   // Second: Write the integer part
   BufBound_Puts(&OutBuf, szIntegerPart);

   // Third: Write the decimal point followed by the fraction part.
   // For %g, we need to truncate the trailing zeros in the fraction.
   // Skip this if the '#' flag is specified
   if (!(me->flags & FF_POUND) && ('g' == TOLOWER(me->cType))) {
      for (nIndex = std_strlen(szFractionPart) - 1;
           (nIndex >= 0) && (szFractionPart[nIndex] == '0'); nIndex--) {
         szFractionPart[nIndex] = '\0';
      }
   }

   // The decimal point is specified only if there are some decimal digits.
   // However, if the '#' format specifier is present then the decimal point
   // will be present.
   if ((me->flags & FF_POUND) || (*szFractionPart != 0)) {
      BufBound_Putc(&OutBuf, '.');

      // Write the fraction part
      BufBound_Puts(&OutBuf, szFractionPart);
   }

   // For %e and %a, write out the exponent
   if (('e' == cType) || ('a' == cType)) {
      char* pcExpStart = NULL;
      char* pcExpEnd = NULL;
      char cTemp = 0;

      if ('a' == me->cType) {
         BufBound_Putc(&OutBuf, 'p');
      }
      else if ('A' == me->cType) {
         BufBound_Putc(&OutBuf, 'P');
      }
      else if (('e' == me->cType) || ('g' == me->cType)) {
         BufBound_Putc(&OutBuf, 'e');
      }
      else {
         BufBound_Putc(&OutBuf, 'E');
      }

      // Write the exponent sign
      if (nExponent < 0) {
         BufBound_Putc(&OutBuf, '-');
         nExponent = -nExponent;
      }
      else {
         BufBound_Putc(&OutBuf, '+');
      }

      // Write out the exponent.
      // For %e, the exponent should at least be two digits.
      // The exponent to be written will be at most 4 digits as any
      // overflow would have been take care of by now.
      if (BufBound_Left(&OutBuf) >= 4) {
         if ('e' == cType) {
            if (nExponent < 10) {
               BufBound_Putc(&OutBuf, '0');
            }
         }

         pcExpStart = OutBuf.pcWrite;
         do {
            BufBound_Putc(&OutBuf, '0' + (nExponent % 10));
            nExponent /= 10;
         } while (nExponent);
         pcExpEnd = OutBuf.pcWrite - 1;

         // Reverse the exponent
         for (; pcExpStart < pcExpEnd; pcExpStart++, pcExpEnd--) {
            cTemp = *pcExpStart;
            *pcExpStart = *pcExpEnd;
            *pcExpEnd = cTemp;
         }
      }
   }

   // Null-terminate the string
   BufBound_ForceNullTerm(&OutBuf);

   // Set the output parameters
   // We do not care if there was enough space in the output buffer or not.
   // The output would be truncated to a maximum length of
   // STD_DTOA_FORMAT_FLOAT_SIZE.
   me->pszStr = OutBuf.pcBuf;
   me->nLen = BufBound_ReallyWrote(&OutBuf) - me->nPrefix - 1;

bail:

   return nError;
}

//
// This is a wrapper function that converts an input floating point number
// to a string based on a given format specifier %e, %f or %g. It first checks
// if the specified number is a valid floating point number before calling
// the function that does the conversion.
//
// The size of the output buffer pcBuffer should be at least STD_DTOA_FORMAT_FLOAT_SIZE.
//
static int FormatFloat(FieldFormat* me, double dNumber,
                       char pcBuffer[STD_DTOA_FORMAT_FLOAT_SIZE])
{
   int nError = AEE_SUCCESS;
   FloatingPointType NumberType = FP_TYPE_UNKOWN;

   // Check for error conditions
   if (NULL == pcBuffer) {
      nError = AEE_EBADPARM;
      goto bail;
   }

   // Initialize the output params first
   me->nLen = 0;
   me->nPrefix = 0;

   // Check for special cases such as NaN and Infinity
   nError = fp_check_special_cases(dNumber, &NumberType);
   CLEANUP_ON_ERROR(nError, bail);

   switch(NumberType) {
	  case FP_TYPE_NEGATIVE_INF:

		 if (('E' == me->cType) || ('F' == me->cType) || ('G' == me->cType)) {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_NEGATIVE_INF_UPPER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }
		 else {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_NEGATIVE_INF_LOWER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }

		 // Don't pad with 0's
		 me->flags &= ~FF_ZERO;

		 break;

	  case FP_TYPE_POSITIVE_INF:

		 if (('E' == me->cType) || ('F' == me->cType) || ('G' == me->cType)) {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_POSITIVE_INF_UPPER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }
		 else {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_POSITIVE_INF_LOWER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }

		 // Don't pad with 0's
		 me->flags &= ~FF_ZERO;

		 break;

	  case FP_TYPE_NAN:

		 if (('E' == me->cType) || ('F' == me->cType) || ('G' == me->cType)) {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_NAN_UPPER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }
		 else
		 {
			me->nLen = std_strlcpy(pcBuffer, STD_DTOA_NAN_LOWER_CASE,
								   STD_DTOA_FORMAT_FLOAT_SIZE);
		 }

		 // Don't pad with 0's
		 me->flags &= ~FF_ZERO;

		 break;

	  case FP_TYPE_GENERAL:

		 nError = ConvertFloat(me, dNumber, pcBuffer,
                               STD_DTOA_FORMAT_FLOAT_SIZE);
		 CLEANUP_ON_ERROR(nError, bail);

		 break;

	  default:

		 // This should only happen if this function has been modified
		 // to support other special cases and this block has not been
		 // updated.
		 nError = AEE_EFAILED;
		 goto bail;
   }

   // Set the output parameters
   me->pszStr = pcBuffer;


bail:

   return nError;
}

static int std_strlprintf_inner(char *pszDest, int nDestSize,
                                const char *cpszFmt, AEEVaList args,
                                pfnFormatFloat pfnFormatFloatFunc)
{
   BufBound bb;
   const char *pcIn = cpszFmt;

   BufBound_Init(&bb, pszDest, nDestSize);

   for (;;) {
      FieldFormat ff;
      const char *pcEsc;
      char achBuf[FORMATNUMBER_SIZE];
      char achBuf2[STD_DTOA_FORMAT_FLOAT_SIZE];
      char cType;
      boolean bLong = 0;

      pcEsc = std_strchrend(pcIn, '%');
      BufBound_Write(&bb, pcIn, pcEsc-pcIn);

      if (0 == *pcEsc) {
         break;
      }
      pcIn = pcEsc+1;

      //----------------------------------------------------
      // Consume "%..." specifiers:
      //
      //   %[FLAGS] [WIDTH] [.PRECISION] [{h | l | I64 | L}]
      //----------------------------------------------------

      std_memset(&ff, 0, sizeof(FieldFormat));
      ff.nPrecision = -1;

      // Consume all flags
      for (;;) {
         int f;

         f = (('+' == *pcIn) ? FF_PLUS  :
              ('-' == *pcIn) ? FF_MINUS :
              ('#' == *pcIn) ? FF_POUND :
              (' ' == *pcIn) ? FF_BLANK :
              ('0' == *pcIn) ? FF_ZERO  : 0);

         if (0 == f) {
            break;
         }

         ff.flags |= f;
         ++pcIn;
      }

      // Consume width
      if ('*' == *pcIn) {
         AEEVA_ARG(args, ff.nWidth, int32);
         pcIn++;
      } else {
         ff.nWidth = ScanDecimal(&pcIn);
      }
      if ((ff.flags & FF_MINUS) && ff.nWidth > 0) {
         ff.nWidth = -ff.nWidth;
      }

      // Consume precision
      if ('.' == *pcIn) {
         pcIn++;
         if ('*' == *pcIn) { // Can be *... (given in int * param)
            AEEVA_ARG(args, ff.nPrecision, int32);
            pcIn++;
         } else {
            ff.nPrecision = ScanDecimal(&pcIn);
         }
      }

      // Consume size designator
      {
         static const struct {
            char    szPre[3];
            boolean b64;
         } a[] = {
            { "l",  0, },
            { "ll", 1, },
            { "L",  1, },
            { "j",  1, },
            { "h",  0, },
            { "hh", 0, },
            { "z",  0 }
         };

         int n = STD_ARRAY_SIZE(a);

         while (--n >= 0) {
            const char *psz = std_strbegins(pcIn, a[n].szPre);
            if ((const char*)0 != psz) {
               pcIn = psz;
               bLong = a[n].b64;
               break;
            }
         }
      }

      //----------------------------------------------------
      //
      // Format output values
      //
      //----------------------------------------------------

      ff.cType = cType = *pcIn++;

      if ('s' == cType) {

         // String
         char *psz;

         AEEVA_ARG(args, psz, char*);
         ff.pszStr = psz;
         ff.nLen = std_strlen(psz);
         if (ff.nPrecision >= 0 && ff.nPrecision < ff.nLen) {
            ff.nLen = ff.nPrecision;
         }

      } else if ('c' == cType) {

         // char
         AEEVA_ARG(args, achBuf[0], int);
         achBuf[1] = '\0';
         ff.pszStr = achBuf;
         ff.nLen = 1;

      } else if ('u' == cType ||
                 'o' == cType ||
                 'd' == cType ||
                 'i' == cType ||
                 'p' == cType ||
                 'x' == TOLOWER(cType) ) {

         // int
         uint64 uArg64;

         if (bLong) {
            AEEVA_ARG(args, uArg64, int64);  // See how much room needed
         } else {
            uint32 uArg32;
            AEEVA_ARG(args, uArg32, int32);  // See how much room needed
            uArg64 = uArg32;
            if ('d' == cType || 'i' == cType) {
               uArg64 = (uint64)(int64)(int32)uArg32;
            }
         }

         FormatNumber(&ff, achBuf, uArg64);

      } else if (pfnFormatFloatFunc &&
                 ('e' == TOLOWER(cType) ||
                  'f' == TOLOWER(cType) ||
                  'g' == TOLOWER(cType) ||
                  'a' == TOLOWER(cType))) {

         // float
            int nError = AEE_SUCCESS;
            double dNumber;

            AEEVA_ARG(args, dNumber, double);
            nError = pfnFormatFloatFunc(&ff, dNumber, achBuf2);
            if (FAILED(nError)) {
               continue;
            }

      } else if ('\0' == cType) {

         // premature end
         break;

      } else {
         // Unknown type
         BufBound_Putc(&bb, cType);
         continue;
      }

      // FieldFormat computed variables + nWidth controls output

      if (ff.flags & FF_ZERO) {
         ff.nNumWidth = ff.nWidth - ff.nPrefix;
      }

      {
         int nLen1 = ff.nLen;
         int nLen2 = STD_MAX(ff.nNumWidth, nLen1) + ff.nPrefix;

         // Putnc() safely ignores negative sizes
         BufBound_Putnc(&bb, ' ', smath_Sub(ff.nWidth,nLen2));
         BufBound_Write(&bb, ff.pszStr, ff.nPrefix);
         BufBound_Putnc(&bb, '0', smath_Sub(ff.nNumWidth, nLen1));
         BufBound_Write(&bb, ff.pszStr+ff.nPrefix, nLen1);
         BufBound_Putnc(&bb, ' ', smath_Sub(-nLen2, ff.nWidth));
      }
   }

   AEEVA_END(args);

   BufBound_ForceNullTerm(&bb);

   /* Return number of bytes required regardless if buffer bound was reached */

   /* Note that we subtract 1 because the NUL byte which was added in
      BufBound_ForceNullTerm() is counted as a written byte; the semantics
      of both the ...printf() functions and the strl...() functions call for
      the NUL byte to be excluded from the count. */

   return BufBound_Wrote(&bb)-1;
}

int std_vstrlprintf(char *pszDest, int nDestSize,
                    const char *cpszFmt,
                    AEEVaList args)
{
   return std_strlprintf_inner(pszDest, nDestSize, cpszFmt, args, NULL);
}

int std_vsnprintf(char *pszDest, int nDestSize,
                  const char *cpszFmt,
                  AEEVaList args)
/*
   Same as std_vstrlprintf with the additional support of floating point
   conversion specifiers - %e, %f, %g and %a
*/
{
   return std_strlprintf_inner(pszDest, nDestSize, cpszFmt, args, FormatFloat);
}

int std_strlprintf(char *pszDest, int nDestSize, const char *pszFmt, ...)
{
   int nRet;
   AEEVaList args;

   AEEVA_START(args, pszFmt);

   nRet = std_vstrlprintf(pszDest, nDestSize, pszFmt, args);

   AEEVA_END(args);

   return nRet;
}

int std_snprintf(char *pszDest, int nDestSize, const char *pszFmt, ...)
/*
   Same as std_strlprintf with the additional support of floating point
   conversion specifiers - %e, %f, %g and %a
*/
{
   int nRet;
   AEEVaList args;

   AEEVA_START(args, pszFmt);

   nRet = std_vsnprintf(pszDest, nDestSize, pszFmt, args);

   AEEVA_END(args);

   return nRet;
}

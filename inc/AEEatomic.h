/**
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AEEATOMIC_H
#define AEEATOMIC_H
/*
=======================================================================

FILE:         AEEatomic.h

SERVICES:     atomic

DESCRIPTION:  Fast Atomic ops

=======================================================================
*/

#include "AEEStdDef.h"

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

uint32_t atomic_Add(uint32_t * volatile puDest, int nAdd);
uint32_t atomic_Exchange(uint32_t * volatile puDest, uint32_t uVal);
uint32_t atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare);
uint32_t atomic_CompareOrAdd(uint32_t * volatile puDest, uint32_t uCompare, int nAdd);

uint64_t atomic_CompareAndExchange64(uint64_t * volatile puDest, uint64_t uExchange, uint64_t uCompare);
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare);
#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

/*=====================================================================
INTERFACE DOCUMENTATION
=======================================================================
atomic Interface

  The atomic interface provides fast "atomic" operations.  The
   operations are defined to be atomic with respect to each other.

=======================================================================

=======================================================================

atomic_Add()

Description:

   Performs an atomic sum operation.

Prototype:

   uint32_t atomic_Add(uint32_t* puDest, int nInc);

Parameters:
   puDest [in|out] : Points to unsigned number to add nInc and save
   nInc : increment

Return Value:
   result.

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================

atomic_Exchange()

Description:

   Atomic exchange of 32bit value. Performs an atomic operation of :
      write uVal to *puDest
      return the previous value in *puDest

Prototype:

   uint32_t atomic_Exchange(uint32_t* puDest, uint32_t uVal);

Parameters:
   puDest [in|out] : Points to unsigned number to be exchanged
   uVal : new value to write.

Return Value:
   previous value at *puDest.

Comments:
   None

Side Effects:
   May cause exception if puDest is not a 32 bit aligned address.

See Also:
   None
=======================================================================

atomic_CompareAndExchange()

Description:

   Performs an atomic operation of :
      if (*puDest == uCompare) {
         *puDest = uExchange;
      }

   returns the previous value in *puDest

Prototype:

   uint32_t atomic_CompareAndExchange(uint32_t *puDest, uint32_t uExchange,
                                    uint32_t uCompare);

Parameters:
   puDest [in|out] : Points to unsigned number.
   uExchange : A new value to write to *puDest
   uCompare : Comparand

Return Value:
   previous value at *puDest.

Comments:
   None

Side Effects:
   May cause exception if puDest is not a 32 bit aligned address.

See Also:
   None

=======================================================================
atomic_CompareOrAdd()

Description:

   Performs an atomic operation of :
      if (*puDest != uCompare) {
         *puDest += nAdd;
      }

   returns the new value in *puDest

Prototype:

   uint32_t atomic_CompareOrAdd(uint32_t *puDest, uint32_t uCompare, int nAdd);

Parameters:
   puDest [in|out] : Points to unsigned number.
   uCompare : Comparand
   nAdd : Add to *puDest

Return Value:
   new value at *puDest.

Comments:
   None

Side Effects:
   May cause exception if puDest is not a 32 bit aligned address.

See Also:
   None
=======================================================================*/

#endif /* #ifndef AEEATOMIC_H */


// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEatomic.h"

uint32 atomic_Add(uint32 * volatile puDest, int nAdd) {
   uint32 previous;
   uint32 current;
   uint32 sum;
   do {
      current = *puDest;
      __builtin_add_overflow(current, nAdd, &sum);
      previous = atomic_CompareAndExchange(puDest, sum, current);
   } while(previous != current);
   __builtin_add_overflow(current, nAdd, &sum);
   return sum;
}

uint32 atomic_Exchange(uint32* volatile puDest, uint32 uVal) {
   uint32 previous;
   uint32 current;
   do {
      current = *puDest;
      previous = atomic_CompareAndExchange(puDest, uVal, current);
   } while(previous != current);
   return previous;
}

uint32 atomic_CompareOrAdd(uint32* volatile puDest, uint32 uCompare, int nAdd) {
   uint32 previous;
   uint32 current;
   uint32 result;
   do {
      current = *puDest;
      previous = current;
      result = current;
      if(current != uCompare) {
         previous = atomic_CompareAndExchange(puDest, current + nAdd, current);
         if(previous == current) {
            result = current + nAdd;
         }
      }
   } while(previous != current);
   return result;
}


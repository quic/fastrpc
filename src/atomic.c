// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEatomic.h"

uint32_t atomic_Add(uint32_t * volatile puDest, int nAdd) {
   uint32_t previous;
   uint32_t current;
   uint32_t sum;
   do {
      current = *puDest;
      __builtin_add_overflow(current, nAdd, &sum);
      previous = atomic_CompareAndExchange(puDest, sum, current);
   } while(previous != current);
   __builtin_add_overflow(current, nAdd, &sum);
   return sum;
}

uint32_t atomic_Exchange(uint32_t* volatile puDest, uint32_t uVal) {
   uint32_t previous;
   uint32_t current;
   do {
      current = *puDest;
      previous = atomic_CompareAndExchange(puDest, uVal, current);
   } while(previous != current);
   return previous;
}

uint32_t atomic_CompareOrAdd(uint32_t* volatile puDest, uint32_t uCompare, int nAdd) {
   uint32_t previous;
   uint32_t current;
   uint32_t result;
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


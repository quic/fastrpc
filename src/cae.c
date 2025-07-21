// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEatomic.h"

#ifdef _WIN32
#include "Windows.h"
uint32_t atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare) {
   C_ASSERT(sizeof(LONG) == sizeof(uint32_t));
   return (uint32_t)InterlockedCompareExchange((LONG*)puDest, (LONG)uExchange, (LONG)uCompare);
}
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare) {
   C_ASSERT(sizeof(uintptr_t) == sizeof(void*));
   return (uintptr_t)InterlockedCompareExchangePointer((void**)puDest, (void*)uExchange, (void*)uCompare);
}
#elif __hexagon__

#ifndef C_ASSERT
#define C_ASSERT(test) \
    switch(0) {\
      case 0:\
      case test:;\
    }
#endif

static inline unsigned int
qurt_atomic_compare_val_and_set(unsigned int* target,
                                unsigned int old_val,
                                unsigned int new_val)
{
   unsigned int current_val;

   __asm__ __volatile__(
       "1:     %0 = memw_locked(%2)\n"
       "       p0 = cmp.eq(%0, %3)\n"
       "       if !p0 jump 2f\n"
       "       memw_locked(%2, p0) = %4\n"
       "       if !p0 jump 1b\n"
       "2:\n"
       : "=&r" (current_val),"+m" (*target)
       : "r" (target), "r" (old_val), "r" (new_val)
       : "p0");

   return current_val;
}
uint32_t atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare) {
   return (uint32_t)qurt_atomic_compare_val_and_set((unsigned int*)puDest, uCompare, uExchange);
}
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare) {
   C_ASSERT(sizeof(uintptr_t) == sizeof(uint32_t));
   return (uint32_t)atomic_CompareAndExchange((uint32_t*)puDest, (uint32_t)uExchange, (uint32_t)uCompare);
}
#elif __GNUC__
uint32_t atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
uint64_t atomic_CompareAndExchange64(uint64_t * volatile puDest, uint64_t uExchange, uint64_t uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
#endif //compare and exchange

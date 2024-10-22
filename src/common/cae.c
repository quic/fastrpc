// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEatomic.h"

#ifdef _WIN32
#include "Windows.h"
uint32 atomic_CompareAndExchange(uint32 * volatile puDest, uint32 uExchange, uint32 uCompare) {
   C_ASSERT(sizeof(LONG) == sizeof(uint32));
   return (uint32)InterlockedCompareExchange((LONG*)puDest, (LONG)uExchange, (LONG)uCompare);
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
uint32 atomic_CompareAndExchange(uint32 * volatile puDest, uint32 uExchange, uint32 uCompare) {
   return (uint32)qurt_atomic_compare_val_and_set((unsigned int*)puDest, uCompare, uExchange);
}
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare) {
   C_ASSERT(sizeof(uintptr_t) == sizeof(uint32));
   return (uint32)atomic_CompareAndExchange((uint32*)puDest, (uint32)uExchange, (uint32)uCompare);
}
#elif __GNUC__
uint32 atomic_CompareAndExchange(uint32 * volatile puDest, uint32 uExchange, uint32 uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
uint64 atomic_CompareAndExchange64(uint64 * volatile puDest, uint64 uExchange, uint64 uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
uintptr_t atomic_CompareAndExchangeUP(uintptr_t * volatile puDest, uintptr_t uExchange, uintptr_t uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
#endif //compare and exchange

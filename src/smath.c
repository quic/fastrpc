// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEStdDef.h"
#include "AEEsmath.h"


static int32_t ToInt(int64_t a)
{
   return (a > INT32_MAX ? INT32_MAX :
           a < INT32_MIN ? INT32_MIN :
           (int32_t)a);
}

int smath_Add(int a, int b)
{
   return ToInt((int64_t)a + (int64_t)b);
}

int smath_Sub(int a, int b)
{
   return ToInt((int64_t)a - (int64_t)b);
}

int smath_Mul(int a, int b)
{
   return ToInt((int64_t)a * (int64_t)b);
}

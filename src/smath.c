// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "AEEStdDef.h"
#include "AEEsmath.h"


static int32 ToInt(int64 a)
{
   return (a > MAX_INT32 ? MAX_INT32 :
           a < MIN_INT32 ? MIN_INT32 :
           (int32)a);
}

int smath_Add(int a, int b)
{
   return ToInt((int64)a + (int64)b);
}

int smath_Sub(int a, int b)
{
   return ToInt((int64)a - (int64)b);
}

int smath_Mul(int a, int b)
{
   return ToInt((int64)a * (int64)b);
}

/*
 * vkrunner
 *
 * Copyright (C) 2018, 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "vr-small-float.h"

double
vr_small_float_load_unsigned(uint32_t part,
                             int e_bits,
                             int m_bits)
{
        int e_max = UINT32_MAX >> (32 - e_bits);
        int e = (part >> m_bits) & e_max;
        int m = part & (UINT32_MAX >> (32 - m_bits));

        if (e == e_max)
                return m == 0 ? INFINITY : NAN;

        if (e == 0)
                e = 1;
        else
                m += 1 << m_bits;

        return ldexp(m / (double) (1 << m_bits), e - (e_max >> 1));
}

double
vr_small_float_load_signed(uint32_t part,
                           int e_bits,
                           int m_bits)
{
        double res = vr_small_float_load_unsigned(part, e_bits, m_bits);

        if (res != NAN && (part & (1 << (e_bits + m_bits))))
                res = -res;

        return res;
}


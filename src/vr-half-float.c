/*
 * Copyright (c) The Piglit project 2007
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "vr-half-float.h"

union fi_type {
        float f;
        int32_t i;
};

/**
 * Convert a 4-byte float to a 2-byte half float.
 * Based on code from:
 * http://www.opengl.org/discussion_boards/ubb/Forum3/HTML/008786.html
 *
 * Taken over from Piglit which took it from Mesa.
 */
uint16_t
vr_half_float_from_float(float val)
{
        const union fi_type fi = {val};
        const int flt_m = fi.i & 0x7fffff;
        const int flt_e = (fi.i >> 23) & 0xff;
        const int flt_s = (fi.i >> 31) & 0x1;
        int s, e, m = 0;
        uint16_t result;

        /* sign bit */
        s = flt_s;

        /* handle special cases */
        if ((flt_e == 0) && (flt_m == 0)) {
                /* zero */
                /* m = 0; - already set */
                e = 0;
        }
        else if ((flt_e == 0) && (flt_m != 0)) {
                /* denorm -- denorm float maps to 0 half */
                /* m = 0; - already set */
                e = 0;
        }
        else if ((flt_e == 0xff) && (flt_m == 0)) {
                /* infinity */
                /* m = 0; - already set */
                e = 31;
        }
        else if ((flt_e == 0xff) && (flt_m != 0)) {
                /* NaN */
                m = 1;
                e = 31;
        }
        else {
                /* regular number */
                const int new_exp = flt_e - 127;
                if (new_exp < -24) {
                        /* this maps to 0 */
                        /* m = 0; - already set */
                        e = 0;
                }
                else if (new_exp < -14) {
                        /* this maps to a denorm */
                        /* 2^-exp_val*/
                        unsigned int exp_val = (unsigned int) (-14 - new_exp);

                        e = 0;
                        switch (exp_val) {
                        case 0:
                                /* m = 0; - already set */
                                break;
                        case 1: m = 512 + (flt_m >> 14); break;
                        case 2: m = 256 + (flt_m >> 15); break;
                        case 3: m = 128 + (flt_m >> 16); break;
                        case 4: m = 64 + (flt_m >> 17); break;
                        case 5: m = 32 + (flt_m >> 18); break;
                        case 6: m = 16 + (flt_m >> 19); break;
                        case 7: m = 8 + (flt_m >> 20); break;
                        case 8: m = 4 + (flt_m >> 21); break;
                        case 9: m = 2 + (flt_m >> 22); break;
                        case 10: m = 1; break;
                        }
                }
                else if (new_exp > 15) {
                        /* map this value to infinity */
                        /* m = 0; - already set */
                        e = 31;
                }
                else {
                        /* regular */
                        e = new_exp + 15;
                        m = flt_m >> 13;
                }
        }

        result = (s << 15) | (e << 10) | m;
        return result;
}

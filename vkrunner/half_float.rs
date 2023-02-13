// Copyright (c) The Piglit project 2007
// Copyright 2023 Neil Roberts
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// on the rights to use, copy, modify, merge, publish, distribute, sub
// license, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
// VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

use crate::small_float;

// Convert a 4-byte float to a 2-byte half float.
// Based on code from:
// http://www.opengl.org/discussion_boards/ubb/Forum3/HTML/008786.html
//
// Taken over from Piglit which took it from Mesa.
pub fn from_f32(val: f32) -> u16 {
    let fi = val.to_bits();
    let flt_m = fi & 0x7fffff;
    let flt_e = (fi >> 23) & 0xff;
    // sign bit
    let flt_s = (fi >> 31) & 0x1;

    let e;
    let m;

    // handle special cases
    if flt_e == 0 && flt_m == 0 {
        // zero
        m = 0;
        e = 0;
    } else if (flt_e == 0) && (flt_m != 0) {
        // denorm -- denorm float maps to 0 half
        m = 0;
        e = 0;
    } else if (flt_e == 0xff) && (flt_m == 0) {
        // infinity
        m = 0;
        e = 31;
    } else if (flt_e == 0xff) && (flt_m != 0) {
        // NaN
        m = 1;
        e = 31;
    } else {
        // regular number
        let new_exp = flt_e as i32 - 127;
        if new_exp < -24 {
            // this maps to 0
            m = 0;
            e = 0;
        } else if new_exp < -14 {
            // this maps to a denorm
            // 2^-exp_val
            let exp_val = (-14 - new_exp) as u32;

            e = 0;

            match exp_val {
                0 => m = 0,
                1 => m = 512 + (flt_m >> 14),
                2 => m = 256 + (flt_m >> 15),
                3 => m = 128 + (flt_m >> 16),
                4 => m = 64 + (flt_m >> 17),
                5 => m = 32 + (flt_m >> 18),
                6 => m = 16 + (flt_m >> 19),
                7 => m = 8 + (flt_m >> 20),
                8 => m = 4 + (flt_m >> 21),
                9 => m = 2 + (flt_m >> 22),
                10 => m = 1,
                _ => unreachable!(),
            }
        } else if new_exp > 15 {
            // map this value to infinity
            m = 0;
            e = 31;
        } else {
            /* regular */
            e = (new_exp + 15) as u32;
            m = flt_m >> 13;
        }
    }

    ((flt_s << 15) | (e << 10) | m) as u16
}

pub fn to_f64(half: u16) -> f64 {
    small_float::load_signed(half as u32, 5, 10)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_from_f32() {
        assert_eq!(from_f32(0.0), 0);
        assert_eq!(from_f32(1.0 / 3.0), 0x3555);
        assert_eq!(from_f32(-1.0 / 3.0), 0xb555);
        assert_eq!(from_f32(f32::INFINITY), 0x7c00);
        assert_eq!(from_f32(-f32::INFINITY), 0xfc00);
        assert_eq!(from_f32(f32::NAN), 0x7c01);
        assert_eq!(from_f32(f32::MAX), 0x7c00);
    }

    fn assert_float_equal(a: f64, b: f64) {
        assert!(
            (a - b).abs() < 0.001,
            "a={}, b={}",
            a,
            b
        );
    }

    #[test]
    fn test_to_f64() {
        assert_eq!(to_f64(0x7c00), f64::INFINITY);
        assert_eq!(to_f64(0xfc00), -f64::INFINITY);
        assert_float_equal(to_f64(0x3555), 1.0 / 3.0);
    }
}

// vkrunner
//
// Copyright (C) 2018, 2019 Intel Corporation
// Copyright 2023 Neil Roberts
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

pub fn load_unsigned(part: u32, e_bits: u32, m_bits: u32) -> f64 {
    let e_max = u32::MAX >> (32 - e_bits);
    let mut e = ((part >> m_bits) & e_max) as i32;
    let mut m = part & (u32::MAX >> (32 - m_bits));

    if e == e_max as i32 {
        if m == 0 {
            f64::INFINITY
        } else {
            f64::NAN
        }
    } else {
        if e == 0 {
            e = 1;
        } else {
            m += 1 << m_bits;
        }

        m as f64 / (1 << m_bits) as f64
            * ((e - (e_max >> 1) as i32) as f64).exp2()
    }
}

pub fn load_signed(part: u32, e_bits: u32, m_bits: u32) -> f64 {
    let res = load_unsigned(part, e_bits, m_bits);

    if res != f64::NAN && (part & (1 << (e_bits + m_bits))) != 0 {
        -res
    } else {
        res
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn assert_float_equal(a: f64, b: f64) {
        assert!(
            (a - b).abs() < 0.001,
            "a={}, b={}",
            a,
            b
        );
    }

    #[test]
    fn test_load_unsigned() {
        assert_eq!(load_unsigned(0x30, 2, 4), f64::INFINITY);
        assert!(load_unsigned(0x3f, 2, 4).is_nan());

        assert_float_equal(load_unsigned(0, 3, 4), 0.0);
        assert_float_equal(load_unsigned(0x3555, 5, 10), 1.0 / 3.0);
    }

    #[test]
    fn test_load_signed() {
        assert_eq!(load_signed(0x70, 2, 4), -f64::INFINITY);
        assert!(load_signed(0x7f, 2, 4).is_nan());

        assert_float_equal(load_signed(0, 3, 4), 0.0);
        assert_float_equal(load_signed(0x3555, 5, 10), 1.0 / 3.0);
        assert_float_equal(load_signed(0xb555, 5, 10), -1.0 / 3.0);
    }
}

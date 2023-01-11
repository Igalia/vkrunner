// vkrunner
//
// Copyright (C) 2018 Google LLC
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

use std::ffi;

#[derive(Debug, Clone)]
#[repr(C)]
pub struct Tolerance {
    value: [f64; 4],
    is_percent: bool,
}

impl Tolerance {
    pub fn new(value: [f64; 4], is_percent: bool) -> Tolerance {
        Tolerance { value, is_percent }
    }

    pub fn equal(&self, component: usize, a: f64, b: f64) -> bool {
        if self.is_percent {
            (a - b).abs() <= (self.value[component] / 100.0 * b).abs()
        } else {
            (a - b).abs() <= self.value[component]
        }
    }
}

impl Default for Tolerance {
    fn default() -> Tolerance {
        Tolerance {
            value: [0.01; 4],
            is_percent: false,
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_tolerance_equal(
    tolerance: &Tolerance,
    component: ffi::c_int,
    a: f64,
    b: f64
) -> u8 {
    tolerance.equal(component as usize, a, b) as u8
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_percentage() {
        let tolerance = Tolerance::new([25.0, 50.0, 1.0, 1.0], true);

        assert!(tolerance.equal(0, 0.76, 1.0));
        assert!(!tolerance.equal(0, 0.74, 1.0));
        assert!(tolerance.equal(1, 41.0, 80.0));
        assert!(!tolerance.equal(1, 39.0, 1.0));
        assert!(tolerance.equal(2, 100.5, 100.0));
        assert!(!tolerance.equal(2, 101.5, 100.0));
    }

    #[test]
    fn test_direct() {
        let tolerance = Tolerance::new([1.0, 2.0, 3.0, 4.0], false);

        assert!(tolerance.equal(0, 5.9, 5.0));
        assert!(!tolerance.equal(0, 6.1, 5.0));
        assert!(tolerance.equal(1, 3.1, 5.0));
        assert!(!tolerance.equal(1, 2.9, 5.0));
        assert!(tolerance.equal(3, 186.1, 190.0));
        assert!(!tolerance.equal(3, 185.9, 190.0));
    }
}

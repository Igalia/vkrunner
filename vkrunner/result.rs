// vkrunner
//
// Copyright (C) 2018 Intel Corporation
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

use std::fmt;
use std::ffi::c_char;

/// Enum representing the possible results of a test.
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[repr(C)]
pub enum Result {
    Pass,
    Fail,
    Skip,
}

impl Result {
    /// Merge this result with another one. If either test is skipped
    /// then the value of the other result is returned. Otherwise if
    /// either of the tests failed then the global result is a
    /// failure. Finally if both tests passed then the global result
    /// is a pass.
    pub fn merge(self, other: Result) -> Result {
        match self {
            Result::Pass => {
                if other == Result::Skip {
                    self
                } else {
                    other
                }
            },
            Result::Fail => Result::Fail,
            Result::Skip => other,
        }
    }

    // Returns the name with a NULL terminator. This is needed because
    // there is a function in the public API which returns a static
    // string to C code.
    fn name_with_terminator(self) -> &'static str {
        match self {
            Result::Fail => "fail\0",
            Result::Skip => "skip\0",
            Result::Pass => "pass\0",
        }
    }

    /// Return either `"fail"`, `"skip"` or `"pass"` to describe the
    /// result. The result is a static string. You can also use the
    /// [to_string](ToString::to_string) method to get an owned string
    /// because [Result] implements [Display](std::fmt::Display).
    pub fn name(self) -> &'static str {
        let name = self.name_with_terminator();
        &name[0..name.len() - 1]
    }
}

impl fmt::Display for Result {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.name())
    }
}

/// These two wrappers are permanent because they form part of the
/// public API.

#[no_mangle]
pub extern "C" fn vr_result_merge(a: Result, b: Result) -> Result {
    a.merge(b)
}

#[no_mangle]
pub extern "C" fn vr_result_to_string(res: Result) -> *const c_char {
    res.name_with_terminator().as_ptr().cast()
}

#[cfg(test)]
mod test {
    use super::*;
    use std::ffi::CStr;

    #[test]
    fn test_merge() {
        assert_eq!(Result::Fail.merge(Result::Fail), Result::Fail);
        assert_eq!(Result::Fail.merge(Result::Skip), Result::Fail);
        assert_eq!(Result::Fail.merge(Result::Pass), Result::Fail);
        assert_eq!(Result::Skip.merge(Result::Fail), Result::Fail);
        assert_eq!(Result::Skip.merge(Result::Skip), Result::Skip);
        assert_eq!(Result::Skip.merge(Result::Pass), Result::Pass);
        assert_eq!(Result::Pass.merge(Result::Fail), Result::Fail);
        assert_eq!(Result::Pass.merge(Result::Skip), Result::Pass);
        assert_eq!(Result::Pass.merge(Result::Pass), Result::Pass);
    }

    #[test]
    fn test_name() {
        assert_eq!(Result::Fail.name(), "fail");
        assert_eq!(Result::Skip.name(), "skip");
        assert_eq!(Result::Pass.name(), "pass");

        for res in [Result::Fail, Result::Skip, Result::Pass] {
            assert_eq!(&res.to_string(), res.name());

            // SAFETY: We know the pointer is null terminated and has
            // a static lifetime.
            let c_name = unsafe {
                CStr::from_ptr(vr_result_to_string(res).cast())
            };
            assert_eq!(res.name(), c_name.to_str().unwrap());
        }
    }
}

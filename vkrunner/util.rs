// vkrunner
//
// Copyright 2013, 2014, 2023 Neil Roberts
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

use std::env;
use std::ffi::{CStr, c_char, OsStr};

/// Align a value, only works on power-of-two alignments
#[inline]
pub const fn align(value: usize, alignment: usize) -> usize {
    debug_assert!(alignment.is_power_of_two());
    debug_assert!(alignment > 0);

    (value + alignment - 1) & !(alignment - 1)
}

/// Reads an environment variable and interprets its value as a boolean.
///
/// Recognizes 0/false/no and 1/true/yes. Other values result in the
/// default value.
pub fn env_var_as_boolean<K: AsRef<OsStr>>(
    var_name: K,
    default_value: bool,
) -> bool {
    match env::var(var_name) {
        Ok(value) => match value.as_str() {
            "1" | "true" | "yes" => true,
            "0" | "false" | "no" => false,
            _ => default_value,
        },
        Err(_) => default_value,
    }
}

#[no_mangle]
pub extern "C" fn vr_env_var_as_boolean(
    var_name: *const c_char,
    default_value: bool
) -> bool {
    let c_str = unsafe { CStr::from_ptr(var_name) };

    match c_str.to_str() {
        Ok(s) => env_var_as_boolean(s, default_value),
        Err(_) => default_value,
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_align() {
        assert_eq!(align(3, 4), 4);
        assert_eq!(align(4, 4), 4);
        assert_eq!(align(0, 8), 0);
        assert_eq!(align(9, 8), 16);
    }

    fn test_env_var_value<V: AsRef<OsStr>>(
        value: V,
        default_value: bool,
        expected_result: bool
    ) {
        const TEST_VAR: &'static str = "VKRUNNER_TEST_ENV_VAR";
        env::set_var(TEST_VAR, value);
        assert_eq!(
            env_var_as_boolean(TEST_VAR, default_value),
            expected_result
        );
    }

    #[test]
    fn test_env_var_as_boolean() {
        test_env_var_value("1", false, true);
        test_env_var_value("true", false, true);
        test_env_var_value("yes", false, true);
        test_env_var_value("0", true, false);
        test_env_var_value("false", true, false);
        test_env_var_value("no", true, false);

        assert_eq!(
            env_var_as_boolean("ENVIRONMENT_VARIABLE_THAT_DOESNT_EXIST", true),
            true,
        );
        assert_eq!(
            env_var_as_boolean("ENVIRONMENT_VARIABLE_THAT_DOESNT_EXIST", false),
            false,
        );

        test_env_var_value("other_value", false, false);
        test_env_var_value("other_value", true, true);

        // Test using a byte sequence that isn’t valid UTF-8. I think
        // this can’t happen on Windows.
        #[cfg(unix)]
        {
            use std::os::unix::ffi::OsStrExt;

            test_env_var_value(
                OsStr::from_bytes(b"Echant\xe9 in Latin-1"),
                false,
                false,
            );
            test_env_var_value(
                OsStr::from_bytes(b"Echant\xe9 in Latin-1"),
                true,
                true,
            );
        }
    }
}

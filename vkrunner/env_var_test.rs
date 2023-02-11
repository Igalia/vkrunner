// vkrunner
//
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

//! Helper to serialise unit tests that set environment variables so
//! that there won’t be a race condition if the tests are run in
//! parallel.

use std::sync::{Mutex, MutexGuard};
use std::collections::HashMap;
use std::env;
use std::ffi::{OsString, OsStr};

// Mutex to make sure only one test is testing environment variables at a time
static LOCK: Mutex<()> = Mutex::new(());

/// Struct to help make unit tests that set environment variables.
/// Only one EnvValLock can exist in the process at any one time. If a
/// thread tries to construct a second one it will block until the
/// first one is dropped. The environment variables will be restored
/// when the EnvVarLock is dropped.
pub struct EnvVarLock {
    old_values: HashMap<&'static str, Option<OsString>>,
    _mutex_lock: MutexGuard<'static, ()>,
}

impl EnvVarLock {
    /// Construct a new EnvVarLock and set the environment variables
    /// from the hash table. This will block if another EnvVarLock
    /// already exists. When the object is dropped the environment
    /// variables will be restored to their original values.
    ///
    /// Note that the object has no useful methods or members but you
    /// need to keep it alive in order to hold the mutex lock. One way
    /// to do this is to put the lock in a local variable prefixed
    /// with `_`:
    ///
    /// ```
    /// let _env_var_lock = EnvVarLock::new(&[
    ///     ("MY_ENVIRONMENT_VARIABLE", "true")
    /// ]);
    /// assert_eq!(std::env::var("MY_ENVIRONMENT_VARIABLE").unwrap(), "true");
    /// ```
    pub fn new<V: AsRef<OsStr>>(
        variables: &[(&'static str, V)]
    ) -> EnvVarLock {
        let mutex_lock = LOCK.lock().unwrap();

        let old_values: HashMap<&'static str, Option<OsString>> = variables
            .iter()
            .map(|(variable, value)| {
                let old_value = env::var_os(variable);
                env::set_var(*variable, value);
                (*variable, old_value)
            })
            .collect();

        EnvVarLock { old_values, _mutex_lock: mutex_lock }
    }
}

impl Drop for EnvVarLock {
    fn drop(&mut self) {
        for (variable, value) in self.old_values.iter() {
            match value {
                Some(value) => env::set_var(variable, value),
                None => env::remove_var(variable),
            }
        }
    }
}

#[test]
fn env_var_lock_test() {
    {
        let _env_var_lock = EnvVarLock::new(&[
            ("ENV_VAR_LOCK_TEST_ENVIRONMENT_VARIABLE", "true")
        ]);
        assert_eq!(
            std::env::var("ENV_VAR_LOCK_TEST_ENVIRONMENT_VARIABLE").unwrap(),
            "true"
        );
    }

    assert!(matches!(
        std::env::var("ENV_VAR_LOCK_TEST_ENVIRONMENT_VARIABLE"),
        Err(std::env::VarError::NotPresent),
    ));
}

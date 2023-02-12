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

use crate::logger::{self, Logger};
use crate::inspect;
use std::ffi::{c_void, c_int};
use std::ptr;
use std::cell::{Cell, RefCell};
use std::rc::Rc;

#[repr(C)]
pub struct Config {
    pub show_disassembly: bool,
    pub device_id: i32,

    error_cb: Option<logger::WriteCallback>,
    inspect_cb: Option<inspect::Callback>,
    user_data: *mut c_void,

    logger: Cell<Option<Rc<RefCell<Logger>>>>,
}

impl Config {
    /// Get a logger that will write to the current `error_cb` of the
    /// `Config`. The logger will be shared between calls to this
    /// until the callback is changed.
    pub fn logger(&self) -> Rc<RefCell<Logger>> {
        let logger = self.logger.take().unwrap_or_else(|| {
            Rc::new(RefCell::new(Logger::new(self.error_cb, self.user_data)))
        });

        self.logger.set(Some(Rc::clone(&logger)));

        logger
    }

    pub fn inspector(&self) -> Option<inspect::Inspector> {
        self.inspect_cb.map(|cb| inspect::Inspector::new(cb, self.user_data))
    }

    fn reset_logger(&self) {
        // Reset the logger back to None so that it will be
        // reconstructed the next time it is requested.
        self.logger.take();
    }
}

#[no_mangle]
pub extern "C" fn vr_config_new() -> *mut Config {
    Box::into_raw(Box::new(Config {
        show_disassembly: false,
        device_id: -1,
        error_cb: None,
        inspect_cb: None,
        user_data: ptr::null_mut(),
        logger: Cell::new(None),
    }))
}

#[no_mangle]
pub extern "C" fn vr_config_free(config: *mut Config)
{
    unsafe { Box::from_raw(config) };
}

#[no_mangle]
pub extern "C" fn vr_config_set_show_disassembly(
    config: &mut Config,
    show_disassembly: bool,
) {
  config.show_disassembly = show_disassembly;
}

#[no_mangle]
pub extern "C" fn vr_config_set_user_data(
    config: &mut Config,
    user_data: *mut c_void,
) {
    config.user_data = user_data;
    config.reset_logger();
}

#[no_mangle]
pub extern "C" fn vr_config_set_error_cb(
    config: &mut Config,
    error_cb: Option<logger::WriteCallback>,
) {
    config.error_cb = error_cb;
    config.reset_logger();
}

#[no_mangle]
pub extern "C" fn vr_config_set_inspect_cb(
    config: &mut Config,
    inspect_cb: Option<inspect::Callback>,
) {
    config.inspect_cb = inspect_cb;
}

#[no_mangle]
pub extern "C" fn vr_config_set_device_id(
    config: &mut Config,
    device_id: c_int,
) {
    config.device_id = device_id;
}

#[cfg(test)]
mod test {
    use super::*;
    use std::ffi::c_char;
    use std::fmt::Write;

    #[test]
    fn logger() {
        extern "C" fn logger_cb(
            _message: *const c_char,
            user_data: *mut c_void
        ) {
            let flag: *mut bool = user_data.cast();

            unsafe { *flag = true };
        }

        let mut flag = false;

        let config_ptr = vr_config_new();
        let config = unsafe { &mut *config_ptr };

        vr_config_set_error_cb(config, Some(logger_cb));
        vr_config_set_user_data(config, ptr::addr_of_mut!(flag).cast());

        let logger = config.logger();

        logger.borrow_mut().write_str("test\n").unwrap();

        assert!(flag);

        assert!(Rc::ptr_eq(&logger, &config.logger()));

        vr_config_set_error_cb(&mut *config, None);
        // When the callback or user_data changes a new logger should
        // be created
        assert!(!Rc::ptr_eq(&logger, &config.logger()));

        vr_config_free(config_ptr);
    }
}

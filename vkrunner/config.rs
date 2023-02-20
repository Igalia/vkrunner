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
use std::fmt;

pub type ErrorCallback = logger::WriteCallback;

pub struct Config {
    show_disassembly: bool,
    device_id: Option<usize>,

    error_cb: Option<logger::WriteCallback>,
    inspect_cb: Option<inspect::Callback>,
    user_data: *mut c_void,

    logger: Cell<Option<Rc<RefCell<Logger>>>>,
}

impl Config {
    pub fn new() -> Config {
        Config {
            show_disassembly: false,
            device_id: None,
            error_cb: None,
            inspect_cb: None,
            user_data: ptr::null_mut(),
            logger: Cell::new(None),
        }
    }

    /// Sets whether the SPIR-V disassembly of the shaders should be
    /// shown when a script is run. The disassembly will be shown on
    /// the standard out or it will be passed to the `error_cb` if one
    /// has been set. The disassembly is generated with the
    /// `spirv-dis` program which needs to be found in the path or it
    /// can be specified with the `PIGLIT_SPIRV_DIS_BINARY`
    /// environment variable.
    pub fn set_show_disassembly(&mut self, show_disassembly: bool) {
        self.show_disassembly = show_disassembly;
    }

    /// Sets or removes a callback that will receive error messages
    /// generated during the script execution. The callback will be
    /// invoked one line at a time without the trailing newline
    /// terminator. If no callback is specified then the output will
    /// be printed on the standard output instead. The callback can
    /// later be removed by passing `None`.
    pub fn set_error_cb(&mut self, error_cb: Option<ErrorCallback>) {
        self.error_cb = error_cb;
        self.reset_logger();
    }

    /// Sets or removes an inspection callback. The callback will be
    /// invoked after executing a script so that the application can
    /// have a chance to examine the framebuffer and any storage or
    /// uniform buffers created by the script.
    pub fn set_inspect_cb(&mut self, inspect_cb: Option<inspect::Callback>) {
        self.inspect_cb = inspect_cb;
    }

    /// Sets a pointer that will be passed to the `error_cb` and the
    /// `inspect_cb`.
    pub fn set_user_data(&mut self, user_data: *mut c_void) {
        self.user_data = user_data;
        self.reset_logger();
    }

    /// Sets or removes a device number to pick out of the list of
    /// `vkPhysicalDevice`s returned by `vkEnumeratePhysicalDevices`.
    /// The device will still be checked to see if it is compatible
    /// with the script and the test will report Skip if not. If no
    /// device ID is set then VkRunner will pick the first one that is
    /// compatible with the script.
    pub fn set_device_id(&mut self, device_id: Option<usize>) {
        self.device_id = device_id;
    }

    /// Get a logger that will write to the current `error_cb` of the
    /// `Config`. The logger will be shared between calls to this
    /// until the callback is changed.
    pub(crate) fn logger(&self) -> Rc<RefCell<Logger>> {
        let logger = self.logger.take().unwrap_or_else(|| {
            Rc::new(RefCell::new(Logger::new(self.error_cb, self.user_data)))
        });

        self.logger.set(Some(Rc::clone(&logger)));

        logger
    }

    pub(crate) fn inspector(&self) -> Option<inspect::Inspector> {
        self.inspect_cb.map(|cb| inspect::Inspector::new(cb, self.user_data))
    }

    pub(crate) fn show_disassembly(&self) -> bool {
        self.show_disassembly
    }

    pub(crate) fn device_id(&self) -> Option<usize> {
        self.device_id
    }

    fn reset_logger(&self) {
        // Reset the logger back to None so that it will be
        // reconstructed the next time it is requested.
        self.logger.take();
    }
}

#[no_mangle]
pub extern "C" fn vr_config_new() -> *const RefCell<Config> {
    Rc::into_raw(Rc::new(RefCell::new(Config::new())))
}

#[no_mangle]
pub extern "C" fn vr_config_free(config: *const RefCell<Config>)
{
    unsafe { Rc::from_raw(config) };
}

#[no_mangle]
pub extern "C" fn vr_config_set_show_disassembly(
    config: &RefCell<Config>,
    show_disassembly: bool,
) {
    config.borrow_mut().set_show_disassembly(show_disassembly);
}

#[no_mangle]
pub extern "C" fn vr_config_set_user_data(
    config: &RefCell<Config>,
    user_data: *mut c_void,
) {
    config.borrow_mut().set_user_data(user_data);
}

#[no_mangle]
pub extern "C" fn vr_config_set_error_cb(
    config: &RefCell<Config>,
    error_cb: Option<ErrorCallback>,
) {
    config.borrow_mut().set_error_cb(error_cb);
}

#[no_mangle]
pub extern "C" fn vr_config_set_inspect_cb(
    config: &RefCell<Config>,
    inspect_cb: Option<inspect::Callback>,
) {
    config.borrow_mut().set_inspect_cb(inspect_cb);
}

#[no_mangle]
pub extern "C" fn vr_config_set_device_id(
    config: &RefCell<Config>,
    device_id: c_int,
) {
    config.borrow_mut().set_device_id(if device_id < 0 {
        None
    } else {
        Some(device_id as usize)
    });
}

// Need to implement this manually because the derive macro can’t
// handle Cells
impl fmt::Debug for Config {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Config")
            .field("show_disassembly", &self.show_disassembly)
            .field("device_id", &self.device_id)
            .field("user_data", &self.user_data)
            .finish()
    }
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
        let config = unsafe { &*config_ptr };

        vr_config_set_error_cb(config, Some(logger_cb));
        vr_config_set_user_data(config, ptr::addr_of_mut!(flag).cast());

        let logger = config.borrow().logger();

        logger.borrow_mut().write_str("test\n").unwrap();

        assert!(flag);

        assert!(Rc::ptr_eq(&logger, &config.borrow().logger()));

        vr_config_set_error_cb(config, None);
        // When the callback or user_data changes a new logger should
        // be created
        assert!(!Rc::ptr_eq(&logger, &config.borrow().logger()));

        vr_config_free(config_ptr);
    }
}

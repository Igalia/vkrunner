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

use crate::logger;
use crate::inspect;
use std::ffi::{c_void, c_int};
use std::ptr;

#[repr(C)]
pub struct Config {
    pub show_disassembly: bool,
    pub device_id: i32,

    pub error_cb: Option<logger::WriteCallback>,
    pub inspect_cb: Option<inspect::Callback>,
    pub user_data: *mut c_void,
}

#[no_mangle]
pub extern "C" fn vr_config_new() -> *mut Config {
    Box::into_raw(Box::new(Config {
        show_disassembly: false,
        device_id: -1,
        error_cb: None,
        inspect_cb: None,
        user_data: ptr::null_mut(),
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
}

#[no_mangle]
pub extern "C" fn vr_config_set_error_cb(
    config: &mut Config,
    error_cb: Option<logger::WriteCallback>,
) {
    config.error_cb = error_cb;
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

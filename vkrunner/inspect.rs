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

use crate::format::Format;
use std::ffi::{c_int, c_void};
use std::fmt;

#[repr(C)]
pub struct Image {
    /// Width of the buffer
    pub width: c_int,
    /// Height of the buffer
    pub height: c_int,

    /// The stride in bytes from one row of the image to the next
    pub stride: usize,

    /// A description the format of each pixel in the buffer
    pub format: &'static Format,

    /// The buffer data
    pub data: *const c_void,
}

#[repr(C)]
pub struct Buffer {
    /// The binding number of the buffer
    pub binding: c_int,
    /// Size in bytes of the buffer
    pub size: usize,
    /// The buffer data
    pub data: *const c_void,
}

#[repr(C)]
pub struct Data {
    /// The color buffer
    pub color_buffer: Image,
    /// An array of buffers used as UBOs or SSBOs
    pub n_buffers: usize,
    pub buffers: *const Buffer,
}

/// A callback used to report the buffer and image data after
/// executing each test.
pub type Callback = extern "C" fn(
    data: &Data,
    user_data: *mut c_void,
);

/// A struct to combine the inspection callback with its user data pointer
#[derive(Clone)]
pub(crate) struct Inspector {
    callback: Callback,
    user_data: *mut c_void,
}

impl Inspector {
    pub fn new(callback: Callback, user_data: *mut c_void) -> Inspector {
        Inspector { callback, user_data }
    }

    pub fn inspect(&self, data: &Data) {
        (self.callback)(data, self.user_data);
    }
}

impl fmt::Debug for Inspector {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Inspector")
            .field("callback", &(self.callback as usize))
            .field("user_data", &self.user_data)
            .finish()
    }
}

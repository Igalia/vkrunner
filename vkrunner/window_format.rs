// vkrunner
//
// Copyright (C) 2013, 2014, 2015, 2017, 2023 Neil Roberts
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
use crate::vk;

#[derive(Clone, Debug, PartialEq)]
pub struct WindowFormat {
    pub color_format: &'static Format,
    pub depth_stencil_format: Option<&'static Format>,
    pub width: usize,
    pub height: usize,
}

impl Default for WindowFormat {
    fn default() -> WindowFormat {
        let color_format =
            Format::lookup_by_vk_format(vk::VK_FORMAT_B8G8R8A8_UNORM);

        WindowFormat {
            color_format,
            depth_stencil_format: None,
            width: 250,
            height: 250,
        }
    }
}

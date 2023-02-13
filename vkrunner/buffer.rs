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

//! Module containing helper structs to automatically free vkBuffers,
//! vkDeviceMemorys and to unmap mapped memory.

use crate::vk;
use crate::context::Context;
use crate::allocate_store;
use std::ffi::c_void;
use std::rc::Rc;
use std::ptr;
use std::fmt;

#[derive(Debug)]
pub enum Error {
    MapMemoryError,
    AllocateStoreError(String),
    BufferError,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::BufferError => write!(
                f,
                "Error creating vkBuffer",
            ),
            Error::MapMemoryError => write!(
                f,
                "vkMapMemory failed",
            ),
            Error::AllocateStoreError(s) => write!(f, "{}", s),
        }
    }
}

#[derive(Debug)]
pub struct MappedMemory {
    pub pointer: *mut c_void,
    memory: vk::VkDeviceMemory,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for MappedMemory {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkUnmapMemory.unwrap()(
                self.context.vk_device(),
                self.memory,
            );
        }
    }
}

impl MappedMemory {
    pub fn new(
        context: Rc<Context>,
        memory: vk::VkDeviceMemory,
    ) -> Result<MappedMemory, Error> {
        let mut pointer: *mut c_void = ptr::null_mut();

        let res = unsafe {
            context.device().vkMapMemory.unwrap()(
                context.vk_device(),
                memory,
                0, // offset
                vk::VK_WHOLE_SIZE as u64,
                0, // flags,
                ptr::addr_of_mut!(pointer),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(MappedMemory { pointer, memory, context })
        } else {
            Err(Error::MapMemoryError)
        }
    }
}

#[derive(Debug)]
pub struct DeviceMemory {
    pub memory: vk::VkDeviceMemory,
    pub memory_type_index: u32,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for DeviceMemory {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkFreeMemory.unwrap()(
                self.context.vk_device(),
                self.memory,
                ptr::null(), // allocator
            );
        }
    }
}

impl DeviceMemory {
    pub fn new_buffer(
        context: Rc<Context>,
        memory_type_flags: vk::VkMemoryPropertyFlags,
        buffer: vk::VkBuffer,
    ) -> Result<DeviceMemory, Error> {
        let res = allocate_store::allocate_buffer(
            context.as_ref(),
            memory_type_flags,
            buffer,
        );
        DeviceMemory::new_from_result(context, res)
    }

    pub fn new_image(
        context: Rc<Context>,
        memory_type_flags: vk::VkMemoryPropertyFlags,
        image: vk::VkImage,
    ) -> Result<DeviceMemory, Error> {
        let res = allocate_store::allocate_image(
            context.as_ref(),
            memory_type_flags,
            image,
        );
        DeviceMemory::new_from_result(context, res)
    }

    fn new_from_result(
        context: Rc<Context>,
        result: Result<(vk::VkDeviceMemory, u32), String>,
    ) -> Result<DeviceMemory, Error> {
        match result {
            Ok((memory, memory_type_index)) => Ok(DeviceMemory {
                memory,
                memory_type_index,
                context,
            }),
            Err(e) => Err(Error::AllocateStoreError(e)),
        }
    }
}

#[derive(Debug)]
pub struct Buffer {
    pub buffer: vk::VkBuffer,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for Buffer {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkDestroyBuffer.unwrap()(
                self.context.vk_device(),
                self.buffer,
                ptr::null(), // allocator
            );
        }
    }
}

impl Buffer {
    pub fn new(
        context: Rc<Context>,
        size: usize,
        usage: vk::VkBufferUsageFlagBits,
    ) -> Result<Buffer, Error> {
        let buffer_create_info = vk::VkBufferCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            size: size as vk::VkDeviceSize,
            usage,
            sharingMode: vk::VK_SHARING_MODE_EXCLUSIVE,
            queueFamilyIndexCount: 0,
            pQueueFamilyIndices: ptr::null(),
        };

        let mut buffer: vk::VkBuffer = ptr::null_mut();

        let res = unsafe {
            context.device().vkCreateBuffer.unwrap()(
                context.vk_device(),
                ptr::addr_of!(buffer_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(buffer),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(Buffer { buffer, context })
        } else {
            Err(Error::BufferError)
        }
    }
}

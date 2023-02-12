// vkrunner
//
// Copyright (C) 2017, 2023 Neil Roberts
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

use crate::context::Context;
use crate::vk;
use std::ptr;
use std::fmt;
use std::ffi::c_int;

#[derive(Debug)]
pub struct Error(vk::VkResult);

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "vkFlushMappedMemoryRanges failed")
    }
}

/// Calls `vkFlushMappedMemoryRanges` with the range specified in
/// `offset` and `size` unless the specified memory type has the
/// `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` property set.
///
/// For testing, the environment variable
/// `VKRUNNER_ALWAYS_FLUSH_MEMORY` can be set to `true` to make it
/// always flush the memory regardless of memory type properties.
pub fn flush_memory(
    context: &Context,
    memory_type_index: usize,
    memory: vk::VkDeviceMemory,
    offset: vk::VkDeviceSize,
    size: vk::VkDeviceSize,
) -> Result<(), Error> {
    let memory_properties = context.memory_properties();
    let memory_type =
        &memory_properties.memoryTypes[memory_type_index as usize];

    // We don’t need to do anything if the memory is already coherent
    if !context.always_flush_memory()
        && (memory_type.propertyFlags
            & vk::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0
    {
        return Ok(());
    }

    let mapped_memory_range = vk::VkMappedMemoryRange {
        sType: vk::VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        pNext: ptr::null(),
        memory,
        offset,
        size,
    };

    let res = unsafe {
        context.device().vkFlushMappedMemoryRanges.unwrap()(
            context.vk_device(),
            1, // memoryRangeCount
            ptr::addr_of!(mapped_memory_range),
        )
    };

    if res == vk::VK_SUCCESS {
        Ok(())
    } else {
        Err(Error(res))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::rc::Rc;
    use crate::requirements::Requirements;
    use crate::fake_vulkan::FakeVulkan;
    use crate::env_var_test::EnvVarLock;

    struct Memory {
        handle: vk::VkDeviceMemory,
        context: Rc<Context>,
    }

    impl Memory {
        fn new(context: Rc<Context>) -> Memory {
            let mut handle = ptr::null_mut();

            let allocate_info = vk::VkMemoryAllocateInfo {
                sType: vk::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                pNext: ptr::null(),
                allocationSize: 1024,
                memoryTypeIndex: 0,
            };

            unsafe {
                let res = context.device().vkAllocateMemory.unwrap()(
                    context.vk_device(),
                    ptr::addr_of!(allocate_info),
                    ptr::null(), // allocator
                    ptr::addr_of_mut!(handle),
                );

                assert_eq!(res, vk::VK_SUCCESS);
            }

            Memory { handle, context }
        }
    }

    impl Drop for Memory {
        fn drop(&mut self) {
            unsafe {
                self.context.device().vkFreeMemory.unwrap()(
                    self.context.vk_device(),
                    self.handle,
                    ptr::null_mut(), // allocator
                );
            }
        }
    }

    struct TestData {
        context: Rc<Context>,
        fake_vulkan: Box<FakeVulkan>,
    }

    impl TestData {
        fn new(coherent_memory: bool) -> TestData {
            let mut fake_vulkan = FakeVulkan::new();
            fake_vulkan.physical_devices.push(Default::default());
            let memory_properties =
                &mut fake_vulkan.physical_devices[0].memory_properties;
            memory_properties.memoryTypeCount = 1;

            memory_properties.memoryTypes[0].propertyFlags =
                if coherent_memory {
                    vk::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                } else {
                    0
                };

            fake_vulkan.set_override();
            let context = Rc::new(
                Context::new(&Requirements::new(), None).unwrap()
            );

            TestData { context, fake_vulkan }
        }
    }

    fn test_flag_combination(
        memory_is_coherent: bool,
        always_flush: bool,
        flush_expected: bool,
    ) {
        let _env_var_lock = EnvVarLock::new(&[
            (
                "VKRUNNER_ALWAYS_FLUSH_MEMORY",
                if always_flush { "true" } else { "false" },
            )
        ]);

        let test_data = TestData::new(memory_is_coherent);

        let memory = Memory::new(Rc::clone(&test_data.context));

        flush_memory(
            &test_data.context,
            0, // memory_type_index
            memory.handle,
            16, // offset
            24, // size
        ).unwrap();

        if flush_expected {
            assert_eq!(test_data.fake_vulkan.memory_flushes.len(), 1);
            assert_eq!(
                test_data.fake_vulkan.memory_flushes[0].memory,
                memory.handle,
            );
            assert_eq!(test_data.fake_vulkan.memory_flushes[0].offset, 16);
            assert_eq!(test_data.fake_vulkan.memory_flushes[0].size, 24);
        } else {
            assert_eq!(test_data.fake_vulkan.memory_flushes.len(), 0);
        }
    }

    #[test]
    fn should_flush() {
        test_flag_combination(
            false, // memory_is_coherent
            false, // always_flush
            true, // flush_expected
        );
        test_flag_combination(
            false, // memory_is_coherent
            true, // always_flush
            true, // flush_expected
        );
        test_flag_combination(
            true, // memory_is_coherent
            false, // always_flush
            false, // flush_expected
        );
        test_flag_combination(
            true, // memory_is_coherent
            true, // always_flush
            true, // flush_expected
        );
    }

    #[test]
    fn error() {
        let _env_var_lock = EnvVarLock::new(&[
            ("VKRUNNER_ALWAYS_FLUSH_MEMORY", "false")
        ]);

        let mut test_data = TestData::new(
            false, // memory_is_coherent
        );

        let memory = Memory::new(Rc::clone(&test_data.context));

        test_data.fake_vulkan.queue_result(
            "vkFlushMappedMemoryRanges".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );

        let error = flush_memory(
            &test_data.context,
            0, // memory_type_index
            memory.handle,
            16, // offset
            24, // size
        ).unwrap_err();

        assert_eq!(error.to_string(), "vkFlushMappedMemoryRanges failed");
        assert_eq!(error.0, vk::VK_ERROR_UNKNOWN);
    }
}

#[no_mangle]
pub extern "C" fn vr_flush_memory(
    context: &Context,
    memory_type_index: c_int,
    memory: vk::VkDeviceMemory,
    offset: vk::VkDeviceSize,
    size: vk::VkDeviceSize
) -> vk::VkResult {
    flush_memory(
        context,
        memory_type_index as usize,
        memory,
        offset,
        size
    ).map(|_| vk::VK_SUCCESS).unwrap_or_else(|Error(result)| result)
}

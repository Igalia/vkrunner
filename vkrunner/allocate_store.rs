// vkrunner
//
// Copyright (C) 2016, 2017, 2023 Neil Roberts
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

//! Helper functions for allocating Vulkan device memory for a buffer
//! or an image.

use crate::context::Context;
use crate::vk;
use std::ptr;

fn find_memory_type(
    context: &Context,
    mut usable_memory_types: u32,
    memory_type_flags: vk::VkMemoryPropertyFlags,
) -> Result<u32, String> {
    let memory_properties = context.memory_properties();

    while usable_memory_types != 0 {
        let memory_type = usable_memory_types.trailing_zeros();

        if memory_properties.memoryTypes[memory_type as usize].propertyFlags
            & memory_type_flags
            == memory_type_flags
        {
            return Ok(memory_type);
        }

        usable_memory_types &= !(1 << memory_type);
    }

    Err("Couldn’t find suitable memory type to allocate buffer".to_string())
}

fn allocate_memory(
    context: &Context,
    reqs: &vk::VkMemoryRequirements,
    memory_type_flags: vk::VkMemoryPropertyFlags,
) -> Result<(vk::VkDeviceMemory, u32), String> {
    let memory_type_index = find_memory_type(
        context,
        reqs.memoryTypeBits,
        memory_type_flags,
    )?;

    let mut memory = vk::null_handle();

    let allocate_info = vk::VkMemoryAllocateInfo {
        sType: vk::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        pNext: ptr::null(),
        allocationSize: reqs.size,
        memoryTypeIndex: memory_type_index,
    };
    let res = unsafe {
        context.device().vkAllocateMemory.unwrap()(
            context.vk_device(),
            ptr::addr_of!(allocate_info),
            ptr::null(), // allocator
            ptr::addr_of_mut!(memory),
        )
    };
    if res == vk::VK_SUCCESS {
        Ok((memory, memory_type_index))
    } else {
        Err("vkAllocateMemory failed".to_string())
    }
}

/// Allocate Vulkan device memory for the given buffer. It will pick
/// the right memory type by querying the device for the memory
/// requirements of the buffer. You can also limit the memory types
/// further by specifying extra flags in `memory_type_flags`. A handle
/// to the newly allocate device memory will be returned along with an
/// index representing the chosen memory type.
///
/// If the allocation fails a `String` will be returned describing the
/// error.
pub fn allocate_buffer(
    context: &Context,
    memory_type_flags: vk::VkMemoryPropertyFlags,
    buffer: vk::VkBuffer,
) -> Result<(vk::VkDeviceMemory, u32), String> {
    let vkdev = context.device();

    let mut reqs = vk::VkMemoryRequirements::default();

    unsafe {
        vkdev.vkGetBufferMemoryRequirements.unwrap()(
            context.vk_device(),
            buffer,
            ptr::addr_of_mut!(reqs),
        );
    }

    let (memory, memory_type_index) =
        allocate_memory(context, &reqs, memory_type_flags)?;

    unsafe {
        vkdev.vkBindBufferMemory.unwrap()(
            context.vk_device(),
            buffer,
            memory,
            0, // memoryOffset
        );
    }

    Ok((memory, memory_type_index))
}

/// Allocate Vulkan device memory for the given image. It will pick
/// the right memory type by querying the device for the memory
/// requirements of the image. You can also limit the memory types
/// further by specifying extra flags in `memory_type_flags`. A handle
/// to the newly allocate device memory will be returned along with an
/// index representing the chosen memory type.
///
/// If the allocation fails a `String` will be returned describing the
/// error.
pub fn allocate_image(
    context: &Context,
    memory_type_flags: vk::VkMemoryPropertyFlags,
    image: vk::VkImage,
) -> Result<(vk::VkDeviceMemory, u32), String> {
    let vkdev = context.device();

    let mut reqs = vk::VkMemoryRequirements::default();

    unsafe {
        vkdev.vkGetImageMemoryRequirements.unwrap()(
            context.vk_device(),
            image,
            ptr::addr_of_mut!(reqs),
        );
    }

    let (memory, memory_type_index) =
        allocate_memory(context, &reqs, memory_type_flags)?;

    unsafe {
        vkdev.vkBindImageMemory.unwrap()(
            context.vk_device(),
            image,
            memory,
            0, // memoryOffset
        );
    }

    Ok((memory, memory_type_index))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, HandleType};
    use crate::requirements::Requirements;

    fn call_with_temp_buffer(
        fake_vulkan: &mut FakeVulkan,
        context: &Context,
        memory_type_flags: vk::VkMemoryPropertyFlags,
    ) -> Result<(vk::VkDeviceMemory, u32), String> {
        let buffer = fake_vulkan.add_handle(
            HandleType::Buffer {
                create_info: Default::default(),
                memory: None,
            }
        );

        let res = allocate_buffer(context, memory_type_flags, buffer);

        fake_vulkan.get_handle_mut(buffer).freed = true;

        res
    }

    fn call_with_temp_image(
        fake_vulkan: &mut FakeVulkan,
        context: &Context,
        memory_type_flags: vk::VkMemoryPropertyFlags,
    ) -> Result<(vk::VkDeviceMemory, u32), String> {
        let image = fake_vulkan.add_handle(HandleType::Image);

        let res = allocate_image(context, memory_type_flags, image);

        fake_vulkan.get_handle_mut(image).freed = true;

        res
    }

    fn do_allocate_buffer(
        memory_property_flags: &[u32],
        memory_type_bits: u32,
        memory_type_flags: vk::VkMemoryPropertyFlags,
    ) -> Result<(vk::VkDeviceMemory, u32, Context, Box<FakeVulkan>), String> {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        let memory_properties =
            &mut fake_vulkan.physical_devices[0].memory_properties;

        for (i, &flags) in memory_property_flags.iter().enumerate() {
            memory_properties.memoryTypes[i].propertyFlags = flags;
        }
        memory_properties.memoryTypeCount = memory_property_flags.len() as u32;

        fake_vulkan.memory_requirements.memoryTypeBits = memory_type_bits;

        fake_vulkan.set_override();
        let context = Context::new(&mut Requirements::new(), None).unwrap();
        call_with_temp_buffer(
            &mut fake_vulkan,
            &context,
            memory_type_flags,
        ).map(|(memory, memory_type)| (
            memory,
            memory_type,
            context,
            fake_vulkan,
        ))
    }

    #[test]
    fn find_memory() {
        let (device_memory, memory_type, context, fake_vulkan) =
            do_allocate_buffer(
            // Made-up set of memory properties
            &[
                vk::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | vk::VK_MEMORY_PROPERTY_PROTECTED_BIT,
                vk::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | vk::VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
                vk::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | vk::VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
            ],
            // Pretend that the buffer can use types 0, 1 or 3
            0b1011,
            // Pretend we need host visible and lazily allocated. This
            // means either of the last two
            vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | vk::VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
        ).unwrap();

        // Only 2 or 3 have the properties we need, but only 3 is
        // allowed for the buffer requirements.
        assert_eq!(memory_type, 3);

        unsafe {
            context.device().vkFreeMemory.unwrap()(
                context.vk_device(),
                device_memory,
                ptr::null(), // allocator
            );
        }

        drop(context);
        drop(fake_vulkan);

        // Try with an impossible combination
        let err = do_allocate_buffer(
            &[
                vk::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            ],
            0b1,
            vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | vk::VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
        ).unwrap_err();

        assert_eq!(
            err,
            "Couldn’t find suitable memory type to allocate buffer"
        );
    }

    fn make_error_context() -> (Box<FakeVulkan>, Context) {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        fake_vulkan.physical_devices[0].memory_properties.memoryTypeCount = 1;
        fake_vulkan.memory_requirements.memoryTypeBits = 1;

        fake_vulkan.queue_result(
            "vkAllocateMemory".to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        fake_vulkan.set_override();
        let context = Context::new(&mut Requirements::new(), None).unwrap();

        (fake_vulkan, context)
    }

    #[test]
    fn buffer_error() {
        let (mut fake_vulkan, context) = make_error_context();

        let err = call_with_temp_buffer(
            &mut fake_vulkan,
            &context,
            0, // memory_type_flags
        ).unwrap_err();

        assert_eq!(err, "vkAllocateMemory failed");
    }

    #[test]
    fn image_error() {
        let (mut fake_vulkan, context) = make_error_context();

        let err = call_with_temp_image(
            &mut fake_vulkan,
            &context,
            0, // memory_type_flags
        ).unwrap_err();

        assert_eq!(err, "vkAllocateMemory failed");
    }

    #[test]
    fn image() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        fake_vulkan.physical_devices[0].memory_properties.memoryTypeCount = 1;
        fake_vulkan.memory_requirements.memoryTypeBits = 1;

        fake_vulkan.set_override();
        let context = Context::new(&mut Requirements::new(), None).unwrap();

        let (device_memory, memory_type) = call_with_temp_image(
            &mut fake_vulkan,
            &context,
            0, // memory_type_flags
        ).unwrap();

        assert!(device_memory != vk::null_handle());
        assert_eq!(memory_type, 0);

        unsafe {
            context.device().vkFreeMemory.unwrap()(
                context.vk_device(),
                device_memory,
                ptr::null(), // allocator
            );
        }
    }
}

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

//! Sets up a fake Vulkan driver which can be manipulated to report
//! different extensions and features. This is only build in test
//! configurations and it is intended to help unit testing.

use crate::vk;
use crate::vulkan_funcs;
use crate::requirements;
use std::cell::Cell;
use std::mem;
use std::mem::transmute;
use std::ffi::{c_char, CStr, c_void};
use std::ptr;
use std::cmp::min;
use std::collections::{HashMap, VecDeque};

// Pointer to the current FakeVulkan instance that was created in
// this thread. There can only be one instance per thread.
thread_local! {
    static CURRENT_FAKE_VULKAN: Cell<Option<*mut FakeVulkan>> = Cell::new(None);
}

fn add_extension_to_vec(
    extension_vec: &mut Vec<vk::VkExtensionProperties>,
    ext: &str,
) {
    let old_len = extension_vec.len();
    extension_vec.resize(old_len + 1, Default::default());
    let props = extension_vec.last_mut().unwrap();
    for (i, b) in ext
        .bytes()
        .take(min(props.extensionName.len() - 1, ext.len()))
        .enumerate()
    {
        props.extensionName[i] = b as c_char;
    }
}

/// A structure containing the physical device infos that will be
/// reported by the driver.
#[derive(Debug, Clone)]
pub struct PhysicalDeviceInfo {
    pub properties: vk::VkPhysicalDeviceProperties,
    pub memory_properties: vk::VkPhysicalDeviceMemoryProperties,
    pub features: vk::VkPhysicalDeviceFeatures,
    pub queue_families: Vec<vk::VkQueueFamilyProperties>,
    pub extensions: Vec<vk::VkExtensionProperties>,
    pub format_properties: HashMap<vk::VkFormat, vk::VkFormatProperties>,
    // Two random extension feature sets to report when asked
    pub shader_atomic: vk::VkPhysicalDeviceShaderAtomicInt64FeaturesKHR,
    pub multiview: vk::VkPhysicalDeviceMultiviewFeaturesKHR,
}

impl PhysicalDeviceInfo {
    pub fn add_extension(&mut self, ext: &str) {
        add_extension_to_vec(&mut self.extensions, ext);
    }
}

impl Default for PhysicalDeviceInfo {
    fn default() -> PhysicalDeviceInfo {
        PhysicalDeviceInfo {
            properties: vk::VkPhysicalDeviceProperties {
                apiVersion: requirements::make_version(1, 0, 0),
                driverVersion: 0,
                vendorID: 0xfa4eed,
                deviceID: 0xfa4ede,
                deviceType: vk::VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
                deviceName: [0; 256],
                pipelineCacheUUID: *b"fakevulkan123456",
                limits: Default::default(),
                sparseProperties: Default::default(),
            },
            memory_properties: Default::default(),
            format_properties: HashMap::new(),
            features: Default::default(),
            queue_families: vec![vk::VkQueueFamilyProperties {
                queueFlags: vk::VK_QUEUE_GRAPHICS_BIT,
                queueCount: 1,
                timestampValidBits: 32,
                minImageTransferGranularity: Default::default(),
            }],
            extensions: Vec::new(),
            shader_atomic: Default::default(),
            multiview: Default::default(),
        }
    }
}

const ATOMIC_TYPE: vk::VkStructureType =
    vk::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR;
const MULTIVIEW_TYPE: vk::VkStructureType =
    vk::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;

#[derive(Debug)]
pub enum HandleType {
    Instance,
    Device,
    CommandPool,
    CommandBuffer { command_pool: usize },
    Fence,
    Memory { mapping: Option<Vec<u8>> },
    RenderPass { attachments: Vec<vk::VkAttachmentDescription> },
    Image,
    ImageView,
    Buffer,
    Framebuffer,
    ShaderModule { code: Vec<u32> },
}

#[derive(Debug)]
pub struct Handle {
    pub freed: bool,
    pub data: HandleType,
}

/// A fake Vulkan driver. Note that there can only be one FakeVulkan
/// instance per-thread because it needs to use thread-local storage
/// to figure out the current fake driver when the fake
/// vkCreateInstance is called. The FakeVulkan should always be stored
/// in a box so that its address can be tracked in
/// [CURRENT_FAKE_VULKAN].
#[derive(Debug)]
pub struct FakeVulkan {
    pub physical_devices: Vec<PhysicalDeviceInfo>,
    pub instance_extensions: Vec<vk::VkExtensionProperties>,

    // A fake set of requirements to return from the next call to
    // vkGetBufferMemoryRequirements or vkGetImageMemoryRequirements.
    pub memory_requirements: vk::VkMemoryRequirements,

    /// Whether to claim that the vkEnumerateInstanceVersion function
    /// is available.
    pub has_enumerate_instance_version: bool,

    handles: Vec<Handle>,

    // Queue of values to return instead of VK_SUCCESS to simulate
    // function call failures. This is indexed by the function name
    // and the value is a queue of override values to return.
    result_queue: HashMap<String, VecDeque<vk::VkResult>>,
}

impl FakeVulkan {
    pub fn new() -> Box<FakeVulkan> {
        let mut fake_vulkan = Box::new(FakeVulkan {
            physical_devices: Vec::new(),
            instance_extensions: Vec::new(),
            memory_requirements: Default::default(),
            handles: Vec::new(),
            has_enumerate_instance_version: false,
            result_queue: HashMap::new(),
        });

        CURRENT_FAKE_VULKAN.with(|f| {
            let old_value = f.replace(Some(
                fake_vulkan.as_mut() as *mut FakeVulkan
            ));

            // There can only be one FakeVulkan instance per thread at a time
            assert!(old_value.is_none());
        });

        fake_vulkan
    }

    pub fn current() -> &'static mut FakeVulkan {
        unsafe { &mut *CURRENT_FAKE_VULKAN.with(|f| f.get().unwrap()) }
    }

    fn next_result(&mut self, func_name: &str) -> vk::VkResult {
        match self.result_queue.get_mut(func_name) {
            Some(queue) => match queue.pop_front() {
                Some(res) => res,
                None => vk::VK_SUCCESS,
            },
            None => vk::VK_SUCCESS,
        }
    }

    /// Queue a VkResult to return the next time the named function is
    /// called. The value will be used only once and after that the
    /// function will revert to always returning VK_SUCCESS. This can
    /// be called multiple times to queue multiple results before
    /// reverting.
    pub fn queue_result(&mut self, func_name: String, result: vk::VkResult) {
        self.result_queue
            .entry(func_name)
            .or_insert_with(Default::default)
            .push_back(result);
    }

    /// Sets the get_proc_addr override on the [vulkan_funcs] so that
    /// it will use this FakeVulkan driver the next time a
    /// [Library](vulkan_funcs::Library) is created.
    pub fn set_override(&self) {
        vulkan_funcs::override_get_instance_proc_addr(
            ptr::addr_of!(*self).cast(),
            Some(FakeVulkan::get_instance_proc_addr),
        );
    }

    pub fn add_instance_extension(&mut self, ext: &str) {
        add_extension_to_vec(&mut self.instance_extensions, ext);
    }

    pub fn get_function(&self, name: *const c_char) -> vk::PFN_vkVoidFunction {
        let name = unsafe { CStr::from_ptr(name).to_str().unwrap() };

        match name {
            "vkGetDeviceProcAddr" => unsafe {
                transmute::<vk::PFN_vkGetDeviceProcAddr, _>(
                    Some(FakeVulkan::get_device_proc_addr)
                )
            },
            "vkCreateInstance" => unsafe {
                transmute::<vk::PFN_vkCreateInstance, _>(
                    Some(FakeVulkan::create_instance)
                )
            },
            "vkEnumeratePhysicalDevices" => unsafe {
                transmute::<vk::PFN_vkEnumeratePhysicalDevices, _>(
                    Some(FakeVulkan::enumerate_physical_devices)
                )
            },
            "vkGetPhysicalDeviceMemoryProperties" => unsafe {
                transmute::<vk::PFN_vkGetPhysicalDeviceMemoryProperties, _>(
                    Some(FakeVulkan::get_physical_device_memory_properties)
                )
            },
            "vkGetPhysicalDeviceFormatProperties" => unsafe {
                transmute::<vk::PFN_vkGetPhysicalDeviceFormatProperties, _>(
                    Some(FakeVulkan::get_physical_device_format_properties)
                )
            },
            "vkGetPhysicalDeviceProperties" => unsafe {
                transmute::<vk::PFN_vkGetPhysicalDeviceProperties, _>(
                    Some(FakeVulkan::get_physical_device_properties)
                )
            },
            "vkGetPhysicalDeviceFeatures" => unsafe {
                transmute::<vk::PFN_vkGetPhysicalDeviceFeatures, _>(
                    Some(FakeVulkan::get_physical_device_features)
                )
            },
            "vkGetPhysicalDeviceQueueFamilyProperties" => unsafe {
                type T = vk::PFN_vkGetPhysicalDeviceQueueFamilyProperties;
                transmute::<T, _>(Some(
                    FakeVulkan::get_physical_device_queue_family_properties
                ))
            },
            "vkGetDeviceQueue" => unsafe {
                transmute::<vk::PFN_vkGetDeviceQueue, _>(
                    Some(FakeVulkan::get_device_queue)
                )
            },
            "vkEnumerateInstanceExtensionProperties" => unsafe {
                transmute::<vk::PFN_vkEnumerateInstanceExtensionProperties, _>(
                    Some(FakeVulkan::enumerate_instance_extension_properties)
                )
            },
            "vkEnumerateDeviceExtensionProperties" => unsafe {
                transmute::<vk::PFN_vkEnumerateDeviceExtensionProperties, _>(
                    Some(FakeVulkan::enumerate_device_extension_properties)
                )
            },
            "vkCreateDevice" => unsafe {
                transmute::<vk::PFN_vkCreateDevice, _>(
                    Some(FakeVulkan::create_device)
                )
            },
            "vkDestroyInstance" => unsafe {
                transmute::<vk::PFN_vkDestroyInstance, _>(
                    Some(FakeVulkan::destroy_instance)
                )
            },
            "vkDestroyDevice" => unsafe {
                transmute::<vk::PFN_vkDestroyDevice, _>(
                    Some(FakeVulkan::destroy_device)
                )
            },
            "vkCreateCommandPool" => unsafe {
                transmute::<vk::PFN_vkCreateCommandPool, _>(
                    Some(FakeVulkan::create_command_pool)
                )
            },
            "vkDestroyCommandPool" => unsafe {
                transmute::<vk::PFN_vkDestroyCommandPool, _>(
                    Some(FakeVulkan::destroy_command_pool)
                )
            },
            "vkAllocateCommandBuffers" => unsafe {
                transmute::<vk::PFN_vkAllocateCommandBuffers, _>(
                    Some(FakeVulkan::allocate_command_buffers)
                )
            },
            "vkFreeCommandBuffers" => unsafe {
                transmute::<vk::PFN_vkFreeCommandBuffers, _>(
                    Some(FakeVulkan::free_command_buffers)
                )
            },
            "vkCreateFence" => unsafe {
                transmute::<vk::PFN_vkCreateFence, _>(
                    Some(FakeVulkan::create_fence)
                )
            },
            "vkDestroyFence" => unsafe {
                transmute::<vk::PFN_vkDestroyFence, _>(
                    Some(FakeVulkan::destroy_fence)
                )
            },
            "vkCreateRenderPass" => unsafe {
                transmute::<vk::PFN_vkCreateRenderPass, _>(
                    Some(FakeVulkan::create_render_pass)
                )
            },
            "vkDestroyRenderPass" => unsafe {
                transmute::<vk::PFN_vkDestroyRenderPass, _>(
                    Some(FakeVulkan::destroy_render_pass)
                )
            },
            "vkCreateImageView" => unsafe {
                transmute::<vk::PFN_vkCreateImageView, _>(
                    Some(FakeVulkan::create_image_view)
                )
            },
            "vkDestroyImageView" => unsafe {
                transmute::<vk::PFN_vkDestroyImageView, _>(
                    Some(FakeVulkan::destroy_image_view)
                )
            },
            "vkCreateImage" => unsafe {
                transmute::<vk::PFN_vkCreateImage, _>(
                    Some(FakeVulkan::create_image)
                )
            },
            "vkDestroyImage" => unsafe {
                transmute::<vk::PFN_vkDestroyImage, _>(
                    Some(FakeVulkan::destroy_image)
                )
            },
            "vkCreateBuffer" => unsafe {
                transmute::<vk::PFN_vkCreateBuffer, _>(
                    Some(FakeVulkan::create_buffer)
                )
            },
            "vkDestroyBuffer" => unsafe {
                transmute::<vk::PFN_vkDestroyBuffer, _>(
                    Some(FakeVulkan::destroy_buffer)
                )
            },
            "vkCreateFramebuffer" => unsafe {
                transmute::<vk::PFN_vkCreateFramebuffer, _>(
                    Some(FakeVulkan::create_framebuffer)
                )
            },
            "vkDestroyFramebuffer" => unsafe {
                transmute::<vk::PFN_vkDestroyFramebuffer, _>(
                    Some(FakeVulkan::destroy_framebuffer)
                )
            },
            "vkEnumerateInstanceVersion" => unsafe {
                if self.has_enumerate_instance_version {
                    transmute::<vk::PFN_vkEnumerateInstanceVersion, _>(
                        Some(FakeVulkan::enumerate_instance_version)
                    )
                } else {
                    None
                }
            },
            "vkGetPhysicalDeviceFeatures2KHR" => unsafe {
                transmute::<vk::PFN_vkGetPhysicalDeviceFeatures2, _>(
                    Some(FakeVulkan::get_physical_device_features2)
                )
            },
            "vkGetImageMemoryRequirements" => unsafe {
                transmute::<vk::PFN_vkGetImageMemoryRequirements, _>(
                    Some(FakeVulkan::get_image_memory_requirements)
                )
            },
            "vkGetBufferMemoryRequirements" => unsafe {
                transmute::<vk::PFN_vkGetBufferMemoryRequirements, _>(
                    Some(FakeVulkan::get_buffer_memory_requirements)
                )
            },
            "vkBindBufferMemory" => unsafe {
                transmute::<vk::PFN_vkBindBufferMemory, _>(
                    Some(FakeVulkan::bind_buffer_memory)
                )
            },
            "vkBindImageMemory" => unsafe {
                transmute::<vk::PFN_vkBindImageMemory, _>(
                    Some(FakeVulkan::bind_image_memory)
                )
            },
            "vkAllocateMemory" => unsafe {
                transmute::<vk::PFN_vkAllocateMemory, _>(
                    Some(FakeVulkan::allocate_memory)
                )
            },
            "vkFreeMemory" => unsafe {
                transmute::<vk::PFN_vkFreeMemory, _>(
                    Some(FakeVulkan::free_memory)
                )
            },
            "vkMapMemory" => unsafe {
                transmute::<vk::PFN_vkMapMemory, _>(
                    Some(FakeVulkan::map_memory)
                )
            },
            "vkUnmapMemory" => unsafe {
                transmute::<vk::PFN_vkUnmapMemory, _>(
                    Some(FakeVulkan::unmap_memory)
                )
            },
            "vkCreateShaderModule" => unsafe {
                transmute::<vk::PFN_vkCreateShaderModule, _>(
                    Some(FakeVulkan::create_shader_module)
                )
            },
            "vkDestroyShaderModule" => unsafe {
                transmute::<vk::PFN_vkDestroyShaderModule, _>(
                    Some(FakeVulkan::destroy_shader_module)
                )
            },
            _ => None,
        }
    }

    extern "C" fn get_instance_proc_addr(
        _instance: vk::VkInstance,
        name: *const c_char,
    ) -> vk::PFN_vkVoidFunction {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.get_function(name)
    }

    extern "C" fn get_device_proc_addr(
        _device: vk::VkDevice,
        name: *const c_char,
    ) -> vk::PFN_vkVoidFunction {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.get_function(name)
    }

    fn copy_with_count<T>(
        values: &[T],
        count_ptr: *mut u32,
        array_ptr: *mut T,
    ) where
        T: Clone,
    {
        if array_ptr.is_null() {
            unsafe {
                *count_ptr = values.len() as u32;
            }

            return;
        }

        let count = min(
            unsafe { *count_ptr } as usize,
            values.len(),
        );

        for (i, value) in values.iter().take(count).enumerate() {
            unsafe {
                *array_ptr.add(i) = value.clone();
            }
        }
    }

    extern "C" fn enumerate_physical_devices(
        _instance: vk::VkInstance,
        physical_device_count: *mut u32,
        physical_devices: *mut vk::VkPhysicalDevice,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        if physical_devices.is_null() {
            unsafe {
                *physical_device_count =
                    fake_vulkan.physical_devices.len() as u32;
            }

            return fake_vulkan.next_result("vkEnumeratePhysicalDevices");
        }

        let count = min(
            unsafe { *physical_device_count as usize },
            fake_vulkan.physical_devices.len()
        );

        for i in 0..count {
            unsafe {
                // Store the device index as a pointer. We add 1 so
                // that it won’t be null.
                *physical_devices.add(i) =
                    fake_vulkan.index_to_physical_device(i);
            }
        }

        unsafe {
            *physical_device_count = count as u32;
        }

        fake_vulkan.next_result("vkEnumeratePhysicalDevices")
    }

    /// Get the physical device that would point to the device at the
    /// given index.
    pub fn index_to_physical_device(
        &self,
        index: usize,
    ) -> vk::VkPhysicalDevice {
        assert!(index < self.physical_devices.len());
        unsafe { transmute(index + 1) }
    }

    /// Get the index of the physical device represented by the
    /// VkPhysicalDevice pointer.
    pub fn physical_device_to_index(
        &self,
        physical_device: vk::VkPhysicalDevice
    ) -> usize {
        assert!(!physical_device.is_null());
        let index = unsafe { transmute::<_, usize>(physical_device) - 1 };
        assert!(index < self.physical_devices.len());
        index
    }

    extern "C" fn get_physical_device_memory_properties(
        physical_device: vk::VkPhysicalDevice,
        memory_properties_out: *mut vk::VkPhysicalDeviceMemoryProperties,
    ) {
        let fake_vulkan = FakeVulkan::current();

        unsafe {
            let device_num =
                fake_vulkan.physical_device_to_index(physical_device);
            let device = &fake_vulkan.physical_devices[device_num];
            *memory_properties_out = device.memory_properties.clone();
        }
    }

    extern "C" fn get_physical_device_properties(
        physical_device: vk::VkPhysicalDevice,
        properties_out: *mut vk::VkPhysicalDeviceProperties,
    ) {
        let fake_vulkan = FakeVulkan::current();

        unsafe {
            let device_num =
                fake_vulkan.physical_device_to_index(physical_device);
            let device = &fake_vulkan.physical_devices[device_num];
            *properties_out = device.properties.clone();
        }
    }

    extern "C" fn get_physical_device_features(
        physical_device: vk::VkPhysicalDevice,
        features_out: *mut vk::VkPhysicalDeviceFeatures,
    ) {
        let fake_vulkan = FakeVulkan::current();

        unsafe {
            let device_num =
                fake_vulkan.physical_device_to_index(physical_device);
            let device = &fake_vulkan.physical_devices[device_num];
            *features_out = device.features.clone();
        }
    }

    extern "C" fn get_physical_device_format_properties(
        physical_device: vk::VkPhysicalDevice,
        format: vk::VkFormat,
        properties: *mut vk::VkFormatProperties,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let device_num =
            fake_vulkan.physical_device_to_index(physical_device);
        let device = &fake_vulkan.physical_devices[device_num];

        unsafe {
            *properties = device.format_properties[&format];
        }
    }

    fn extract_struct_data(
        ptr: *mut u8
    ) -> (vk::VkStructureType, *mut u8) {
        let mut type_bytes =
            [0u8; mem::size_of::<vk::VkStructureType>()];
        unsafe {
            ptr.copy_to(type_bytes.as_mut_ptr(), type_bytes.len());
        }
        let mut next_bytes =
            [0u8; mem::size_of::<*mut u8>()];
        unsafe {
            ptr.add(vulkan_funcs::NEXT_PTR_OFFSET).copy_to(
                next_bytes.as_mut_ptr(), next_bytes.len()
            );
        }

        (
            vk::VkStructureType::from_ne_bytes(type_bytes),
            usize::from_ne_bytes(next_bytes) as *mut u8,
        )
    }

    extern "C" fn get_physical_device_features2(
        physical_device: vk::VkPhysicalDevice,
        features: *mut vk::VkPhysicalDeviceFeatures2,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let device_num = fake_vulkan.physical_device_to_index(physical_device);
        let device = &fake_vulkan.physical_devices[device_num];

        let (struct_type, mut struct_ptr) =
            FakeVulkan::extract_struct_data(features.cast());

        assert_eq!(
            struct_type,
            vk::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
        );

        while !struct_ptr.is_null() {
            let (struct_type, next_ptr) =
                FakeVulkan::extract_struct_data(struct_ptr);

            let to_copy = match struct_type {
                ATOMIC_TYPE => vec![
                    device.shader_atomic.shaderBufferInt64Atomics,
                    device.shader_atomic.shaderSharedInt64Atomics,
                ],
                MULTIVIEW_TYPE => vec![
                    device.multiview.multiview,
                    device.multiview.multiviewGeometryShader,
                    device.multiview.multiviewTessellationShader,
                ],
                _ => unreachable!("unexpected struct type {}", struct_type),
            };

            unsafe {
                std::ptr::copy(
                    to_copy.as_ptr(),
                    struct_ptr.add(vulkan_funcs::FIRST_FEATURE_OFFSET).cast(),
                    to_copy.len(),
                );
            }

            struct_ptr = next_ptr;
        }
    }

    extern "C" fn get_physical_device_queue_family_properties(
        physical_device: vk::VkPhysicalDevice,
        property_count_out: *mut u32,
        properties: *mut vk::VkQueueFamilyProperties,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let device_num = fake_vulkan.physical_device_to_index(physical_device);
        let device = &fake_vulkan.physical_devices[device_num];

        FakeVulkan::copy_with_count(
            &device.queue_families,
            property_count_out,
            properties,
        );
    }

    #[inline]
    pub fn make_queue(
        queue_family_index: u32,
        queue_index: u32
    ) -> vk::VkQueue {
        let queue =
            ((queue_family_index << 9) | (queue_index << 1) | 1) as usize;
        queue as vk::VkQueue
    }

    #[inline]
    pub fn unmake_queue(
        queue: vk::VkQueue,
    ) -> (u32, u32) {
        let queue = queue as usize;
        ((queue >> 9) as u32, ((queue >> 1) & 0xff) as u32)
    }

    extern "C" fn get_device_queue(
        _device: vk::VkDevice,
        queue_family_index: u32,
        queue_index: u32,
        queue_out: *mut vk::VkQueue,
    ) {
        unsafe {
            *queue_out = FakeVulkan::make_queue(
                queue_family_index,
                queue_index
            );
        }
    }

    extern "C" fn enumerate_instance_extension_properties(
        _layer_name: *const c_char,
        property_count: *mut u32,
        properties: *mut vk::VkExtensionProperties,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        FakeVulkan::copy_with_count(
            &fake_vulkan.instance_extensions,
            property_count,
            properties,
        );

        fake_vulkan.next_result("vkEnumerateInstanceExtensionProperties")
    }

    extern "C" fn enumerate_device_extension_properties(
        physical_device: vk::VkPhysicalDevice,
        _layer_name: *const c_char,
        property_count: *mut u32,
        properties: *mut vk::VkExtensionProperties,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let device_num = fake_vulkan.physical_device_to_index(physical_device);
        let device = &fake_vulkan.physical_devices[device_num];

        FakeVulkan::copy_with_count(
            &device.extensions,
            property_count,
            properties,
        );

        fake_vulkan.next_result("vkEnumerateDeviceExtensionProperties")
    }

    pub fn add_handle<T>(&mut self, data: HandleType) -> *mut T {
        self.handles.push(Handle {
            freed: false,
            data,
        });

        self.handles.len() as *mut T
    }

    pub fn handle_to_index<T>(handle: *mut T) -> usize {
        let handle_num = handle as usize;

        assert!(handle_num > 0);

        handle_num - 1
    }

    pub fn get_handle<T>(&mut self, handle: *mut T) -> &mut Handle {
        let handle = &mut self.handles[FakeVulkan::handle_to_index(handle)];

        assert!(!handle.freed);

        handle
    }

    fn check_device(&mut self, device: vk::VkDevice) {
        let handle = self.get_handle(device);
        assert!(matches!(handle.data, HandleType::Device));
    }

    fn check_command_pool(&mut self, command_pool: vk::VkCommandPool) {
        let handle = self.get_handle(command_pool);
        assert!(matches!(handle.data, HandleType::CommandPool));
    }

    fn check_image(&mut self, image: vk::VkImage) {
        let handle = self.get_handle(image);
        assert!(matches!(handle.data, HandleType::Image));
    }

    fn check_image_view(&mut self, image_view: vk::VkImageView) {
        let handle = self.get_handle(image_view);
        assert!(matches!(handle.data, HandleType::ImageView));
    }

    extern "C" fn create_instance(
        _create_info: *const vk::VkInstanceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        instance_out: *mut vk::VkInstance,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateInstance");

        if res != vk::VK_SUCCESS {
            return res;
        }

        unsafe {
            *instance_out = fake_vulkan.add_handle(HandleType::Instance);
        }

        res
    }

    extern "C" fn create_device(
        _physical_device: vk::VkPhysicalDevice,
        _create_info: *const vk::VkDeviceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        device_out: *mut vk::VkDevice,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateDevice");

        if res != vk::VK_SUCCESS {
            return res;
        }

        unsafe {
            *device_out = fake_vulkan.add_handle(HandleType::Device);
        }

        res
    }

    extern "C" fn destroy_device(
        device: vk::VkDevice,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        let handle = fake_vulkan.get_handle(device);
        assert!(matches!(handle.data, HandleType::Device));
        handle.freed = true;
    }

    extern "C" fn destroy_instance(
        instance: vk::VkInstance,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        let handle = fake_vulkan.get_handle(instance);
        assert!(matches!(handle.data, HandleType::Instance));
        handle.freed = true;
    }

    extern "C" fn create_command_pool(
        device: vk::VkDevice,
        _create_info: *const vk::VkCommandPoolCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        command_pool_out: *mut vk::VkCommandPool,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateCommandPool");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *command_pool_out = fake_vulkan.add_handle(HandleType::CommandPool);
        }

        res
    }

    extern "C" fn destroy_command_pool(
        device: vk::VkDevice,
        command_pool: vk::VkCommandPool,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(command_pool);
        assert!(matches!(handle.data, HandleType::CommandPool));
        handle.freed = true;
    }

    extern "C" fn allocate_command_buffers(
        device: vk::VkDevice,
        allocate_info: *const vk::VkCommandBufferAllocateInfo,
        command_buffers: *mut vk::VkCommandBuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkAllocateCommandBuffers");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let command_pool_handle = unsafe { (*allocate_info).commandPool };

        fake_vulkan.check_command_pool(command_pool_handle);

        let n_buffers = unsafe { (*allocate_info).commandBufferCount };

        for i in 0..(n_buffers as usize) {
            unsafe {
                *command_buffers.add(i) = fake_vulkan.add_handle(
                    HandleType::CommandBuffer {
                        command_pool: FakeVulkan::handle_to_index(
                            command_pool_handle
                        ),
                    },
                );
            }
        }

        res
    }

    extern "C" fn free_command_buffers(
        device: vk::VkDevice,
        command_pool: vk::VkCommandPool,
        command_buffer_count: u32,
        command_buffers: *const vk::VkCommandBuffer,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        fake_vulkan.check_command_pool(command_pool);

        for i in 0..command_buffer_count as usize {
            let command_buffer = unsafe {
                *command_buffers.add(i)
            };

            let command_buffer_handle = fake_vulkan.get_handle(command_buffer);

            match command_buffer_handle.data {
                HandleType::CommandBuffer { command_pool: handle_pool } => {
                    assert_eq!(
                        handle_pool,
                        FakeVulkan::handle_to_index(command_pool),
                    );
                    command_buffer_handle.freed = true;
                },
                _ => unreachable!("mismatched handle"),
            }
        }
    }

    extern "C" fn create_fence(
        device: vk::VkDevice,
        _create_info: *const vk::VkFenceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        fence_out: *mut vk::VkFence,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateFence");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *fence_out = fake_vulkan.add_handle(HandleType::Fence);
        }

        res
    }

    extern "C" fn destroy_fence(
        device: vk::VkDevice,
        fence: vk::VkFence,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(fence);
        assert!(matches!(handle.data, HandleType::Fence));
        handle.freed = true;
    }

    extern "C" fn create_render_pass(
        device: vk::VkDevice,
        create_info: *const vk::VkRenderPassCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        render_pass_out: *mut vk::VkRenderPass,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateRenderPass");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            let create_info = &*create_info;

            *render_pass_out = fake_vulkan.add_handle(HandleType::RenderPass {
                attachments: std::slice::from_raw_parts(
                    create_info.pAttachments,
                    create_info.attachmentCount as usize,
                ).to_owned(),
            });
        }

        res
    }

    extern "C" fn destroy_render_pass(
        device: vk::VkDevice,
        render_pass: vk::VkRenderPass,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(render_pass);
        assert!(matches!(handle.data, HandleType::RenderPass { .. }));
        handle.freed = true;
    }

    extern "C" fn create_image_view(
        device: vk::VkDevice,
        create_info: *const vk::VkImageViewCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        image_view_out: *mut vk::VkImageView,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateImageView");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        fake_vulkan.check_image(unsafe { *create_info }.image);

        unsafe {
            *image_view_out = fake_vulkan.add_handle(HandleType::ImageView);
        }

        res
    }

    extern "C" fn destroy_image_view(
        device: vk::VkDevice,
        image_view: vk::VkImageView,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(image_view);
        assert!(matches!(handle.data, HandleType::ImageView));
        handle.freed = true;
    }

    extern "C" fn create_image(
        device: vk::VkDevice,
        _create_info: *const vk::VkImageCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        image_out: *mut vk::VkImage,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateImage");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *image_out = fake_vulkan.add_handle(HandleType::Image);
        }

        res
    }

    extern "C" fn destroy_image(
        device: vk::VkDevice,
        image: vk::VkImage,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(image);
        assert!(matches!(handle.data, HandleType::Image));
        handle.freed = true;
    }

    extern "C" fn create_buffer(
        device: vk::VkDevice,
        _create_info: *const vk::VkBufferCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        buffer_out: *mut vk::VkBuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateBuffer");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *buffer_out = fake_vulkan.add_handle(HandleType::Buffer);
        }

        res
    }

    extern "C" fn destroy_buffer(
        device: vk::VkDevice,
        buffer: vk::VkBuffer,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(buffer);
        assert!(matches!(handle.data, HandleType::Buffer));
        handle.freed = true;
    }

    extern "C" fn create_framebuffer(
        device: vk::VkDevice,
        create_info: *const vk::VkFramebufferCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        framebuffer_out: *mut vk::VkFramebuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateFramebuffer");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let attachments = unsafe {
            let attachment_count = (*create_info).attachmentCount as usize;
            std::slice::from_raw_parts(
                (*create_info).pAttachments,
                attachment_count
            )
        };

        for &attachment in attachments {
            fake_vulkan.check_image_view(attachment);
        }

        unsafe {
            *framebuffer_out = fake_vulkan.add_handle(HandleType::Framebuffer);
        }

        res
    }

    extern "C" fn destroy_framebuffer(
        device: vk::VkDevice,
        framebuffer: vk::VkFramebuffer,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(framebuffer);
        assert!(matches!(handle.data, HandleType::Framebuffer));
        handle.freed = true;
    }

    extern "C" fn enumerate_instance_version(
        api_version: *mut u32
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();
        unsafe { *api_version = requirements::make_version(1, 1, 0) }
        fake_vulkan.next_result("vkEnumerateInstanceVersion")
    }

    fn get_memory_requirements(
        memory_requirements: *mut vk::VkMemoryRequirements,
    ) {
        let fake_vulkan = FakeVulkan::current();
        unsafe {
            *memory_requirements = fake_vulkan.memory_requirements.clone();
        }
    }

    extern "C" fn get_buffer_memory_requirements(
        _device: vk::VkDevice,
        _buffer: vk::VkBuffer,
        memory_requirements: *mut vk::VkMemoryRequirements,
    ) {
        FakeVulkan::get_memory_requirements(memory_requirements);
    }

    extern "C" fn get_image_memory_requirements(
        _device: vk::VkDevice,
        _buffer: vk::VkImage,
        memory_requirements: *mut vk::VkMemoryRequirements,
    ) {
        FakeVulkan::get_memory_requirements(memory_requirements);
    }

    extern "C" fn allocate_memory(
        device: vk::VkDevice,
        _allocate_info: *const vk::VkMemoryAllocateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        memory: *mut vk::VkDeviceMemory,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkAllocateMemory");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *memory = fake_vulkan.add_handle(HandleType::Memory {
                mapping: None,
            });
        }

        res
    }

    extern "C" fn free_memory(
        device: vk::VkDevice,
        memory: vk::VkDeviceMemory,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(memory);

        match handle.data {
            HandleType::Memory { ref mapping } => assert!(mapping.is_none()),
            _ => unreachable!("mismatched handle"),
        }

        handle.freed = true;
    }

    extern "C" fn bind_buffer_memory(
        _device: vk::VkDevice,
        _buffer: vk::VkBuffer,
        _memory: vk::VkDeviceMemory,
        _memory_offset: vk::VkDeviceSize,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();
        fake_vulkan.next_result("vkBindBufferMemory")
    }

    extern "C" fn bind_image_memory(
        _device: vk::VkDevice,
        _image: vk::VkImage,
        _memory: vk::VkDeviceMemory,
        _memory_offset: vk::VkDeviceSize,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();
        fake_vulkan.next_result("vkBindImageMemory")
    }

    extern "C" fn map_memory(
        device: vk::VkDevice,
        memory: vk::VkDeviceMemory,
        _offset: vk::VkDeviceSize,
        size: vk::VkDeviceSize,
        _flags: vk::VkMemoryMapFlags,
        data_out: *mut *mut c_void,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkMapMemory");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let mapping = match fake_vulkan.get_handle(memory).data {
            HandleType::Memory { ref mut mapping } => mapping,
            _ => unreachable!("mismatched handle"),
        };

        assert!(mapping.is_none());

        let size = if size == vk::VK_WHOLE_SIZE as vk::VkDeviceSize {
            10 * 1024 * 1024
        } else {
            size as usize
        };

        let mut data = Vec::new();
        data.resize(size, 0);

        unsafe {
            *data_out = mapping.insert(data).as_mut_ptr().cast();
        }

        res
    }

    extern "C" fn unmap_memory(
        device: vk::VkDevice,
        memory: vk::VkDeviceMemory
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let mapping = match fake_vulkan.get_handle(memory).data {
            HandleType::Memory { ref mut mapping } => mapping,
            _ => unreachable!("mismatched handle"),
        };

        assert!(mapping.is_some());
        *mapping = None;
    }

    extern "C" fn create_shader_module(
        device: vk::VkDevice,
        create_info: *const vk::VkShaderModuleCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        shader_module_out: *mut vk::VkShaderModule,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateShaderModule");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            assert_eq!((*create_info).codeSize % (u32::BITS as usize / 8), 0);

            let code = Vec::from(std::slice::from_raw_parts(
                (*create_info).pCode,
                (*create_info).codeSize / (u32::BITS as usize / 8),
            ));

            *shader_module_out = fake_vulkan.add_handle(
                HandleType::ShaderModule { code }
            );
        }

        res
    }

    extern "C" fn destroy_shader_module(
        device: vk::VkDevice,
        shader_module: vk::VkShaderModule,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle(shader_module);
        assert!(matches!(handle.data, HandleType::ShaderModule { .. }));
        handle.freed = true;
    }

}

impl Drop for FakeVulkan {
    fn drop(&mut self) {
        let old_value = CURRENT_FAKE_VULKAN.with(|f| f.replace(None));

        if !std::thread::panicking() {
            for handle in self.handles.iter() {
                assert!(handle.freed);

                match handle.data {
                    HandleType::Instance | HandleType::Device |
                    HandleType::CommandPool | HandleType::CommandBuffer { .. } |
                    HandleType::Fence | HandleType::RenderPass { .. } |
                    HandleType::Image | HandleType::ImageView |
                    HandleType::Buffer | HandleType::Framebuffer |
                    HandleType::ShaderModule { .. } => (),
                    HandleType::Memory { ref mapping } => {
                        assert!(mapping.is_none());
                    },
                }
            }

            // There should only be one FakeVulkan at a time so the
            // one we just dropped should be the one that was set for
            // the current thread.
            assert_eq!(old_value.unwrap(), self as *mut FakeVulkan);
        }
    }
}

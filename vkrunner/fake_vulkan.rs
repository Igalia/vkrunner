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
use std::ffi::{c_char, CStr};
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

    /// Whether to claim that the vkEnumerateInstanceVersion function
    /// is available.
    pub has_enumerate_instance_version: bool,

    n_instances: usize,
    n_devices: usize,
    n_command_pools: usize,
    n_command_buffers: usize,
    n_fences: usize,

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
            n_instances: 0,
            n_devices: 0,
            n_command_pools: 0,
            n_command_buffers: 0,
            n_fences: 0,
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

    extern "C" fn create_instance(
        _create_info: *const vk::VkInstanceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        instance_out: *mut vk::VkInstance,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.n_instances += 1;

        unsafe {
            *instance_out = fake_vulkan.n_instances as vk::VkInstance;
        }

        fake_vulkan.next_result("vkCreateInstance")
    }

    extern "C" fn create_device(
        _physical_device: vk::VkPhysicalDevice,
        _create_info: *const vk::VkDeviceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        device_out: *mut vk::VkDevice,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.n_devices += 1;

        unsafe {
            *device_out = fake_vulkan.n_devices as vk::VkDevice;
        }

        fake_vulkan.next_result("vkCreateDevice")
    }

    extern "C" fn destroy_device(
        _device: vk::VkDevice,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_devices >= 1);

        fake_vulkan.n_devices -= 1;
    }

    extern "C" fn destroy_instance(
        _instance: vk::VkInstance,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_instances >= 1);

        fake_vulkan.n_instances -= 1;
    }

    extern "C" fn create_command_pool(
        _device: vk::VkDevice,
        _create_info: *const vk::VkCommandPoolCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        command_pool_out: *mut vk::VkCommandPool,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.n_command_pools += 1;

        unsafe {
            *command_pool_out = 1usize as vk::VkCommandPool;
        }

        fake_vulkan.next_result("vkCreateCommandPool")
    }

    extern "C" fn destroy_command_pool(
        _device: vk::VkDevice,
        _command_pool: vk::VkCommandPool,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_command_pools >= 1);

        fake_vulkan.n_command_pools -= 1;
    }

    extern "C" fn allocate_command_buffers(
        _device: vk::VkDevice,
        allocate_info: *const vk::VkCommandBufferAllocateInfo,
        command_buffers: *mut vk::VkCommandBuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_command_pools >= 1);

        let n_buffers = unsafe { (*allocate_info).commandBufferCount };

        fake_vulkan.n_command_buffers += n_buffers as usize;

        for i in 0..(n_buffers as usize) {
            unsafe {
                *command_buffers.add(i) = (i + 1) as vk::VkCommandBuffer;
            }
        }

        fake_vulkan.next_result("vkAllocateCommandBuffers")
    }

    extern "C" fn free_command_buffers(
        _device: vk::VkDevice,
        _command_pool: vk::VkCommandPool,
        command_buffer_count: u32,
        _command_buffers: *const vk::VkCommandBuffer,
    ) {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_command_pools >= 1);

        assert!(command_buffer_count as usize <= fake_vulkan.n_command_buffers);

        fake_vulkan.n_command_buffers -= command_buffer_count as usize;
    }

    extern "C" fn create_fence(
        _device: vk::VkDevice,
        _create_info: *const vk::VkFenceCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        fence_out: *mut vk::VkFence,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.n_fences += 1;

        unsafe {
            *fence_out = 1usize as vk::VkFence;
        }

        fake_vulkan.next_result("vkCreateFence")
    }

    extern "C" fn destroy_fence(
        _device: vk::VkDevice,
        _fence: vk::VkFence,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        assert!(fake_vulkan.n_fences >= 1);

        fake_vulkan.n_fences -= 1;
    }

    extern "C" fn enumerate_instance_version(
        api_version: *mut u32
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();
        unsafe { *api_version = requirements::make_version(1, 1, 0) }
        fake_vulkan.next_result("vkEnumerateInstanceVersion")
    }
}

impl Drop for FakeVulkan {
    fn drop(&mut self) {
        let old_value = CURRENT_FAKE_VULKAN.with(|f| f.replace(None));

        if !std::thread::panicking() {
            assert_eq!(self.n_instances, 0);
            assert_eq!(self.n_devices, 0);
            assert_eq!(self.n_command_pools, 0);
            assert_eq!(self.n_command_buffers, 0);
            assert_eq!(self.n_fences, 0);

            // There should only be one FakeVulkan at a time so the
            // one we just dropped should be the one that was set for
            // the current thread.
            assert_eq!(old_value.unwrap(), self as *mut FakeVulkan);
        }
    }
}
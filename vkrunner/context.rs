// vkrunner
//
// Copyright (C) 2013, 2014, 2015, 2017, 2023 Neil Roberts
// Copyright (C) 2018 Intel Corporation
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

use crate::vk;
use crate::requirements::{Requirements, CheckError};
use crate::vulkan_funcs;
use crate::util::env_var_as_boolean;
use std::ffi::{c_char, c_void, CStr};
use std::fmt;
use std::fmt::Write;
use std::ptr;

/// Struct containing the VkDevice and accessories such as a
/// VkCommandPool, VkQueue and the function pointers from
/// [vulkan_funcs].
#[derive(Debug)]
pub struct Context {
    // These three need to be in this order so that they will be
    // dropped in the correct order.
    device_pair: DevicePair,
    instance_pair: InstancePair,
    // This isn’t read anywhere but we want to keep it alive for the
    // duration of the Context so that the library won’t be unloaded.
    _vklib: Option<Box<vulkan_funcs::Library>>,

    physical_device: vk::VkPhysicalDevice,

    memory_properties: vk::VkPhysicalDeviceMemoryProperties,

    command_pool: vk::VkCommandPool,
    command_buffer: vk::VkCommandBuffer,
    fence: vk::VkFence,

    queue: vk::VkQueue,

    always_flush_memory: bool,
}

/// Error returned by [Context::new]
#[derive(Debug)]
pub enum ContextError {
    /// An unexpected failure occured such as not being able to find
    /// the Vulkan library. This should result in a test failure.
    Failure(String),
    /// The requirements couldn’t be satisfied. This should result in
    /// the test being skipped.
    Incompatible(String),
}

impl From<String> for ContextError {
    fn from(error: String) -> ContextError {
        ContextError::Failure(error)
    }
}

impl fmt::Display for ContextError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let message = match self {
            ContextError::Failure(m) => m,
            ContextError::Incompatible(m) => m,
        };
        write!(f, "{}", message)
    }
}

struct GetInstanceProcClosure<'a> {
    vklib: &'a vulkan_funcs::Library,
    vk_instance: vk::VkInstance,
}

extern "C" fn get_instance_proc(
    func_name: *const c_char,
    user_data: *const c_void,
) -> *const c_void {
    unsafe {
        let data: &GetInstanceProcClosure = &*user_data.cast();
        let vklib = data.vklib;
        std::mem::transmute(
            vklib.vkGetInstanceProcAddr.unwrap()(
                data.vk_instance,
                func_name.cast()
            )
        )
    }
}

// ext is a zero-terminated byte array, as one of the constants in
// vulkan_bindings.
fn check_instance_extension(
    vklib: &vulkan_funcs::Library,
    ext: &[u8],
) -> Result<(), ContextError> {
    let mut count: u32 = 0;

    let res = unsafe {
        vklib.vkEnumerateInstanceExtensionProperties.unwrap()(
            ptr::null(), // layerName
            ptr::addr_of_mut!(count),
            ptr::null_mut(), // props
        )
    };

    if res != vk::VK_SUCCESS {
        return Err(ContextError::Failure(
            "vkEnumerateInstanceExtensionProperties failed".to_string(),
        ));
    }

    let mut props = Vec::<vk::VkExtensionProperties>::new();
    props.resize_with(count as usize, Default::default);

    let res = unsafe {
        vklib.vkEnumerateInstanceExtensionProperties.unwrap()(
            ptr::null(), // layerName
            ptr::addr_of_mut!(count),
            props.as_mut_ptr(),
        )
    };

    if res != vk::VK_SUCCESS {
        return Err(ContextError::Failure(
            "vkEnumerateInstanceExtensionProperties failed".to_string(),
        ));
    }

    'prop_loop: for prop in props.iter() {
        assert!(ext.len() <= prop.extensionName.len());

        // Can’t just compare the slices because one is i8 and the
        // other is u8. ext should include the null terminator so this
        // will check for null too.

        for (i, &c) in ext.iter().enumerate() {
            if c as c_char != prop.extensionName[i] {
                continue 'prop_loop;
            }
        }

        // If we made it here then it’s the same extension name
        return Ok(());
    }

    let ext_name = CStr::from_bytes_with_nul(ext).unwrap();

    Err(ContextError::Incompatible(
        format!("Missing instance extension: {}", ext_name.to_string_lossy())
    ))
}

// Struct that contains a VkInstance and its function pointers from
// vulkan_funcs. This is needed so that it can have a drop
// implementation that uses the function pointers to free the
// instance.
#[derive(Debug)]
struct InstancePair {
    is_external: bool,
    vk_instance: vk::VkInstance,
    vkinst: Box<vulkan_funcs::Instance>,
}

impl InstancePair {
    fn new(
        vklib: &vulkan_funcs::Library,
        requirements: &Requirements
    ) -> Result<InstancePair, ContextError> {
        let application_info = vk::VkApplicationInfo {
            sType: vk::VK_STRUCTURE_TYPE_APPLICATION_INFO,
            pNext: ptr::null(),
            pApplicationName: "vkrunner\0".as_ptr().cast(),
            applicationVersion: 0,
            pEngineName: ptr::null(),
            engineVersion: 0,
            apiVersion: requirements.version(),
        };

        let mut instance_create_info = vk::VkInstanceCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            pApplicationInfo: ptr::addr_of!(application_info),
            enabledLayerCount: 0,
            ppEnabledLayerNames: ptr::null(),
            enabledExtensionCount: 0,
            ppEnabledExtensionNames: ptr::null(),
        };

        let ext = vk::VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
        let structures = requirements.c_structures();

        let mut enabled_extensions = Vec::<*const c_char>::new();

        if structures.is_some() {
            check_instance_extension(vklib, ext)?;
            enabled_extensions.push(ext.as_ptr().cast());
        }

        instance_create_info.enabledExtensionCount =
            enabled_extensions.len() as u32;
        instance_create_info.ppEnabledExtensionNames =
            enabled_extensions.as_ptr();

        let mut vk_instance = ptr::null_mut();

        let res = unsafe {
            vklib.vkCreateInstance.unwrap()(
                ptr::addr_of!(instance_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(vk_instance),
            )
        };

        match res {
            vk::VK_ERROR_INCOMPATIBLE_DRIVER => Err(ContextError::Incompatible(
                "vkCreateInstance reported VK_ERROR_INCOMPATIBLE_DRIVER"
                    .to_string()
            )),
            vk::VK_SUCCESS => {
                let vkinst = unsafe {
                    let closure = GetInstanceProcClosure {
                        vklib,
                        vk_instance,
                    };

                    Box::new(vulkan_funcs::Instance::new(
                        get_instance_proc,
                        ptr::addr_of!(closure).cast(),
                    ))
                };

                Ok(InstancePair {
                    is_external: false,
                    vk_instance,
                    vkinst,
                })
            },
            _ => Err(ContextError::Failure(
                "vkCreateInstance failed".to_string()
            )),
        }
    }

    fn new_external(
        get_instance_proc_cb: vulkan_funcs::GetInstanceProcFunc,
        user_data: *const c_void,
    ) -> InstancePair {
        InstancePair {
            is_external: true,
            vk_instance: ptr::null_mut(),
            vkinst: unsafe {
                Box::new(vulkan_funcs::Instance::new(
                    get_instance_proc_cb,
                    user_data,
                ))
            },
        }
    }
}

impl Drop for InstancePair {
    fn drop(&mut self) {
        if !self.is_external {
            unsafe {
                self.vkinst.vkDestroyInstance.unwrap()(
                    self.vk_instance,
                    ptr::null(), // allocator
                );
            }
        }
    }
}

// Struct that contains a VkDevice and its function pointers from
// vulkan_funcs. This is needed so that it can have a drop
// implementation that uses the function pointers to free the device.
#[derive(Debug)]
struct DevicePair {
    is_external: bool,
    device: vk::VkDevice,
    vkdev: Box<vulkan_funcs::Device>,
}

impl DevicePair {
    fn new(
        instance_pair: &InstancePair,
        requirements: &Requirements,
        physical_device: vk::VkPhysicalDevice,
        queue_family: u32,
    ) -> Result<DevicePair, ContextError> {
        let structures = requirements.c_structures();
        let base_features = requirements.c_base_features();
        let extensions = requirements.c_extensions();

        let queue_priorities = [1.0f32];

        let queue_create_info = vk::VkDeviceQueueCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            queueFamilyIndex: queue_family,
            queueCount: 1,
            pQueuePriorities: queue_priorities.as_ptr(),
        };

        let device_create_info = vk::VkDeviceCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            pNext: structures
                .and_then(|s| Some(s.as_ptr().cast()))
                .unwrap_or(ptr::null()),
            flags: 0,
            queueCreateInfoCount: 1,
            pQueueCreateInfos: ptr::addr_of!(queue_create_info),
            enabledLayerCount: 0,
            ppEnabledLayerNames: ptr::null(),
            enabledExtensionCount: extensions.len() as u32,
            ppEnabledExtensionNames: if extensions.len() > 0 {
                extensions.as_ptr().cast()
            } else {
                ptr::null()
            },
            pEnabledFeatures: base_features,
        };

        let mut device = ptr::null_mut();

        let res = unsafe {
            instance_pair.vkinst.vkCreateDevice.unwrap()(
                physical_device,
                ptr::addr_of!(device_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(device),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(DevicePair {
                is_external: false,
                device,
                vkdev: Box::new(vulkan_funcs::Device::new(
                    &instance_pair.vkinst,
                    device,
                )),
            })
        } else {
            Err(ContextError::Failure("vkCreateDevice failed".to_string()))
        }
    }

    fn new_external(
        instance_pair: &InstancePair,
        device: vk::VkDevice,
    ) -> DevicePair {
        DevicePair {
            is_external: true,
            device,
            vkdev: Box::new(vulkan_funcs::Device::new(
                &instance_pair.vkinst,
                device,
            )),
        }
    }
}

impl Drop for DevicePair {
    fn drop(&mut self) {
        if !self.is_external {
            unsafe {
                self.vkdev.vkDestroyDevice.unwrap()(
                    self.device,
                    ptr::null(), // allocator
                );
            }
        }
    }
}

fn free_resources(
    device_pair: &DevicePair,
    mut command_pool: Option<vk::VkCommandPool>,
    mut command_buffer: Option<vk::VkCommandBuffer>,
    mut fence: Option<vk::VkFence>,
) {
    unsafe {
        if let Some(fence) = fence.take() {
            device_pair.vkdev.vkDestroyFence.unwrap()(
                device_pair.device,
                fence,
                ptr::null() // allocator
            );
        }

        if let Some(mut command_buffer) = command_buffer.take() {
            device_pair.vkdev.vkFreeCommandBuffers.unwrap()(
                device_pair.device,
                command_pool.unwrap(),
                1, // commandBufferCount
                ptr::addr_of_mut!(command_buffer),
            );
        }

        if let Some(command_pool) = command_pool.take() {
            device_pair.vkdev.vkDestroyCommandPool.unwrap()(
                device_pair.device,
                command_pool,
                ptr::null() // allocator
            );
        }
    }
}

// This is a helper struct for creating the rest of the context
// resources. Its members are optional so that it can handle
// destruction if an error occurs between creating one of the
// resources.
struct DeviceResources<'a> {
    device_pair: &'a DevicePair,

    command_pool: Option<vk::VkCommandPool>,
    command_buffer: Option<vk::VkCommandBuffer>,
    fence: Option<vk::VkFence>,
}

impl<'a> DeviceResources<'a> {
    fn create_command_pool(
        &mut self,
        queue_family: u32,
    ) -> Result<(), ContextError> {
        let command_pool_create_info = vk::VkCommandPoolCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            pNext: ptr::null(),
            flags: vk::VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            queueFamilyIndex: queue_family,
        };
        let mut command_pool = ptr::null_mut();
        let res = unsafe {
            self.device_pair.vkdev.vkCreateCommandPool.unwrap()(
                self.device_pair.device,
                ptr::addr_of!(command_pool_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(command_pool),
            )
        };

        if res != vk::VK_SUCCESS {
            return Err(ContextError::Failure(
                "vkCreateCommandPool failed".to_string()
            ));
        }

        self.command_pool = Some(command_pool);

        Ok(())
    }

    fn allocate_command_buffer(&mut self) -> Result<(), ContextError> {
        let command_buffer_allocate_info = vk::VkCommandBufferAllocateInfo {
            sType: vk::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            pNext: ptr::null(),
            commandPool: self.command_pool.unwrap(),
            level: vk::VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            commandBufferCount: 1,
        };
        let mut command_buffer = ptr::null_mut();
        let res = unsafe {
            self.device_pair.vkdev.vkAllocateCommandBuffers.unwrap()(
                self.device_pair.device,
                ptr::addr_of!(command_buffer_allocate_info),
                ptr::addr_of_mut!(command_buffer),
            )
        };

        if res != vk::VK_SUCCESS {
            return Err(ContextError::Failure(
                "vkCommandBufferAllocate failed".to_string()
            ));
        }

        self.command_buffer = Some(command_buffer);

        Ok(())
    }

    fn create_fence(&mut self) -> Result<(), ContextError> {
        let fence_create_info = vk::VkFenceCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
        };
        let mut fence = ptr::null_mut();
        let res = unsafe {
            self.device_pair.vkdev.vkCreateFence.unwrap()(
                self.device_pair.device,
                ptr::addr_of!(fence_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(fence),
            )
        };
        if res != vk::VK_SUCCESS {
            return Err(ContextError::Failure(
                "vkCreateFence failed".to_string()
            ));
        }

        self.fence = Some(fence);

        Ok(())
    }

    fn new(
        device_pair: &'a DevicePair,
        queue_family: u32,
    ) -> Result<DeviceResources<'a>, ContextError> {
        let mut device_resources = DeviceResources {
            device_pair,
            command_pool: None,
            command_buffer: None,
            fence: None,
        };

        device_resources.create_command_pool(queue_family)?;
        device_resources.allocate_command_buffer()?;
        device_resources.create_fence()?;

        Ok(device_resources)
    }
}

impl<'a> Drop for DeviceResources<'a> {
    fn drop(&mut self) {
        free_resources(
            self.device_pair,
            self.command_pool.take(),
            self.command_buffer.take(),
            self.fence.take(),
        );
    }
}

fn combine_device_errors(mut errors: Vec<ContextError>) -> ContextError {
    let n_errors = errors.len();

    match n_errors {
        0 => {
            ContextError::Incompatible(
                "The Vulkan instance reported zero drivers"
                    .to_string()
            )
        },
        1 => errors.pop().unwrap(),
        _ => {
            // If all of the errors were “Failure” then we’ll return
            // failure overall, otherwise we’ll return “Incompatible”.
            let mut all_failures = true;
            let mut message = String::new();

            for (i, error) in errors.into_iter().enumerate() {
                if i > 0 {
                    message.push('\n');
                }

                write!(&mut message, "{}: {}", i, &error).unwrap();

                match error {
                    ContextError::Incompatible(_) => all_failures = false,
                    ContextError::Failure(_) => (),
                }
            }

            if all_failures {
                ContextError::Failure(message)
            } else {
                ContextError::Incompatible(message)
            }
        }
    }
}

fn find_queue_family(
    instance_pair: &InstancePair,
    physical_device: vk::VkPhysicalDevice,
) -> Result<u32, ContextError> {
    let vkinst = instance_pair.vkinst.as_ref();

    let mut count = 0u32;

    unsafe {
        vkinst.vkGetPhysicalDeviceQueueFamilyProperties.unwrap()(
            physical_device,
            ptr::addr_of_mut!(count),
            ptr::null_mut(), // queues
        );
    }

    let mut queues = Vec::<vk::VkQueueFamilyProperties>::new();
    queues.resize_with(count as usize, Default::default);

    unsafe {
        vkinst.vkGetPhysicalDeviceQueueFamilyProperties.unwrap()(
            physical_device,
            ptr::addr_of_mut!(count),
            queues.as_mut_ptr(),
        );
    }

    for (i, queue) in queues.into_iter().enumerate() {
        if queue.queueFlags & vk::VK_QUEUE_GRAPHICS_BIT != 0 &&
            queue.queueCount >= 1
        {
            return Ok(i as u32);
        }
    }

    Err(ContextError::Incompatible(
        "Device has no graphics queue family".to_string()
    ))
}

// Checks whether the chosen physical device can be used and has an
// appropriate queue family. If so, it returns the index of the queue
// family, otherwise an error.
fn check_physical_device(
    vklib: &vulkan_funcs::Library,
    instance_pair: &InstancePair,
    requirements: &Requirements,
    physical_device: vk::VkPhysicalDevice,
) -> Result<u32, ContextError> {
    match requirements.check(
        vklib,
        instance_pair.vkinst.as_ref(),
        instance_pair.vk_instance,
        physical_device
    ) {
        Ok(()) => find_queue_family(instance_pair, physical_device),
        Err(CheckError::Invalid(s)) => Err(ContextError::Failure(s)),
        Err(e) => Err(ContextError::Incompatible(e.to_string())),
    }
}

// Checks all of the physical devices advertised by the instance. If
// it can found one matching the requirements then it returns it along
// with a queue family index of the first graphics queue. Otherwise
// returns an error.
fn find_physical_device(
    vklib: &vulkan_funcs::Library,
    instance_pair: &InstancePair,
    requirements: &Requirements,
    device_id: Option<usize>,
) -> Result<(vk::VkPhysicalDevice, u32), ContextError> {
    let vkinst = instance_pair.vkinst.as_ref();

    let mut count = 0u32;

    let res = unsafe {
        vkinst.vkEnumeratePhysicalDevices.unwrap()(
            instance_pair.vk_instance,
            ptr::addr_of_mut!(count),
            ptr::null_mut(), // devices
        )
    };

    if res != vk::VK_SUCCESS {
        return Err(ContextError::Failure(
            "vkEnumeratePhysicalDevices failed".to_string()
        ));
    }

    let mut devices = Vec::<vk::VkPhysicalDevice>::new();
    devices.resize(count as usize, ptr::null_mut());

    let res = unsafe {
        vkinst.vkEnumeratePhysicalDevices.unwrap()(
            instance_pair.vk_instance,
            ptr::addr_of_mut!(count),
            devices.as_mut_ptr(),
        )
    };

    if res != vk::VK_SUCCESS {
        return Err(ContextError::Failure(
            "vkEnumeratePhysicalDevices failed".to_string()
        ));
    }

    if let Some(device_id) = device_id {
        if device_id >= count as usize {
            return Err(ContextError::Failure(format!(
                "Device {} was selected but the Vulkan instance only reported \
                 {} device{}.",
                device_id,
                count,
                if count == 1 { "" } else { "s" },
            )));
        } else {
            return match check_physical_device(
                vklib,
                instance_pair,
                requirements,
                devices[device_id]
            ) {
                Ok(queue_family) => Ok((devices[device_id], queue_family)),
                Err(e) => Err(e),
            };
        }
    }

    // Collect all of the errors into an array so we can report all of
    // them in a combined error message if we can’t find a good
    // device.
    let mut errors = Vec::<ContextError>::new();

    for device in devices {
        match check_physical_device(
            vklib,
            instance_pair,
            requirements,
            device
        ) {
            Ok(queue_family) => return Ok((device, queue_family)),
            Err(e) => {
                errors.push(e);
            },
        }
    }

    Err(combine_device_errors(errors))
}

impl Context {
    fn new_internal(
        vklib: Option<Box<vulkan_funcs::Library>>,
        instance_pair: InstancePair,
        physical_device: vk::VkPhysicalDevice,
        queue_family: u32,
        device_pair: DevicePair,
    ) -> Result<Context, ContextError> {
        let mut memory_properties =
            vk::VkPhysicalDeviceMemoryProperties::default();

        unsafe {
            instance_pair.vkinst.vkGetPhysicalDeviceMemoryProperties.unwrap()(
                physical_device,
                ptr::addr_of_mut!(memory_properties),
            );
        }

        let mut queue = ptr::null_mut();

        unsafe {
            device_pair.vkdev.vkGetDeviceQueue.unwrap()(
                device_pair.device,
                queue_family,
                0, // queueIndex
                ptr::addr_of_mut!(queue)
            );
        }

        let mut resources = DeviceResources::new(
            &device_pair,
            queue_family,
        )?;

        let command_pool = resources.command_pool.take().unwrap();
        let command_buffer = resources.command_buffer.take().unwrap();
        let fence = resources.fence.take().unwrap();
        drop(resources);

        Ok(Context {
            _vklib: vklib,
            instance_pair,
            device_pair,
            physical_device,
            memory_properties,
            command_pool,
            command_buffer,
            fence,
            queue,
            always_flush_memory: env_var_as_boolean(
                "VKRUNNER_ALWAYS_FLUSH_MEMORY",
                false
            )
        })
    }

    /// Constructs a Context or returns [ContextError::Failure] if an
    /// error occurred while constructing it or
    /// [ContextError::Incompatible] if the requirements couldn’t be
    /// met. `device_id` can optionally be set to limit the device
    /// selection to an index in the list returned by
    /// `vkEnumeratePhysicalDevices`.
    pub fn new(
        requirements: &Requirements,
        device_id: Option<usize>,
    ) -> Result<Context, ContextError> {
        let vklib = Box::new(vulkan_funcs::Library::new()?);

        let instance_pair = InstancePair::new(vklib.as_ref(), requirements)?;

        let (physical_device, queue_family) = find_physical_device(
            vklib.as_ref(),
            &instance_pair,
            requirements,
            device_id,
        )?;

        let device_pair = DevicePair::new(
            &instance_pair,
            requirements,
            physical_device,
            queue_family,
        )?;

        Context::new_internal(
            Some(vklib),
            instance_pair,
            physical_device,
            queue_family,
            device_pair
        )
    }

    /// Constructs a Context from a VkDevice created externally. The
    /// VkDevice won’t be freed when the context is dropped. It is the
    /// caller’s responsibility to keep the device alive during the
    /// lifetime of the Context. It also needs to ensure that any
    /// features and extensions that might be used during script
    /// execution were enabled when the device was created.
    ///
    /// `get_instance_proc_cb` will be called to get the
    /// instance-level functions that the context needs. The rest of
    /// the functions will be retrieved using the
    /// `vkGetDeviceProcAddr` function that that returns. `user_data`
    /// will be passed to the function.
    pub fn new_with_device(
        get_instance_proc_cb: vulkan_funcs::GetInstanceProcFunc,
        user_data: *const c_void,
        physical_device: vk::VkPhysicalDevice,
        queue_family: u32,
        device: vk::VkDevice
    ) -> Result<Context, ContextError> {
        let instance_pair = InstancePair::new_external(
            get_instance_proc_cb,
            user_data,
        );

        let device_pair = DevicePair::new_external(
            &instance_pair,
            device,
        );

        Context::new_internal(
            None, // vklib
            instance_pair,
            physical_device,
            queue_family,
            device_pair,
        )
    }

    /// Get the instance function pointers
    #[inline]
    pub fn instance(&self) -> &vulkan_funcs::Instance {
        &self.instance_pair.vkinst
    }

    /// Get the VkDevice handle
    #[inline]
    pub fn vk_device(&self) -> vk::VkDevice {
        self.device_pair.device
    }

    /// Get the device function pointers
    #[inline]
    pub fn device(&self) -> &vulkan_funcs::Device {
        &self.device_pair.vkdev
    }

    /// Get the physical device that was used to create this context.
    #[inline]
    pub fn physical_device(&self) -> vk::VkPhysicalDevice {
        self.physical_device
    }

    /// Get the memory properties struct for the physical device. This
    /// is queried from the physical device once when the context is
    /// constructed and cached for later use so this method is very
    /// cheap.
    #[inline]
    pub fn memory_properties(&self) -> &vk::VkPhysicalDeviceMemoryProperties {
        &self.memory_properties
    }

    /// Get the single shared command buffer that is associated with
    /// the context.
    #[inline]
    pub fn command_buffer(&self) -> vk::VkCommandBuffer {
        self.command_buffer
    }

    /// Get the single shared fence that is associated with the
    /// context.
    #[inline]
    pub fn fence(&self) -> vk::VkFence {
        self.fence
    }

    /// Get the queue chosen for this context.
    #[inline]
    pub fn queue(&self) -> vk::VkQueue {
        self.queue
    }

    /// Returns whether the memory should always be flushed regardless
    /// of whether the `VK_MEMORY_PROPERTY_HOST_COHERENT` bit is set
    /// in the [memory_properties](Context::memory_properties). This
    /// is mainly for testing purposes and can be enabled by setting
    /// the `VKRUNNER_ALWAYS_FLUSH_MEMORY` environment variable to
    /// `true`.
    #[inline]
    pub fn always_flush_memory(&self) -> bool {
        self.always_flush_memory
    }

    /// Returns whether the context was created from an external
    /// provided via the API, ie with [Context::new_with_device]
    /// instead of [Context::new].
    #[inline]
    pub fn is_external(&self) -> bool {
        self.device_pair.is_external
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        free_resources(
            &self.device_pair,
            Some(self.command_pool),
            Some(self.command_buffer),
            Some(self.fence),
        );
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, HandleType};
    use crate::env_var_test::EnvVarLock;

    #[test]
    fn base() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.physical_devices[0].memory_properties.memoryTypeCount = 3;

        fake_vulkan.set_override();
        let context = Context::new(&Requirements::new(), None).unwrap();

        assert!(!context.instance_pair.vk_instance.is_null());
        assert!(context.instance().vkCreateDevice.is_some());
        assert!(!context.vk_device().is_null());
        assert!(context.device().vkCreateCommandPool.is_some());
        assert_eq!(
            context.physical_device(),
            fake_vulkan.index_to_physical_device(0)
        );
        assert_eq!(context.memory_properties().memoryTypeCount, 3);
        assert!(!context.command_buffer().is_null());
        assert!(!context.fence().is_null());
        assert_eq!(FakeVulkan::unmake_queue(context.queue()), (0, 0));
        assert!(!context.is_external());
    }

    #[test]
    fn no_devices() {
        let fake_vulkan = FakeVulkan::new();
        fake_vulkan.set_override();
        let err = Context::new(&Requirements::new(), None).unwrap_err();
        assert_eq!(
            &err.to_string(),
            "The Vulkan instance reported zero drivers"
        );
    }

    #[test]
    fn check_instance_extension() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        let mut reqs = Requirements::new();
        // Add a requirement so that c_structures won’t be NULL
        reqs.add("shaderInt8");

        // Make vkEnumerateInstanceExtensionProperties fail
        fake_vulkan.queue_result(
            "vkEnumerateInstanceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );
        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "vkEnumerateInstanceExtensionProperties failed",
        );

        // Make it fail the second time too
        fake_vulkan.queue_result(
            "vkEnumerateInstanceExtensionProperties".to_string(),
            vk::VK_SUCCESS,
        );
        fake_vulkan.queue_result(
            "vkEnumerateInstanceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );
        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "vkEnumerateInstanceExtensionProperties failed",
        );

        // Add an extension name with the same length but different characters
        fake_vulkan.add_instance_extension(
            "VK_KHR_get_physical_device_properties3"
        );

        // Add a shorter extension name
        fake_vulkan.add_instance_extension("VK_KHR");
        // And a longer one
        fake_vulkan.add_instance_extension(
            "VK_KHR_get_physical_device_properties23"
        );

        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "Missing instance extension: \
             VK_KHR_get_physical_device_properties2",
        );
    }

    #[test]
    fn extension_feature() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.add_instance_extension(
            "VK_KHR_get_physical_device_properties2"
        );

        let mut reqs = Requirements::new();

        reqs.add("multiview");

        fake_vulkan.set_override();
        assert_eq!(
            Context::new(&mut reqs, None).unwrap_err().to_string(),
            "Missing required extension: VK_KHR_multiview",
        );

        fake_vulkan.physical_devices[0].add_extension("VK_KHR_multiview");
        fake_vulkan.physical_devices[0].multiview.multiview = vk::VK_TRUE;

        fake_vulkan.set_override();
        Context::new(&mut reqs, None).unwrap();
    }

    #[test]
    fn multiple_mismatches() {
        let mut fake_vulkan = FakeVulkan::new();

        let mut reqs = Requirements::new();
        reqs.add("madeup_extension");

        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.physical_devices[0].properties.apiVersion =
            crate::requirements::make_version(0, 1, 0);
        fake_vulkan.physical_devices[0].add_extension("madeup_extension");

        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.physical_devices[1].queue_families.clear();
        fake_vulkan.physical_devices[1].add_extension("madeup_extension");

        fake_vulkan.physical_devices.push(Default::default());

        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "0: Vulkan API version 1.0.0 required but the driver \
             reported 0.1.0\n\
             1: Device has no graphics queue family\n\
             2: Missing required extension: madeup_extension",
        );
        assert!(matches!(err, ContextError::Incompatible(_)));

        // Try making them one of them fail
        fake_vulkan.queue_result(
            "vkEnumerateDeviceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );

        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "0: vkEnumerateDeviceExtensionProperties failed\n\
             1: Device has no graphics queue family\n\
             2: Missing required extension: madeup_extension",
        );
        assert!(matches!(err, ContextError::Incompatible(_)));

        // Try making all of them fail
        fake_vulkan.physical_devices[0] = Default::default();
        fake_vulkan.physical_devices[1] = Default::default();
        fake_vulkan.queue_result(
            "vkEnumerateDeviceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );
        fake_vulkan.queue_result(
            "vkEnumerateDeviceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );
        fake_vulkan.queue_result(
            "vkEnumerateDeviceExtensionProperties".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );

        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, None).unwrap_err();
        assert_eq!(
            err.to_string(),
            "0: vkEnumerateDeviceExtensionProperties failed\n\
             1: vkEnumerateDeviceExtensionProperties failed\n\
             2: vkEnumerateDeviceExtensionProperties failed",
        );
        assert!(matches!(err, ContextError::Failure(_)));

        // Finally add a physical device that will succeed
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.physical_devices[3].add_extension("madeup_extension");
        fake_vulkan.set_override();
        let context = Context::new(&mut reqs, None).unwrap();
        assert_eq!(
            fake_vulkan.physical_device_to_index(context.physical_device),
            3,
        );
    }

    #[test]
    fn device_id() {
        let mut fake_vulkan = FakeVulkan::new();
        for _ in 0..3 {
            fake_vulkan.physical_devices.push(Default::default());
        }

        let mut reqs = Requirements::new();

        // Try selecting each device
        for i in 0..3 {
            fake_vulkan.set_override();
            let context = Context::new(&mut reqs, Some(i)).unwrap();
            assert_eq!(
                fake_vulkan.physical_device_to_index(context.physical_device),
                i,
            );
        }

        // Try selecting a non-existant device
        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, Some(3)).unwrap_err();
        assert_eq!(
            err.to_string(),
            "Device 3 was selected but the Vulkan instance only reported 3 \
             devices."
        );

        fake_vulkan.physical_devices.truncate(1);
        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, Some(3)).unwrap_err();
        assert_eq!(
            err.to_string(),
            "Device 3 was selected but the Vulkan instance only reported 1 \
             device."
        );

        // Try a failure
        reqs.add("madeup_extension");
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.set_override();
        let err = Context::new(&mut reqs, Some(0)).unwrap_err();
        assert_eq!(
            err.to_string(),
            "Missing required extension: madeup_extension",
        );
    }

    #[test]
    fn always_flush_memory_false() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        let mut reqs = Requirements::new();

        let _env_var_lock = EnvVarLock::new(&[
            ("VKRUNNER_ALWAYS_FLUSH_MEMORY", "false"),
        ]);

        fake_vulkan.set_override();
        let context = Context::new(&mut reqs, None).unwrap();
        assert_eq!(context.always_flush_memory(), false);
    }

    #[test]
    fn always_flush_memory_true() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        let mut reqs = Requirements::new();

        let _env_var_lock = EnvVarLock::new(&[
            ("VKRUNNER_ALWAYS_FLUSH_MEMORY", "true"),
        ]);

        fake_vulkan.set_override();
        let context = Context::new(&mut reqs, None).unwrap();
        assert_eq!(context.always_flush_memory(), true);
    }

    #[test]
    fn new_with_device() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        let device = fake_vulkan.add_handle(HandleType::Device);

        extern "C" fn no_create_device(
            _physical_device: vk::VkPhysicalDevice,
            _create_info: *const vk::VkDeviceCreateInfo,
            _allocator: *const vk::VkAllocationCallbacks,
            _device_out: *mut vk::VkDevice,
        ) -> vk::VkResult {
            unreachable!(
                "vkCreateDevice shouldn’t be called for an external \
                 device"
            );
        }

        extern "C" fn no_destroy_device(
            _device: vk::VkDevice,
            _allocator: *const vk::VkAllocationCallbacks
        ) {
            unreachable!(
                "vkDestroyDevice shouldn’t be called for an external \
                 device"
            );
        }

        extern "C" fn no_destroy_instance(
            _instance: vk::VkInstance,
            _allocator: *const vk::VkAllocationCallbacks
        ) {
            unreachable!(
                "vkDestroyInstance shouldn’t be called for an external \
                 device"
            );
        }

        extern "C" fn get_device_proc_addr(
            _device: vk::VkDevice,
            name: *const c_char,
        ) -> vk::PFN_vkVoidFunction {
            unsafe {
                std::mem::transmute(get_instance_proc_cb(
                    name.cast(),
                    (FakeVulkan::current() as *mut FakeVulkan).cast(),
                ))
            }
        }

        extern "C" fn get_instance_proc_cb(
            func_name: *const c_char,
            user_data: *const c_void,
        ) -> *const c_void {
            let name = unsafe {
                CStr::from_ptr(func_name.cast()).to_str().unwrap()
            };

            match name {
                "vkGetDeviceProcAddr" => unsafe {
                    std::mem::transmute::<vk::PFN_vkGetDeviceProcAddr, _>(
                        Some(get_device_proc_addr)
                    )
                },
                "vkDestroyInstance" => unsafe {
                    std::mem::transmute::<vk::PFN_vkDestroyInstance, _>(
                        Some(no_destroy_instance)
                    )
                },
                "vkCreateDevice" => unsafe {
                    std::mem::transmute::<vk::PFN_vkCreateDevice, _>(
                        Some(no_create_device)
                    )
                },
                "vkDestroyDevice" => unsafe {
                    std::mem::transmute::<vk::PFN_vkDestroyDevice, _>(
                        Some(no_destroy_device)
                    )
                },
                _ => unsafe {
                    let fake_vulkan = &*(user_data.cast::<FakeVulkan>());
                    std::mem::transmute(
                        fake_vulkan.get_function(func_name.cast())
                    )
                },
            }
        }

        let context = Context::new_with_device(
            get_instance_proc_cb,
            (fake_vulkan.as_ref() as *const FakeVulkan).cast(),
            fake_vulkan.index_to_physical_device(0),
            3, // queue_family
            device,
        ).unwrap();

        assert_eq!(context.instance_pair.vk_instance, ptr::null_mut());
        assert_eq!(
            fake_vulkan.physical_device_to_index(context.physical_device()),
            0
        );
        assert_eq!(context.vk_device(), device);
        assert!(context.device_pair.is_external);
        assert!(context.instance_pair.is_external);

        drop(context);

        fake_vulkan.get_handle_mut(device).freed = true;
    }
}

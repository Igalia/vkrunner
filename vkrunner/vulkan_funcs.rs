// vkrunner
//
// Copyright (C) 2016, 2023 Neil Roberts
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
use std::ffi::{c_void, c_int, c_char, CString};

pub type GetInstanceProcFunc = unsafe extern "C" fn(
    func_name: *const u8,
    user_data: *const c_void,
) -> *const c_void;

include!{"vulkan_funcs_data.rs"}

impl Library {
    pub fn new() -> Result<Library, String> {
        let lib: *const c_void;
        let get_instance_proc_addr: vk::PFN_vkGetInstanceProcAddr;

        #[cfg(unix)]
        {
            extern "C" {
                fn dlopen(name: *const c_char, flags: c_int) -> *const c_void;
                fn dlsym(
                    lib: *const c_void,
                    name: *const c_char
                ) -> *const c_void;
            }

            let lib_name;

            #[cfg(target_os = "android")]
            {
                lib_name = "libvulkan.so";
            }
            #[cfg(not(target_os = "android"))]
            {
                lib_name = "libvulkan.so.1";
            }

            let lib_name_c = CString::new(lib_name).unwrap();

            lib = unsafe {
                dlopen(lib_name_c.as_ptr(), 1)
            };

            if lib.is_null() {
                return Err(format!("Error opening {}", lib_name));
            }

            get_instance_proc_addr = unsafe {
                std::mem::transmute(dlsym(
                    lib,
                    "vkGetInstanceProcAddr\0".as_ptr().cast()
                ))
            };
        }
        #[cfg(not(unix))]
        {
            todo!("library opening on non-Unix platforms is not yet \
                   implemented");
        }

        Ok(Library {
            lib_vulkan: lib,
            vkGetInstanceProcAddr: get_instance_proc_addr,
            vkCreateInstance: unsafe {
                std::mem::transmute(get_instance_proc_addr.unwrap()(
                    std::ptr::null_mut(),
                    "vkCreateInstance\0".as_ptr().cast()
                ))
            },
            vkEnumerateInstanceExtensionProperties: unsafe {
                std::mem::transmute(get_instance_proc_addr.unwrap()(
                    std::ptr::null_mut(),
                    "vkEnumerateInstanceExtensionProperties\0".as_ptr().cast()
                ))
            }
        })
    }
}

impl Drop for Library {
    fn drop(&mut self) {
        #[cfg(unix)]
        {
            extern "C" {
                fn dlclose(lib: *const c_void) -> *const c_void;
            }

            if !self.lib_vulkan.is_null() {
                unsafe { dlclose(self.lib_vulkan) };
            }
        }

        #[cfg(not(unix))]
        {
            todo!("library closing on non-Unix platforms is not yet \
                   implemented");
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_vk_library_new(
    config: *const c_void
) -> *const Library {
    extern "C" {
        fn vr_error_message_string(config: *const c_void, str: *const i8);
    }

    match Library::new() {
        Ok(library) => Box::into_raw(Box::new(library)),
        Err(s) => {
            let message = CString::new(s).unwrap();

            unsafe {
                vr_error_message_string(config, message.as_ptr());
            }

            std::ptr::null()
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_vk_library_free(library: *mut Library) {
    unsafe { Box::from_raw(library) };
}

#[no_mangle]
pub unsafe extern "C" fn vr_vk_instance_new(
    get_instance_proc_cb: GetInstanceProcFunc,
    user_data: *const c_void,
) -> *const Instance {
    Box::into_raw(Box::new(Instance::new(
        get_instance_proc_cb,
        user_data,
    )))
}

#[no_mangle]
pub extern "C" fn vr_vk_instance_free(instance: *mut Instance) {
    unsafe { Box::from_raw(instance) };
}

#[no_mangle]
pub extern "C" fn vr_vk_device_new(
    instance: *const Instance,
    device: vk::VkDevice,
) -> *const Device {
    Box::into_raw(Box::new(Device::new(
        unsafe { instance.as_ref().unwrap() },
        device,
    )))
}

#[no_mangle]
pub extern "C" fn vr_vk_device_free(device: *mut Device) {
    unsafe { Box::from_raw(device) };
}

// Helper function for unit tests in other modules that want to create
// a fake Vulkan library to help testing. The tests can override
// functions in the structs to implement tests.
#[cfg(test)]
pub fn make_fake_vulkan() -> (Library, Instance, Device) {
    use std::ffi::CStr;

    extern "C" fn get_instance_proc_addr(
        _instance: vk::VkInstance,
        _name: *const c_char,
    ) -> vk::PFN_vkVoidFunction {
        None
    }

    extern "C" fn get_instance_proc_cb(
        func_name: *const u8,
        _user_data: *const c_void,
    ) -> *const c_void {
        let name = unsafe { CStr::from_ptr(func_name.cast()) };
        let name = match name.to_str() {
            Err(_) => return std::ptr::null(),
            Ok(n) => n,
        };

        if name == "vkGetDeviceProcAddr" {
            unsafe {
                std::mem::transmute(
                    vk::PFN_vkGetDeviceProcAddr::Some(get_device_proc_addr)
                )
            }
        } else {
            std::ptr::null()
        }
    }

    extern "C" fn get_device_proc_addr(
        _device: vk::VkDevice,
        _name: *const c_char,
    ) -> vk::PFN_vkVoidFunction {
        None
    }

    let vklib = Library {
        lib_vulkan: std::ptr::null(),

        vkGetInstanceProcAddr: Some(get_instance_proc_addr),
        vkCreateInstance: None,
        vkEnumerateInstanceExtensionProperties: None,
    };

    let vkinst = unsafe {
        Instance::new(get_instance_proc_cb, std::ptr::null())
    };
    let vkdev = Device::new(&vkinst, std::ptr::null_mut());

    (vklib, vkinst, vkdev)
}

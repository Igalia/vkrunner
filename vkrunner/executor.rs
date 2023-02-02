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

use crate::vulkan_funcs;
use crate::context::{Context, ContextError};
use crate::config::Config;
use crate::window_format::WindowFormat;
use crate::script::{Script, LoadError};
use crate::source::Source;
use crate::result;
use crate::vk;
use crate::requirements::Requirements;
use std::ffi::{c_void, c_int};
use std::ptr;
use std::fmt;
use std::rc::Rc;

extern "C" {
    fn vr_window_new(
        config: *const Config,
        context: &Context,
        format: &WindowFormat,
        window_out: *mut *const c_void,
    ) -> result::Result;

    fn vr_window_get_format(window: *const c_void) -> *const WindowFormat;

    fn vr_window_free(window: *const c_void);

    fn vr_pipeline_create(
        config: *const Config,
        window: *const c_void,
        script: &Script,
    ) -> *mut c_void;

    fn vr_pipeline_free(pipeline: *mut c_void);

    fn vr_test_run(
        window: *const c_void,
        pipeline: *const c_void,
        script: &Script,
    ) -> bool;
}

pub enum ExecutorError {
    Context(ContextError),
    WindowIncompatible,
    WindowError,
    PipelineError,
    LoadError(LoadError),
    TestFailed,
}

impl ExecutorError {
    pub fn result(&self) -> result::Result {
        match self {
            ExecutorError::Context(e) => match e {
                ContextError::Failure(_) => result::Result::Fail,
                ContextError::Incompatible(_) => result::Result::Skip,
            },
            ExecutorError::WindowIncompatible => result::Result::Skip,
            ExecutorError::WindowError => result::Result::Fail,
            ExecutorError::PipelineError => result::Result::Fail,
            ExecutorError::LoadError(_) => result::Result::Fail,
            ExecutorError::TestFailed => result::Result::Fail,
        }
    }
}

impl From<ContextError> for ExecutorError {
    fn from(error: ContextError) -> ExecutorError {
        ExecutorError::Context(error)
    }
}

impl From<LoadError> for ExecutorError {
    fn from(error: LoadError) -> ExecutorError {
        ExecutorError::LoadError(error)
    }
}

impl fmt::Display for ExecutorError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ExecutorError::Context(e) => e.fmt(f),
            ExecutorError::WindowIncompatible => {
                write!(f, "The window format is incompatible")
            },
            ExecutorError::WindowError => {
                write!(f, "Error creating the window")
            },
            ExecutorError::PipelineError => {
                write!(f, "Error creating the pipelines")
            },
            ExecutorError::LoadError(e) => e.fmt(f),
            ExecutorError::TestFailed => {
                write!(f, "Test failed")
            },
        }
    }
}

#[derive(Debug)]
struct ExternalData {
    get_instance_proc_cb: vulkan_funcs::GetInstanceProcFunc,
    user_data: *const c_void,
    physical_device: vk::VkPhysicalDevice,
    queue_family: u32,
    vk_device: vk::VkDevice,
}

#[derive(Debug)]
pub struct Executor {
    config: *const Config,
    window: Option<*const c_void>,
    context: Option<Rc<Context>>,
    // A cache of the requirements that the context was created with.
    // Used to detect if the requirements have changed. This won’t be
    // used if the context is external.
    requirements: Requirements,

    external: Option<ExternalData>,
}

impl Executor {
    fn reset_window(&mut self) {
        if let Some(window) = self.window.take() {
            unsafe {
                vr_window_free(window);
            }
        }
    }

    fn reset_context(&mut self) {
        self.reset_window();
        self.context = None;
    }

    fn context_is_compatible(&self, script: &Script) -> bool {
        // If the device is created externally then it’s up to the
        // caller to ensure the device has all the necessary features
        // enabled.
        if self.external.is_some() {
            return true;
        }

        match self.context {
            Some(_) => self.requirements.eq(script.requirements()),
            None => false,
        }
    }

    pub fn new(config: *const Config) -> Executor {
        Executor {
            config,
            window: None,
            context: None,
            requirements: Requirements::new(),
            external: None,
        }
    }

    pub fn set_device(
        &mut self,
        get_instance_proc_cb: vulkan_funcs::GetInstanceProcFunc,
        user_data: *const c_void,
        physical_device: vk::VkPhysicalDevice,
        queue_family: u32,
        vk_device: vk::VkDevice,
    ) {
        self.reset_context();

        self.external = Some(ExternalData {
            get_instance_proc_cb,
            user_data,
            physical_device,
            queue_family,
            vk_device,
        });
    }

    fn create_context(
        &self,
        requirements: &Requirements,
    ) -> Result<Context, ExecutorError> {
        match &self.external {
            Some(e) => {
                Ok(Context::new_with_device(
                    e.get_instance_proc_cb,
                    e.user_data,
                    e.physical_device,
                    e.queue_family,
                    e.vk_device,
                )?)
            },
            None => {
                let device_id = unsafe { (*self.config).device_id };
                let device_id = if device_id >= 0 {
                    Some(device_id as usize)
                } else {
                    None
                };

                Ok(Context::new(requirements, device_id)?)
            },
        }
    }

    fn context_for_script(
        &mut self,
        script: &Script,
    ) -> Result<Rc<Context>, ExecutorError> {
        // Recreate the context if the features or extensions have changed
        if !self.context_is_compatible(script) {
            self.reset_context();
        }

        match &self.context {
            Some(c) => Ok(Rc::clone(c)),
            None => {
                self.requirements.clone_from(script.requirements());
                let context = self.create_context(&self.requirements)?;

                Ok(Rc::clone(self.context.insert(Rc::new(context))))
            }
        }
    }

    fn window_for_script(
        &mut self,
        script: &Script,
        context: Rc<Context>,
    ) -> Result<*const c_void, ExecutorError> {
        // Recreate the window if the framebuffer format is different
        if let &Some(window) = &self.window {
            let window_format = unsafe { &*vr_window_get_format(window) };

            if !window_format.eq(script.window_format()) {
                self.reset_window();
            }
        }

        match self.window {
            Some(w) => Ok(w),
            None => {
                let mut window = std::ptr::null();
                let res = unsafe {
                    vr_window_new(
                        self.config,
                        context.as_ref(),
                        script.window_format(),
                        ptr::addr_of_mut!(window),
                    )
                };
                match res {
                    result::Result::Pass => Ok(*self.window.insert(window)),
                    result::Result::Skip => {
                        Err(ExecutorError::WindowIncompatible)
                    },
                    result::Result::Fail => {
                        Err(ExecutorError::WindowError)
                    },
                }
            },
        }
    }

    pub fn execute_script(
        &mut self,
        script: &Script
    ) -> Result<(), ExecutorError> {
        let context = self.context_for_script(script)?;
        let window = self.window_for_script(script, Rc::clone(&context))?;

        let pipeline = unsafe {
            vr_pipeline_create(
                self.config,
                window,
                script
            )
        };

        if pipeline.is_null() {
            return Err(ExecutorError::PipelineError);
        }

        let test_result = unsafe {
            vr_test_run(window, pipeline, script)
        };

        unsafe {
            vr_pipeline_free(pipeline);
        }

        if test_result {
            Ok(())
        } else {
            Err(ExecutorError::TestFailed)
        }
    }

    pub fn execute(
        &mut self,
        source: &Source,
    ) -> Result<(), ExecutorError> {
        self.execute_script(&Script::load(source)?)
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        self.reset_window();
    }
}

#[no_mangle]
pub extern "C" fn vr_executor_new(config: *const Config) -> *mut Executor {
    Box::into_raw(Box::new(Executor::new(config)))
}

#[no_mangle]
pub extern "C" fn vr_executor_set_device(
    executor: &mut Executor,
    get_instance_proc_cb: vulkan_funcs::GetInstanceProcFunc,
    user_data: *const c_void,
    physical_device: vk::VkPhysicalDevice,
    queue_family: c_int,
    vk_device: vk::VkDevice,
) {
    executor.set_device(
        get_instance_proc_cb,
        user_data,
        physical_device,
        queue_family as u32,
        vk_device,
    )
}

fn handle_execute_result(result: Result<(), ExecutorError>) -> result::Result {
    match result {
        Ok(()) => result::Result::Pass,
        Err(e) => {
            eprintln!("{}", e);
            e.result()
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_executor_execute(
    executor: &mut Executor,
    source: &Source,
) -> result::Result {
    handle_execute_result(executor.execute(source))
}

#[no_mangle]
pub extern "C" fn vr_executor_execute_script(
    executor: &mut Executor,
    script: &Script,
) -> result::Result {
    handle_execute_result(executor.execute_script(script))
}

#[no_mangle]
pub extern "C" fn vr_executor_free(executor: *mut Executor) {
    unsafe { Box::from_raw(executor) };
}

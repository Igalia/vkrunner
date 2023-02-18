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
use crate::context::{self, Context};
use crate::window::{Window, WindowError};
use crate::config::Config;
use crate::script::{Script, LoadError};
use crate::source::Source;
use crate::result;
use crate::vk;
use crate::requirements::Requirements;
use crate::pipeline_set::{self, PipelineSet};
use crate::tester;
use std::ffi::{c_void, c_int};
use std::fmt;
use std::rc::Rc;
use std::cell::RefCell;

pub enum ExecutorError {
    Context(context::Error),
    Window(WindowError),
    PipelineError(pipeline_set::Error),
    LoadError(LoadError),
    TestError(tester::Error),
}

impl ExecutorError {
    pub fn result(&self) -> result::Result {
        match self {
            ExecutorError::Context(e) => e.result(),
            ExecutorError::Window(e) => e.result(),
            ExecutorError::PipelineError(_) => result::Result::Fail,
            ExecutorError::LoadError(_) => result::Result::Fail,
            ExecutorError::TestError(_) => result::Result::Fail,
        }
    }
}

impl From<context::Error> for ExecutorError {
    fn from(error: context::Error) -> ExecutorError {
        ExecutorError::Context(error)
    }
}

impl From<WindowError> for ExecutorError {
    fn from(error: WindowError) -> ExecutorError {
        ExecutorError::Window(error)
    }
}

impl From<LoadError> for ExecutorError {
    fn from(error: LoadError) -> ExecutorError {
        ExecutorError::LoadError(error)
    }
}

impl From<pipeline_set::Error> for ExecutorError {
    fn from(error: pipeline_set::Error) -> ExecutorError {
        ExecutorError::PipelineError(error)
    }
}

impl From<tester::Error> for ExecutorError {
    fn from(error: tester::Error) -> ExecutorError {
        ExecutorError::TestError(error)
    }
}

impl fmt::Display for ExecutorError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ExecutorError::Context(e) => e.fmt(f),
            ExecutorError::Window(e) => e.fmt(f),
            ExecutorError::PipelineError(e) => e.fmt(f),
            ExecutorError::LoadError(e) => e.fmt(f),
            ExecutorError::TestError(e) => e.fmt(f),
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
    config: Rc<RefCell<Config>>,

    window: Option<Rc<Window>>,
    context: Option<Rc<Context>>,
    // A cache of the requirements that the context was created with.
    // Used to detect if the requirements have changed. This won’t be
    // used if the context is external.
    requirements: Requirements,

    external: Option<ExternalData>,
}

impl Executor {
    fn reset_window(&mut self) {
        self.window = None;
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

    pub fn new(config: Rc<RefCell<Config>>) -> Executor {
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
            None => Ok(Context::new(
                requirements,
                self.config.borrow().device_id()
            )?),
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
    ) -> Result<Rc<Window>, ExecutorError> {
        // Recreate the window if the framebuffer format is different
        if let Some(window) = &self.window {
            if !window.format().eq(script.window_format()) {
                self.reset_window();
            }
        }

        match &self.window {
            Some(w) => Ok(Rc::clone(w)),
            None => Ok(Rc::clone(self.window.insert(Rc::new(Window::new(
                context,
                script.window_format(),
            )?)))),
        }
    }

    pub fn execute_script(
        &mut self,
        script: &Script
    ) -> Result<(), ExecutorError> {
        let context = self.context_for_script(script)?;
        let window = self.window_for_script(script, Rc::clone(&context))?;

        let pipeline_set = PipelineSet::new(
            &mut self.config.borrow().logger().borrow_mut(),
            Rc::clone(&window),
            script,
            self.config.borrow().show_disassembly(),
        )?;

        tester::run(
            window.as_ref(),
            &pipeline_set,
            script,
            self.config.borrow().inspector().clone(),
        )?;

        Ok(())
    }

    pub fn execute(
        &mut self,
        source: &Source,
    ) -> Result<(), ExecutorError> {
        self.execute_script(&Script::load(source)?)
    }
}

#[no_mangle]
pub extern "C" fn vr_executor_new(
    config: *const RefCell<Config>
) -> *mut Executor {
    let config = unsafe { Rc::from_raw(config) };

    let executor = Box::into_raw(Box::new(Executor::new(Rc::clone(&config))));

    // Forget the config because we don’t want to steal the caller’s
    // reference
    std::mem::forget(config);

    executor
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

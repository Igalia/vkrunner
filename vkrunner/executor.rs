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
use crate::requirements;
use std::ffi::{c_void, c_int};
use std::fmt;
use std::rc::Rc;
use std::cell::RefCell;

#[derive(Debug)]
pub enum Error {
    Context(context::Error),
    Window(WindowError),
    PipelineError(pipeline_set::Error),
    LoadError(LoadError),
    TestError(tester::Error),
    ExternalDeviceRequirementsError(requirements::Error),
}

impl Error {
    pub fn result(&self) -> result::Result {
        match self {
            Error::Context(e) => e.result(),
            Error::Window(e) => e.result(),
            Error::ExternalDeviceRequirementsError(e) => e.result(),
            Error::PipelineError(_) => result::Result::Fail,
            Error::LoadError(_) => result::Result::Fail,
            Error::TestError(_) => result::Result::Fail,
        }
    }
}

impl From<context::Error> for Error {
    fn from(error: context::Error) -> Error {
        Error::Context(error)
    }
}

impl From<WindowError> for Error {
    fn from(error: WindowError) -> Error {
        Error::Window(error)
    }
}

impl From<LoadError> for Error {
    fn from(error: LoadError) -> Error {
        Error::LoadError(error)
    }
}

impl From<pipeline_set::Error> for Error {
    fn from(error: pipeline_set::Error) -> Error {
        Error::PipelineError(error)
    }
}

impl From<tester::Error> for Error {
    fn from(error: tester::Error) -> Error {
        Error::TestError(error)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::Context(e) => e.fmt(f),
            Error::Window(e) => e.fmt(f),
            Error::PipelineError(e) => e.fmt(f),
            Error::LoadError(e) => e.fmt(f),
            Error::TestError(e) => e.fmt(f),
            Error::ExternalDeviceRequirementsError(e) => e.fmt(f),
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
    ) -> Result<Context, Error> {
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
    ) -> Result<Rc<Context>, Error> {
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
    ) -> Result<Rc<Window>, Error> {
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
    ) -> Result<(), Error> {
        let context = self.context_for_script(script)?;

        if self.external.is_some() {
            if let Err(e) = script.requirements().check(
                context.instance(),
                context.physical_device(),
            ) {
                return Err(Error::ExternalDeviceRequirementsError(e));
            }
        }

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
    ) -> Result<(), Error> {
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

fn handle_execute_result(result: Result<(), Error>) -> result::Result {
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

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::FakeVulkan;
    use crate::config::{vr_config_new, vr_config_set_device_id};
    use std::ffi::c_char;

    fn create_fake_vulkan() -> Box<FakeVulkan> {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.physical_devices[0].features.wideLines = vk::VK_TRUE;
        fake_vulkan.physical_devices[0].format_properties.insert(
            vk::VK_FORMAT_B8G8R8A8_UNORM,
            vk::VkFormatProperties {
                linearTilingFeatures: 0,
                optimalTilingFeatures:
                vk::VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                    | vk::VK_FORMAT_FEATURE_BLIT_SRC_BIT,
                bufferFeatures: 0,
            },
        );
        fake_vulkan.physical_devices[0].format_properties.insert(
            vk::VK_FORMAT_R8_UNORM,
            vk::VkFormatProperties {
                linearTilingFeatures: 0,
                optimalTilingFeatures:
                vk::VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                    | vk::VK_FORMAT_FEATURE_BLIT_SRC_BIT,
                bufferFeatures: 0,
            },
        );

        let memory_properties =
            &mut fake_vulkan.physical_devices[0].memory_properties;
        memory_properties.memoryTypes[0].propertyFlags =
            vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        memory_properties.memoryTypeCount = 1;
        fake_vulkan.memory_requirements.memoryTypeBits = 1;

        fake_vulkan
    }

    #[test]
    fn recreate_resources() {
        let fake_vulkan = create_fake_vulkan();

        let config = unsafe { Rc::from_raw(vr_config_new()) };

        let mut executor = Executor::new(Rc::clone(&config));

        fake_vulkan.set_override();
        executor.execute(&Source::from_string("".to_string())).unwrap();

        let context = Rc::clone(executor.context.as_ref().unwrap());
        let window = Rc::clone(&executor.window.as_ref().unwrap());

        // Run another script that has the same requirements
        executor.execute(&Source::from_string(
            "[test]\n\
             draw rect -1 -1 2 2".to_string()
        )).unwrap();

        // The context and window shouldn’t have changed
        assert!(Rc::ptr_eq(&context, executor.context.as_ref().unwrap()));
        assert!(Rc::ptr_eq(&window, executor.window.as_ref().unwrap()));

        // Run a script with different requirements
        fake_vulkan.set_override();
        executor.execute(&Source::from_string(
            "[require]\n\
             wideLines\n\
             [test]\n\
             draw rect -1 -1 2 2".to_string()
        )).unwrap();

        // The context and window should have changed
        assert!(!Rc::ptr_eq(&context, executor.context.as_ref().unwrap()));
        assert!(!Rc::ptr_eq(&window, executor.window.as_ref().unwrap()));

        let context = Rc::clone(executor.context.as_ref().unwrap());
        let window = Rc::clone(&executor.window.as_ref().unwrap());

        // Run the same script with a different framebuffer format
        executor.execute(&Source::from_string(
            "[require]\n\
             wideLines\n\
             framebuffer R8_UNORM\n\
             [test]\n\
             draw rect -1 -1 2 2".to_string()
        )).unwrap();

        // The context should stay the same but the framebuffer should
        // have changed
        assert!(Rc::ptr_eq(&context, executor.context.as_ref().unwrap()));
        assert!(!Rc::ptr_eq(&window, executor.window.as_ref().unwrap()));
    }

    extern "C" fn get_instance_proc_cb(
        func_name: *const c_char,
        user_data: *const c_void,
    ) -> *const c_void {
        unsafe {
            let fake_vulkan = &*(user_data as *const FakeVulkan);

            let func = fake_vulkan.get_function(func_name);

            std::mem::transmute(func)
        }
    }

    #[test]
    fn external_device() {
        let fake_vulkan = create_fake_vulkan();

        fake_vulkan.set_override();
        let context = Context::new(&Requirements::new(), None).unwrap();

        let config = unsafe { Rc::from_raw(vr_config_new()) };

        let mut executor = Executor::new(Rc::clone(&config));

        let (queue_family, _) = FakeVulkan::unmake_queue(context.queue());

        executor.set_device(
            get_instance_proc_cb,
            (fake_vulkan.as_ref() as *const FakeVulkan).cast(),
            context.physical_device(),
            queue_family,
            context.vk_device()
        );

        executor.execute(&Source::from_string("".to_string())).unwrap();
    }

    #[test]
    fn external_requirements_error() {
        let fake_vulkan = create_fake_vulkan();

        fake_vulkan.set_override();
        let context = Context::new(&Requirements::new(), None).unwrap();

        let config = unsafe { Rc::from_raw(vr_config_new()) };

        let mut executor = Executor::new(Rc::clone(&config));

        let (queue_family, _) = FakeVulkan::unmake_queue(context.queue());

        executor.set_device(
            get_instance_proc_cb,
            (fake_vulkan.as_ref() as *const FakeVulkan).cast(),
            context.physical_device(),
            queue_family,
            context.vk_device()
        );

        let source = Source::from_string(
            "[require]\n\
             logicOp".to_string()
        );

        let error = executor.execute(&source).unwrap_err();

        assert_eq!(
            &error.to_string(),
            "Missing required feature: logicOp",
        );
        assert_eq!(
            error.result(),
            result::Result::Skip,
        );
    }

    #[test]
    fn context_error() {
        let fake_vulkan = create_fake_vulkan();

        let config = unsafe { Rc::from_raw(vr_config_new()) };
        vr_config_set_device_id(&config, 12);

        let mut executor = Executor::new(Rc::clone(&config));

        let source = Source::from_string("".to_string());

        fake_vulkan.set_override();
        let error = executor.execute(&source).unwrap_err();

        assert_eq!(
            &error.to_string(),
            "Device 12 was selected but the Vulkan instance only reported \
             1 device.",
        );
        assert_eq!(
            error.result(),
            result::Result::Fail,
        );
    }

    fn run_script_error(source: &str) -> Error {
        let fake_vulkan = create_fake_vulkan();

        let config = unsafe { Rc::from_raw(vr_config_new()) };

        let mut executor = Executor::new(config);

        let source = Source::from_string(source.to_string());

        fake_vulkan.set_override();
        executor.execute(&source).unwrap_err()
    }

    #[test]
    fn window_error() {
        let error = run_script_error(
            "[require]\n\
             depthstencil R8_UNORM"
        );

        assert_eq!(
            &error.to_string(),
            "Format R8_UNORM is not supported as a depth/stencil attachment",
        );
        assert_eq!(
            error.result(),
            result::Result::Skip,
        );
    }

    #[test]
    fn script_error() {
        let error = run_script_error("[bad section]\n");

        assert_eq!(
            &error.to_string(),
            "line 1: Unknown section “bad section”",
        );
        assert_eq!(
            error.result(),
            result::Result::Fail,
        );
    }

    #[test]
    fn pipeline_error() {
        let error = run_script_error(
            "[vertex shader]\n\
             12"
        );

        assert_eq!(
            &error.to_string(),
            "The compiler or assembler generated an invalid SPIR-V binary",
        );
        assert_eq!(
            error.result(),
            result::Result::Fail,
        );
    }

    #[test]
    fn tester_error() {
        let error = run_script_error(
            "[test]\n\
             probe all rgb 1 2 3"
        );

        assert_eq!(
            &error.to_string(),
            "line 2: Probe color at (0,0)\n\
             \x20 Expected: 1 2 3\n\
             \x20 Observed: 0 0 0"
        );
        assert_eq!(
            error.result(),
            result::Result::Fail,
        );
    }
}

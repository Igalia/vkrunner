// vkrunner
//
// Copyright (C) 2018 Intel Corporation
// Copypright 2023 Neil Roberts
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

use crate::window::Window;
use crate::compiler;
use crate::shader_stage;
use crate::vk;
use crate::script::{Script, Buffer, BufferType, Operation};
use crate::pipeline_key;
use crate::logger::Logger;
use crate::vbo::Vbo;
use std::rc::Rc;
use std::ptr;
use std::mem;
use std::fmt;

#[derive(Debug)]
pub struct PipelineSet {
    pipelines: PipelineVec,
    layout: PipelineLayout,
    // The descriptor data is only created if there are buffers in the
    // script
    descriptor_data: Option<DescriptorData>,
    stages: vk::VkShaderStageFlagBits,
    pipeline_cache: PipelineCache,
    modules: [Option<ShaderModule>; shader_stage::N_STAGES],
}

#[repr(C)]
#[derive(Debug)]
pub struct RectangleVertex {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

/// An error that can be returned by [PipelineSet::new].
#[derive(Debug)]
pub enum Error {
    /// Compiling one of the shaders in the script failed
    CompileError(compiler::Error),
    /// vkCreatePipelineCache failed
    CreatePipelineCacheFailed,
    /// vkCreateDescriptorPool failed
    CreateDescriptorPoolFailed,
    /// vkCreateDescriptorSetLayout failed
    CreateDescriptorSetLayoutFailed,
    /// vkCreatePipelineLayout failed
    CreatePipelineLayoutFailed,
    /// vkCreatePipeline failed
    CreatePipelineFailed,
}

impl From<compiler::Error> for Error {
    fn from(e: compiler::Error) -> Error {
        Error::CompileError(e)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        match self {
            Error::CompileError(e) => e.fmt(f),
            Error::CreatePipelineCacheFailed => {
                write!(f, "vkCreatePipelineCache failed")
            },
            Error::CreateDescriptorPoolFailed => {
                write!(f, "vkCreateDescriptorPool failed")
            },
            Error::CreateDescriptorSetLayoutFailed => {
                write!(f, "vkCreateDescriptorSetLayout failed")
            },
            Error::CreatePipelineLayoutFailed => {
                write!(f, "vkCreatePipelineLayout failed")
            },
            Error::CreatePipelineFailed => {
                write!(f, "Pipeline creation function failed")
            },
        }
    }
}

#[derive(Debug)]
struct ShaderModule {
    handle: vk::VkShaderModule,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for ShaderModule {
    fn drop(&mut self) {
        unsafe {
            self.window.device().vkDestroyShaderModule.unwrap()(
                self.window.vk_device(),
                self.handle,
                ptr::null(), // allocator
            );
        }
    }
}

#[derive(Debug)]
struct PipelineCache {
    handle: vk::VkPipelineCache,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for PipelineCache {
    fn drop(&mut self) {
        unsafe {
            self.window.device().vkDestroyPipelineCache.unwrap()(
                self.window.vk_device(),
                self.handle,
                ptr::null(), // allocator
            );
        }
    }
}

impl PipelineCache {
    fn new(window: Rc<Window>) -> Result<PipelineCache, Error> {
        let create_info = vk::VkPipelineCacheCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            flags: 0,
            pNext: ptr::null(),
            initialDataSize: 0,
            pInitialData: ptr::null(),
        };

        let mut handle = ptr::null_mut();

        let res = unsafe {
            window.device().vkCreatePipelineCache.unwrap()(
                window.vk_device(),
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(PipelineCache { handle, window })
        } else {
            Err(Error::CreatePipelineCacheFailed)
        }
    }
}

#[derive(Debug)]
struct DescriptorPool {
    handle: vk::VkDescriptorPool,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for DescriptorPool {
    fn drop(&mut self) {
        unsafe {
            self.window.device().vkDestroyDescriptorPool.unwrap()(
                self.window.vk_device(),
                self.handle,
                ptr::null(), // allocator
            );
        }
    }
}

impl DescriptorPool {
    fn new(
        window: Rc<Window>,
        buffers: &[Buffer],
    ) -> Result<DescriptorPool, Error> {
        let mut n_ubos = 0;
        let mut n_ssbos = 0;

        for buffer in buffers {
            match buffer.buffer_type {
                BufferType::Ubo => n_ubos += 1,
                BufferType::Ssbo => n_ssbos += 1,
            }
        }

        let mut pool_sizes = Vec::<vk::VkDescriptorPoolSize>::new();

        if n_ubos > 0 {
            pool_sizes.push(vk::VkDescriptorPoolSize {
                type_: vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                descriptorCount: n_ubos,
            });
        }
        if n_ssbos > 0 {
            pool_sizes.push(vk::VkDescriptorPoolSize {
                type_: vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                descriptorCount: n_ssbos,
            });
        }

        // The descriptor pool shouldn’t have been created if there
        // were no buffers
        assert!(!pool_sizes.is_empty());

        let create_info = vk::VkDescriptorPoolCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            flags: vk::VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            pNext: ptr::null(),
            maxSets: n_desc_sets(buffers) as u32,
            poolSizeCount: pool_sizes.len() as u32,
            pPoolSizes: pool_sizes.as_ptr(),
        };

        let mut handle = ptr::null_mut();

        let res = unsafe {
            window.device().vkCreateDescriptorPool.unwrap()(
                window.vk_device(),
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(DescriptorPool { handle, window })
        } else {
            Err(Error::CreateDescriptorPoolFailed)
        }
    }
}

#[derive(Debug)]
struct DescriptorSetLayoutVec {
    handles: Vec<vk::VkDescriptorSetLayout>,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for DescriptorSetLayoutVec {
    fn drop(&mut self) {
        for &handle in self.handles.iter() {
            unsafe {
                self.window.device().vkDestroyDescriptorSetLayout.unwrap()(
                    self.window.vk_device(),
                    handle,
                    ptr::null(), // allocator
                );
            }
        }
    }
}

impl DescriptorSetLayoutVec {
    fn new(window: Rc<Window>) -> DescriptorSetLayoutVec {
        DescriptorSetLayoutVec { window, handles: Vec::new() }
    }

    fn len(&self) -> usize {
        self.handles.len()
    }

    fn as_ptr(&self) -> *const vk::VkDescriptorSetLayout {
        self.handles.as_ptr()
    }

    fn add(
        &mut self,
        bindings: &[vk::VkDescriptorSetLayoutBinding],
    ) -> Result<(), Error> {
        let create_info = vk::VkDescriptorSetLayoutCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            flags: 0,
            pNext: ptr::null(),
            bindingCount: bindings.len() as u32,
            pBindings: bindings.as_ptr(),
        };

        let mut handle = ptr::null_mut();

        let res = unsafe {
            self.window.device().vkCreateDescriptorSetLayout.unwrap()(
                self.window.vk_device(),
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            self.handles.push(handle);
            Ok(())
        } else {
            Err(Error::CreateDescriptorSetLayoutFailed)
        }
    }
}

fn create_descriptor_set_layouts(
    window: Rc<Window>,
    buffers: &[Buffer],
    stages: vk::VkShaderStageFlags,
) -> Result<DescriptorSetLayoutVec, Error> {
    let n_desc_sets = n_desc_sets(buffers);
    let mut layouts = DescriptorSetLayoutVec::new(window);
    let mut bindings = Vec::new();
    let mut buffer_num = 0;

    for desc_set in 0..n_desc_sets {
        bindings.clear();

        while buffer_num < buffers.len()
            && buffers[buffer_num].desc_set as usize == desc_set
        {
            let buffer = &buffers[buffer_num];

            bindings.push(vk::VkDescriptorSetLayoutBinding {
                binding: buffer.binding,
                descriptorType: match buffer.buffer_type {
                    BufferType::Ubo => vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    BufferType::Ssbo => vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                },
                descriptorCount: 1,
                stageFlags: stages,
                pImmutableSamplers: ptr::null(),
            });

            buffer_num += 1;
        }

        layouts.add(&bindings)?;
    }

    assert_eq!(layouts.len(), n_desc_sets);

    Ok(layouts)
}

fn n_desc_sets(buffers: &[Buffer]) -> usize {
    match buffers.last() {
        // The number of descriptor sets is the highest used
        // descriptor set index + 1. The buffers are in order so the
        // highest one should be the last one.
        Some(last) => last.desc_set as usize + 1,
        None => 0,
    }
}

#[derive(Debug)]
struct DescriptorData {
    pool: DescriptorPool,
    layouts: DescriptorSetLayoutVec,
}

impl DescriptorData {
    fn new(
        window: &Rc<Window>,
        buffers: &[Buffer],
        stages: vk::VkShaderStageFlagBits,
    ) -> Result<DescriptorData, Error> {
        let pool = DescriptorPool::new(
            Rc::clone(&window),
            buffers,
        )?;

        let layouts = create_descriptor_set_layouts(
            Rc::clone(&window),
            buffers,
            stages,
        )?;

        Ok(DescriptorData { pool, layouts })
    }
}

fn compile_shaders(
    logger: &mut Logger,
    window: &Rc<Window>,
    script: &Script,
    show_disassembly: bool,
) -> Result<[Option<ShaderModule>; shader_stage::N_STAGES], Error> {
    let mut modules: [Option<ShaderModule>; shader_stage::N_STAGES] =
        Default::default();

    for &stage in shader_stage::ALL_STAGES.iter() {
        if script.shaders(stage).is_empty() {
            continue;
        }

        modules[stage as usize] = Some(ShaderModule {
            handle: compiler::build_stage(
                logger,
                window.context(),
                script,
                stage,
                show_disassembly,
            )?,
            window: Rc::clone(window),
        });
    }

    Ok(modules)
}

fn stage_flags(script: &Script) -> vk::VkShaderStageFlagBits {
    // Set a flag for each stage that has a shader in the script
    shader_stage::ALL_STAGES
        .iter()
        .filter_map(|&stage| if script.shaders(stage).is_empty() {
            None
        } else {
            Some(stage.flag())
        })
        .fold(0, |a, b| a | b)
}

fn push_constant_size(script: &Script) -> usize {
    script.commands()
        .iter()
        .map(|command| match &command.op {
            Operation::SetPushCommand { offset, data } => offset + data.len(),
            _ => 0,
        })
        .max()
        .unwrap_or(0)
}

#[derive(Debug)]
struct PipelineLayout {
    handle: vk::VkPipelineLayout,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for PipelineLayout {
    fn drop(&mut self) {
        unsafe {
            self.window.device().vkDestroyPipelineLayout.unwrap()(
                self.window.vk_device(),
                self.handle,
                ptr::null(), // allocator
            );
        }
    }
}

impl PipelineLayout {
    fn new(
        window: Rc<Window>,
        script: &Script,
        stages: vk::VkShaderStageFlagBits,
        descriptor_data: Option<&DescriptorData>
    ) -> Result<PipelineLayout, Error> {
        let mut handle = ptr::null_mut();

        let push_constant_range = vk::VkPushConstantRange {
            stageFlags: stages,
            offset: 0,
            size: push_constant_size(script) as u32,
        };

        let mut create_info = vk::VkPipelineLayoutCreateInfo::default();

        create_info.sType = vk::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        if push_constant_range.size > 0 {
            create_info.pushConstantRangeCount = 1;
            create_info.pPushConstantRanges =
                ptr::addr_of!(push_constant_range);
        }

        if let Some(descriptor_data) = descriptor_data {
            create_info.setLayoutCount = descriptor_data.layouts.len() as u32;
            create_info.pSetLayouts = descriptor_data.layouts.as_ptr();
        }

        let res = unsafe {
            window.device().vkCreatePipelineLayout.unwrap()(
                window.vk_device(),
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(PipelineLayout { handle, window })
        } else {
            Err(Error::CreatePipelineLayoutFailed)
        }
    }
}

#[derive(Debug)]
struct VertexInputState {
    create_info: vk::VkPipelineVertexInputStateCreateInfo,
    input_bindings: Vec::<vk::VkVertexInputBindingDescription>,
    attribs: Vec::<vk::VkVertexInputAttributeDescription>,
}

impl VertexInputState {
    fn new(script: &Script, key: &pipeline_key::Key) -> VertexInputState {
        let mut input_bindings = Vec::new();
        let mut attribs = Vec::new();

        match key.source() {
            pipeline_key::Source::Rectangle => {
                input_bindings.push(vk::VkVertexInputBindingDescription {
                    binding: 0,
                    stride: mem::size_of::<RectangleVertex>() as u32,
                    inputRate: vk::VK_VERTEX_INPUT_RATE_VERTEX,
                });
                attribs.push(vk::VkVertexInputAttributeDescription {
                    location: 0,
                    binding: 0,
                    format: vk::VK_FORMAT_R32G32B32_SFLOAT,
                    offset: 0,
                });
            },
            pipeline_key::Source::VertexData => {
                if let Some(vbo) = script.vertex_data() {
                    VertexInputState::set_up_vertex_data_attribs(
                        &mut input_bindings,
                        &mut attribs,
                        vbo
                    );
                }
            }
        }

        VertexInputState {
            create_info: vk::VkPipelineVertexInputStateCreateInfo {
                sType:
                vk::VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                flags: 0,
                pNext: ptr::null(),
                vertexBindingDescriptionCount: input_bindings.len() as u32,
                pVertexBindingDescriptions: input_bindings.as_ptr(),
                vertexAttributeDescriptionCount: attribs.len() as u32,
                pVertexAttributeDescriptions: attribs.as_ptr(),
            },
            input_bindings,
            attribs,
        }
    }

    fn set_up_vertex_data_attribs(
        input_bindings: &mut Vec::<vk::VkVertexInputBindingDescription>,
        attribs: &mut Vec::<vk::VkVertexInputAttributeDescription>,
        vbo: &Vbo,
    ) {
        input_bindings.push(vk::VkVertexInputBindingDescription {
            binding: 0,
            stride: vbo.stride() as u32,
            inputRate: vk::VK_VERTEX_INPUT_RATE_VERTEX,
        });

        for attrib in vbo.attribs().iter() {
            attribs.push(vk::VkVertexInputAttributeDescription {
                location: attrib.location(),
                binding: 0,
                format: attrib.format().vk_format,
                offset: attrib.offset() as u32,
            });
        }
    }
}

#[derive(Debug)]
struct PipelineVec {
    handles: Vec<vk::VkPipeline>,
    // needed for the destructor
    window: Rc<Window>,
}

impl Drop for PipelineVec {
    fn drop(&mut self) {
        for &handle in self.handles.iter() {
            unsafe {
                self.window.device().vkDestroyPipeline.unwrap()(
                    self.window.vk_device(),
                    handle,
                    ptr::null(), // allocator
                );
            }
        }
    }
}

impl PipelineVec {
    fn new(
        window: Rc<Window>,
        script: &Script,
        pipeline_cache: vk::VkPipelineCache,
        layout: vk::VkPipelineLayout,
        modules: &[Option<ShaderModule>],
    ) -> Result<PipelineVec, Error> {
        let mut vec = PipelineVec { window, handles: Vec::new() };

        let mut first_graphics_pipeline: Option<vk::VkPipeline> = None;

        for key in script.pipeline_keys().iter() {
            let pipeline = match key.pipeline_type() {
                pipeline_key::Type::Graphics => {
                    let allow_derivatives =
                        first_graphics_pipeline.is_none()
                        && script.pipeline_keys().len() > 1;

                    let pipeline = PipelineVec::create_graphics_pipeline(
                        &vec.window,
                        script,
                        key,
                        pipeline_cache,
                        layout,
                        modules,
                        allow_derivatives,
                        first_graphics_pipeline,
                    )?;

                    first_graphics_pipeline.get_or_insert(pipeline);

                    pipeline
                },
                pipeline_key::Type::Compute => {
                    PipelineVec::create_compute_pipeline(
                        &vec.window,
                        key,
                        pipeline_cache,
                        layout,
                        modules[shader_stage::Stage::Compute as usize]
                            .as_ref()
                            .map(|m| m.handle)
                            .unwrap_or(ptr::null_mut())
                    )?
                },
            };

            vec.handles.push(pipeline);
        }

        Ok(vec)
    }

    fn null_terminated_entrypoint(
        key: &pipeline_key::Key,
        stage: shader_stage::Stage,
    ) -> String {
        let mut entrypoint = key.entrypoint(stage).to_string();
        entrypoint.push('\0');
        entrypoint
    }

    fn create_stages(
        modules: &[Option<ShaderModule>],
        entrypoints: &[String],
    ) -> Vec<vk::VkPipelineShaderStageCreateInfo> {
        let mut stages = Vec::new();

        for &stage in shader_stage::ALL_STAGES.iter() {
            if stage == shader_stage::Stage::Compute {
                continue;
            }

            let module = match &modules[stage as usize] {
                Some(module) => module.handle,
                None => continue,
            };

            stages.push(vk::VkPipelineShaderStageCreateInfo {
                sType: vk::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                flags: 0,
                pNext: ptr::null(),
                stage: stage.flag(),
                module,
                pName: entrypoints[stage as usize].as_ptr().cast(),
                pSpecializationInfo: ptr::null(),
            });
        }

        stages
    }

    fn create_graphics_pipeline(
        window: &Window,
        script: &Script,
        key: &pipeline_key::Key,
        pipeline_cache: vk::VkPipelineCache,
        layout: vk::VkPipelineLayout,
        modules: &[Option<ShaderModule>],
        allow_derivatives: bool,
        parent_pipeline: Option<vk::VkPipeline>,
    ) -> Result<vk::VkPipeline, Error> {
        let create_info_buf = key.to_create_info();

        let mut create_info = vk::VkGraphicsPipelineCreateInfo::default();

        unsafe {
            ptr::copy_nonoverlapping(
                create_info_buf.as_ptr().cast(),
                ptr::addr_of_mut!(create_info),
                1, // count
            );
        }

        let entrypoints = shader_stage::ALL_STAGES
            .iter()
            .map(|&s| PipelineVec::null_terminated_entrypoint(key, s))
            .collect::<Vec<String>>();
        let stages = PipelineVec::create_stages(modules, &entrypoints);

        let window_format = window.format();

        let viewport = vk::VkViewport {
            x: 0.0,
            y: 0.0,
            width: window_format.width as f32,
            height: window_format.height as f32,
            minDepth: 0.0,
            maxDepth: 0.0,
        };

        let scissor = vk::VkRect2D {
            offset: vk::VkOffset2D {
                x: 0,
                y: 0,
            },
            extent: vk::VkExtent2D {
                width: window_format.width as u32,
                height: window_format.height as u32,
            },
        };

        let viewport_state = vk::VkPipelineViewportStateCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            flags: 0,
            pNext: ptr::null(),
            viewportCount: 1,
            pViewports: ptr::addr_of!(viewport),
            scissorCount: 1,
            pScissors: ptr::addr_of!(scissor),
        };

        let multisample_state = vk::VkPipelineMultisampleStateCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            rasterizationSamples: vk::VK_SAMPLE_COUNT_1_BIT,
            sampleShadingEnable: vk::VK_FALSE,
            minSampleShading: 0.0,
            pSampleMask: ptr::null(),
            alphaToCoverageEnable: vk::VK_FALSE,
            alphaToOneEnable: vk::VK_FALSE,
        };

        create_info.pViewportState = ptr::addr_of!(viewport_state);
        create_info.pMultisampleState = ptr::addr_of!(multisample_state);
        create_info.subpass = 0;
        create_info.basePipelineHandle =
            parent_pipeline.unwrap_or(ptr::null_mut());
        create_info.basePipelineIndex = -1;

        create_info.stageCount = stages.len() as u32;
        create_info.pStages = stages.as_ptr();
        create_info.layout = layout;
        create_info.renderPass = window.render_passes()[0];

        if allow_derivatives {
            create_info.flags |= vk::VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        }
        if parent_pipeline.is_some() {
            create_info.flags |= vk::VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        }

        if modules[shader_stage::Stage::TessCtrl as usize].is_none()
            && modules[shader_stage::Stage::TessEval as usize].is_none()
        {
            create_info.pTessellationState = ptr::null();
        }

        let vertex_input_state = VertexInputState::new(script, key);
        create_info.pVertexInputState =
            ptr::addr_of!(vertex_input_state.create_info);

        let mut handle = ptr::null_mut();

        let res = unsafe {
            window.device().vkCreateGraphicsPipelines.unwrap()(
                window.vk_device(),
                pipeline_cache,
                1, // nCreateInfos
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(handle)
        } else {
            Err(Error::CreatePipelineFailed)
        }
    }

    fn create_compute_pipeline(
        window: &Window,
        key: &pipeline_key::Key,
        pipeline_cache: vk::VkPipelineCache,
        layout: vk::VkPipelineLayout,
        module: vk::VkShaderModule,
    ) -> Result<vk::VkPipeline, Error> {
        let entrypoint = PipelineVec::null_terminated_entrypoint(
            key,
            shader_stage::Stage::Compute,
        );

        let create_info = vk::VkComputePipelineCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,

            stage: vk::VkPipelineShaderStageCreateInfo {
                sType: vk::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                pNext: ptr::null(),
                flags: 0,
                stage: vk::VK_SHADER_STAGE_COMPUTE_BIT,
                module,
                pName: entrypoint.as_ptr().cast(),
                pSpecializationInfo: ptr::null(),
            },

            layout,
            basePipelineHandle: ptr::null_mut(),
            basePipelineIndex: -1,
        };

        let mut handle = ptr::null_mut();

        let res = unsafe {
            window.device().vkCreateComputePipelines.unwrap()(
                window.vk_device(),
                pipeline_cache,
                1, // nCreateInfos
                ptr::addr_of!(create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(handle),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(handle)
        } else {
            Err(Error::CreatePipelineFailed)
        }
    }
}

impl PipelineSet {
    pub fn new(
        logger: &mut Logger,
        window: Rc<Window>,
        script: &Script,
        show_disassembly: bool,
    ) -> Result<PipelineSet, Error> {
        let modules = compile_shaders(
            logger,
            &window,
            script,
            show_disassembly
        )?;

        let pipeline_cache = PipelineCache::new(Rc::clone(&window))?;

        let stages = stage_flags(script);

        let descriptor_data = if script.buffers().is_empty() {
            None
        } else {
            Some(DescriptorData::new(&window, script.buffers(), stages)?)
        };

        let layout = PipelineLayout::new(
            Rc::clone(&window),
            script,
            stages,
            descriptor_data.as_ref(),
        )?;

        let pipelines = PipelineVec::new(
            Rc::clone(&window),
            script,
            pipeline_cache.handle,
            layout.handle,
            &modules,
        )?;

        Ok(PipelineSet {
            modules,
            pipeline_cache,
            stages,
            descriptor_data,
            layout,
            pipelines,
        })
    }

    pub fn descriptor_set_layouts(&self) -> &[vk::VkDescriptorSetLayout] {
        match self.descriptor_data.as_ref() {
            Some(data) => &data.layouts.handles,
            None => &[],
        }
    }

    pub fn stages(&self) -> vk::VkShaderStageFlagBits {
        self.stages
    }

    pub fn layout(&self) -> vk::VkPipelineLayout {
        self.layout.handle
    }

    pub fn pipelines(&self) -> &[vk::VkPipeline] {
        &self.pipelines.handles
    }

    pub fn descriptor_pool(&self) -> Option<vk::VkDescriptorPool> {
        self.descriptor_data.as_ref().map(|data| data.pool.handle)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, HandleType, PipelineCreateInfo};
    use crate::fake_vulkan::GraphicsPipelineCreateInfo;
    use crate::fake_vulkan::PipelineLayoutCreateInfo;
    use crate::context::Context;
    use crate::requirements::Requirements;
    use crate::source::Source;

    #[derive(Debug)]
    struct TestData {
        pipeline_set: PipelineSet,
        window: Rc<Window>,
        fake_vulkan: Box<FakeVulkan>,
    }

    impl TestData {
        fn new_with_errors<F: FnOnce(&mut FakeVulkan)>(
            source: &str,
            queue_errors: F
        ) -> Result<TestData, Error> {
            let mut fake_vulkan = FakeVulkan::new();

            fake_vulkan.physical_devices.push(Default::default());
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

            let memory_properties =
                &mut fake_vulkan.physical_devices[0].memory_properties;
            memory_properties.memoryTypes[0].propertyFlags =
                vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            memory_properties.memoryTypeCount = 1;
            fake_vulkan.memory_requirements.memoryTypeBits = 1;

            fake_vulkan.set_override();
            let context = Rc::new(Context::new(
                &Requirements::new(),
                None, // device_id
            ).unwrap());

            let window = Rc::new(Window::new(
                Rc::clone(&context),
                &Default::default(), // format
            ).unwrap());

            queue_errors(&mut fake_vulkan);

            let mut logger = Logger::new(None, ptr::null_mut());

            let source = Source::from_string(source.to_string());
            let script = Script::load(&source).unwrap();

            let pipeline_set = PipelineSet::new(
                &mut logger,
                Rc::clone(&window),
                &script,
                false, // show_disassembly
            )?;

            Ok(TestData { fake_vulkan, window, pipeline_set })
        }

        fn new(source: &str) -> Result<TestData, Error> {
            TestData::new_with_errors(source, |_| ())
        }

        fn graphics_create_info(
            &mut self,
            pipeline_num: usize,
        ) -> GraphicsPipelineCreateInfo {
            let pipeline = self.pipeline_set.pipelines.handles[pipeline_num];

            match &self.fake_vulkan.get_handle(pipeline).data {
                HandleType::Pipeline(PipelineCreateInfo::Graphics(info)) => {
                    info.clone()
                },
                handle @ _ => {
                    unreachable!("unexpected handle type: {:?}", handle)
                },
            }
        }

        fn shader_module_code(
            &mut self,
            stage: shader_stage::Stage,
        ) -> Vec<u32> {
            let module = self.pipeline_set.modules[stage as usize]
                .as_ref()
                .unwrap()
                .handle;

            match &self.fake_vulkan.get_handle(module).data {
                HandleType::ShaderModule { code } => code.clone(),
                _ => unreachable!("Unexpected Vulkan handle type"),
            }
        }

        fn descriptor_set_layout_bindings(
            &mut self,
            desc_set: usize,
        ) -> Vec<vk::VkDescriptorSetLayoutBinding> {
            let desc_set_layout = self.pipeline_set
                .descriptor_set_layouts()
                [desc_set];

            match &self.fake_vulkan.get_handle(desc_set_layout).data {
                HandleType::DescriptorSetLayout { bindings } => {
                    bindings.clone()
                },
                _ => unreachable!("Unexpected Vulkan handle type"),
            }
        }

        fn pipeline_layout_create_info(&mut self) -> PipelineLayoutCreateInfo {
            let layout = self.pipeline_set.layout();

            match &self.fake_vulkan.get_handle(layout).data {
                HandleType::PipelineLayout(create_info) => create_info.clone(),
                _ => unreachable!("Unexpected Vulkan handle type"),
            }
        }
    }

    #[test]
    fn base() {
        let mut test_data = TestData::new(
            "[vertex shader passthrough]\n\
             [fragment shader]\n\
             03 02 23 07\n\
             fe ca fe ca\n\
             [vertex data]\n\
             0/R32G32_SFLOAT 1/R32_SFLOAT\n\
             -0.5 -0.5 1.52\n\
             0.5 -0.5 1.55\n\
             [compute shader]\n\
             03 02 23 07\n\
             ca fe ca fe\n\
             [test]\n\
             ubo 0 1024\n\
             ssbo 1 1024\n\
             compute 1 1 1\n\
             push float 6 42.0\n\
             draw rect 0 0 1 1\n\
             draw arrays TRIANGLE_LIST 0 2\n"
        ).unwrap();

        assert_eq!(
            test_data.pipeline_set.stages(),
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT
                | vk::VK_SHADER_STAGE_COMPUTE_BIT,
        );

        let descriptor_pool = test_data.pipeline_set.descriptor_pool();
        assert!(matches!(
            test_data.fake_vulkan.get_handle(descriptor_pool.unwrap()).data,
            HandleType::DescriptorPool,
        ));

        assert_eq!(test_data.pipeline_set.pipelines().len(), 3);

        let create_data = test_data.graphics_create_info(1);
        let create_info = &create_data.create_info;

        assert_eq!(
            create_info.flags,
            vk::VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        );
        assert_eq!(create_info.stageCount, 2);
        assert_eq!(create_info.layout, test_data.pipeline_set.layout());
        assert_eq!(
            create_info.renderPass,
            test_data.window.render_passes()[0]
        );
        assert_eq!(create_info.basePipelineHandle, ptr::null_mut());

        assert_eq!(create_data.bindings.len(), 1);
        assert_eq!(create_data.bindings[0].binding, 0);
        assert_eq!(
            create_data.bindings[0].stride as usize,
            mem::size_of::<RectangleVertex>(),
        );
        assert_eq!(
            create_data.bindings[0].inputRate,
            vk::VK_VERTEX_INPUT_RATE_VERTEX,
        );

        assert_eq!(create_data.attribs.len(), 1);
        assert_eq!(create_data.attribs[0].location, 0);
        assert_eq!(create_data.attribs[0].binding, 0);
        assert_eq!(
            create_data.attribs[0].format,
            vk::VK_FORMAT_R32G32B32_SFLOAT
        );
        assert_eq!(create_data.attribs[0].offset, 0);

        let create_data = test_data.graphics_create_info(2);
        let create_info = &create_data.create_info;

        assert_eq!(
            create_info.flags,
            vk::VK_PIPELINE_CREATE_DERIVATIVE_BIT,
        );
        assert_eq!(create_info.stageCount, 2);
        assert_eq!(create_info.layout, test_data.pipeline_set.layout());
        assert_eq!(
            create_info.renderPass,
            test_data.window.render_passes()[0]
        );
        assert_eq!(
            create_info.basePipelineHandle,
            test_data.pipeline_set.pipelines()[1]
        );

        assert_eq!(create_data.bindings.len(), 1);
        assert_eq!(create_data.bindings[0].binding, 0);
        assert_eq!(
            create_data.bindings[0].stride as usize,
            mem::size_of::<f32>() * 3,
        );
        assert_eq!(
            create_data.bindings[0].inputRate,
            vk::VK_VERTEX_INPUT_RATE_VERTEX,
        );

        assert_eq!(create_data.attribs.len(), 2);
        assert_eq!(create_data.attribs[0].location, 0);
        assert_eq!(create_data.attribs[0].binding, 0);
        assert_eq!(
            create_data.attribs[0].format,
            vk::VK_FORMAT_R32G32_SFLOAT
        );
        assert_eq!(create_data.attribs[0].offset, 0);
        assert_eq!(create_data.attribs[1].location, 1);
        assert_eq!(create_data.attribs[1].binding, 0);
        assert_eq!(
            create_data.attribs[1].format,
            vk::VK_FORMAT_R32_SFLOAT
        );
        assert_eq!(create_data.attribs[1].offset, 8);

        let code = test_data.shader_module_code(shader_stage::Stage::Vertex);
        assert!(code.len() > 2);

        let code = test_data.shader_module_code(shader_stage::Stage::Fragment);
        assert_eq!(&code, &[0x07230203, 0xcafecafe]);

        let code = test_data.shader_module_code(shader_stage::Stage::Compute);
        assert_eq!(&code, &[0x07230203, 0xfecafeca]);

        assert_eq!(test_data.pipeline_set.descriptor_set_layouts().len(), 1);
        let bindings = test_data.descriptor_set_layout_bindings(0);
        assert_eq!(bindings.len(), 2);

        assert_eq!(bindings[0].binding, 0);
        assert_eq!(
            bindings[0].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        );
        assert_eq!(bindings[0].descriptorCount, 1);
        assert_eq!(
            bindings[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT
                | vk::VK_SHADER_STAGE_COMPUTE_BIT,
        );

        assert_eq!(bindings[1].binding, 1);
        assert_eq!(
            bindings[1].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
        assert_eq!(bindings[1].descriptorCount, 1);
        assert_eq!(
            bindings[1].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT
                | vk::VK_SHADER_STAGE_COMPUTE_BIT,
        );

        let layout = test_data.pipeline_layout_create_info();
        assert_eq!(layout.push_constant_ranges.len(), 1);
        assert_eq!(
            layout.push_constant_ranges[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT
                | vk::VK_SHADER_STAGE_COMPUTE_BIT,
        );
        assert_eq!(layout.push_constant_ranges[0].offset, 0);
        assert_eq!(
            layout.push_constant_ranges[0].size as usize,
            6 + mem::size_of::<f32>()
        );
        assert_eq!(
            &layout.layouts,
            test_data.pipeline_set.descriptor_set_layouts()
        );
    }

    #[test]
    fn descriptor_set_gaps() {
        let mut test_data = TestData::new(
            "[vertex shader passthrough]\n\
             [fragment shader]\n\
             03 02 23 07\n\
             fe ca fe ca\n\
             [test]\n\
             ubo 0 1024\n\
             ssbo 2:1 1024\n\
             ssbo 3:1 1024\n\
             ubo 5:5 1024\n\
             draw rect 0 0 1 1\n"
        ).unwrap();

        assert_eq!(test_data.pipeline_set.descriptor_set_layouts().len(), 6);

        let bindings = test_data.descriptor_set_layout_bindings(0);
        assert_eq!(bindings.len(), 1);

        assert_eq!(bindings[0].binding, 0);
        assert_eq!(
            bindings[0].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        );
        assert_eq!(bindings[0].descriptorCount, 1);
        assert_eq!(
            bindings[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT,
        );

        assert_eq!(test_data.descriptor_set_layout_bindings(1).len(), 0);

        let bindings = test_data.descriptor_set_layout_bindings(2);
        assert_eq!(bindings.len(), 1);

        assert_eq!(bindings[0].binding, 1);
        assert_eq!(
            bindings[0].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
        assert_eq!(bindings[0].descriptorCount, 1);
        assert_eq!(
            bindings[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT,
        );

        let bindings = test_data.descriptor_set_layout_bindings(3);
        assert_eq!(bindings.len(), 1);

        assert_eq!(bindings[0].binding, 1);
        assert_eq!(
            bindings[0].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
        assert_eq!(bindings[0].descriptorCount, 1);
        assert_eq!(
            bindings[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT,
        );

        assert_eq!(test_data.descriptor_set_layout_bindings(4).len(), 0);

        let bindings = test_data.descriptor_set_layout_bindings(5);
        assert_eq!(bindings.len(), 1);

        assert_eq!(bindings[0].binding, 5);
        assert_eq!(
            bindings[0].descriptorType,
            vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        );
        assert_eq!(bindings[0].descriptorCount, 1);
        assert_eq!(
            bindings[0].stageFlags,
            vk::VK_SHADER_STAGE_VERTEX_BIT
                | vk::VK_SHADER_STAGE_FRAGMENT_BIT,
        );
    }

    #[test]
    fn compile_error() {
        let error = TestData::new(
            "[fragment shader]\n\
             12 34 56 78\n"
        ).unwrap_err();

        assert_eq!(
            &error.to_string(),
            "The compiler or assembler generated an invalid SPIR-V binary"
        );
    }

    #[test]
    fn create_errors() {
        let commands = [
            "vkCreatePipelineCache",
            "vkCreateDescriptorPool",
            "vkCreateDescriptorSetLayout",
            "vkCreatePipelineLayout",
        ];
        for command in commands {
            let error = TestData::new_with_errors(
                "[test]\n\
                 ubo 0 10\n\
                 draw rect 0 0 1 1\n",
                |fake_vulkan| fake_vulkan.queue_result(
                    command.to_string(),
                    vk::VK_ERROR_UNKNOWN,
                ),
            ).unwrap_err();

            assert_eq!(
                error.to_string(),
                format!("{} failed", command),
            );
        }
    }

    #[test]
    fn create_pipeline_errors() {
        let commands = [
            "vkCreateGraphicsPipelines",
            "vkCreateComputePipelines",
        ];
        for command in commands {
            let error = TestData::new_with_errors(
                "[compute shader]\n\
                 03 02 23 07\n\
                 ca fe ca fe\n\
                 [test]\n\
                 compute 1 1 1\n\
                 draw rect 0 0 1 1\n",
                |fake_vulkan| fake_vulkan.queue_result(
                    command.to_string(),
                    vk::VK_ERROR_UNKNOWN,
                ),
            ).unwrap_err();

            assert_eq!(
                &error.to_string(),
                "Pipeline creation function failed",
            );
        }
    }

    #[test]
    fn no_buffers() {
        let test_data = TestData::new("").unwrap();

        assert!(test_data.pipeline_set.descriptor_pool().is_none());
        assert_eq!(test_data.pipeline_set.descriptor_set_layouts().len(), 0);
    }
}

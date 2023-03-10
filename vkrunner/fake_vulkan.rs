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

#[derive(Debug, Clone)]
pub struct GraphicsPipelineCreateInfo {
    pub create_info: vk::VkGraphicsPipelineCreateInfo,
    pub bindings: Vec<vk::VkVertexInputBindingDescription>,
    pub attribs: Vec<vk::VkVertexInputAttributeDescription>,
}

impl GraphicsPipelineCreateInfo {
    fn new(
        create_info: &vk::VkGraphicsPipelineCreateInfo
    ) -> GraphicsPipelineCreateInfo {
        let vertex_input_state = unsafe {
            &*create_info.pVertexInputState
        };
        let bindings = unsafe {
            std::slice::from_raw_parts(
                vertex_input_state.pVertexBindingDescriptions,
                vertex_input_state.vertexBindingDescriptionCount as usize,
            ).to_owned()
        };
        let attribs = unsafe {
            std::slice::from_raw_parts(
                vertex_input_state.pVertexAttributeDescriptions,
                vertex_input_state.vertexAttributeDescriptionCount as usize,
            ).to_owned()
        };

        GraphicsPipelineCreateInfo {
            create_info: create_info.clone(),
            bindings,
            attribs,
        }
    }
}

#[derive(Debug)]
pub enum PipelineCreateInfo {
    Graphics(GraphicsPipelineCreateInfo),
    Compute(vk::VkComputePipelineCreateInfo),
}

#[derive(Debug, Clone)]
pub struct PipelineLayoutCreateInfo {
    pub create_info: vk::VkPipelineLayoutCreateInfo,
    pub push_constant_ranges: Vec<vk::VkPushConstantRange>,
    pub layouts: Vec<vk::VkDescriptorSetLayout>,
}

#[derive(Debug)]
// It would be nice to just store the VkClearAttachment directly but
// that can’t derive Debug because it is a union and it would be
// annoying to have to manually implement Debug.
pub enum ClearAttachment {
    Color {
        attachment: u32,
        value: [f32; 4],
    },
    DepthStencil {
        aspect_mask: vk::VkImageAspectFlags,
        value: vk::VkClearDepthStencilValue,
    },
}

#[derive(Debug)]
pub enum Command {
    BeginRenderPass(vk::VkRenderPassBeginInfo),
    EndRenderPass,
    BindPipeline {
        bind_point: vk::VkPipelineBindPoint,
        pipeline: vk::VkPipeline,
    },
    BindVertexBuffers {
        first_binding: u32,
        buffers: Vec<vk::VkBuffer>,
        offsets: Vec<vk::VkDeviceSize>,
    },
    BindIndexBuffer {
        buffer: vk::VkBuffer,
        offset: vk::VkDeviceSize,
        index_type: vk::VkIndexType,
    },
    Draw {
        vertex_count: u32,
        instance_count: u32,
        first_vertex: u32,
        first_instance: u32,
    },
    DrawIndexed {
        index_count: u32,
        instance_count: u32,
        first_index: u32,
        vertex_offset: i32,
        first_instance: u32,
    },
    Dispatch {
        x: u32,
        y: u32,
        z: u32,
    },
    ClearAttachments {
        attachments: Vec<ClearAttachment>,
        rects: Vec<vk::VkClearRect>,
    },
    PipelineBarrier {
        src_stage_mask: vk::VkPipelineStageFlags,
        dst_stage_mask: vk::VkPipelineStageFlags,
        dependency_flags: vk::VkDependencyFlags,
        memory_barriers: Vec<vk::VkMemoryBarrier>,
        buffer_memory_barriers: Vec<vk::VkBufferMemoryBarrier>,
        image_memory_barriers: Vec<vk::VkImageMemoryBarrier>,
    },
    CopyImageToBuffer {
        src_image: vk::VkImage,
        src_image_layout: vk::VkImageLayout,
        dst_buffer: vk::VkBuffer,
        regions: Vec<vk::VkBufferImageCopy>,
    },
    PushConstants {
        layout: vk::VkPipelineLayout,
        stage_flags: vk::VkShaderStageFlags,
        offset: u32,
        values: Vec<u8>,
    },
    BindDescriptorSets {
        pipeline_bind_point: vk::VkPipelineBindPoint,
        layout: vk::VkPipelineLayout,
        first_set: u32,
        descriptor_sets: Vec<vk::VkDescriptorSet>,
    },
}

#[derive(Debug)]
pub enum HandleType {
    Instance,
    Device,
    CommandPool,
    CommandBuffer {
        command_pool: usize,
        commands: Vec<Command>,
        begun: bool,
    },
    Fence {
        reset_count: usize,
        wait_count: usize,
    },
    Memory {
        contents: Vec<u8>,
        mapped: bool,
    },
    RenderPass { attachments: Vec<vk::VkAttachmentDescription> },
    Image,
    ImageView,
    Buffer {
        create_info: vk::VkBufferCreateInfo,
        memory: Option<vk::VkDeviceMemory>,
    },
    Framebuffer,
    ShaderModule { code: Vec<u32> },
    PipelineCache,
    DescriptorPool,
    DescriptorSetLayout { bindings: Vec<vk::VkDescriptorSetLayoutBinding> },
    PipelineLayout(PipelineLayoutCreateInfo),
    Pipeline(PipelineCreateInfo),
    DescriptorSet { bindings: HashMap<u32, Binding> },
}

#[derive(Debug)]
pub struct Binding {
    pub descriptor_type: vk::VkDescriptorType,
    pub info: vk::VkDescriptorBufferInfo,
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

    /// Log of calls to vkFlushMappedMemoryRanges
    pub memory_flushes: Vec<vk::VkMappedMemoryRange>,
    /// Log of calls to vkInvalidateMappedMemoryRanges
    pub memory_invalidations: Vec<vk::VkMappedMemoryRange>,

    /// All of the commands from command queues that were submitted
    /// with vkQueueSubmit
    pub commands: Vec<Command>,

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
            memory_flushes: Vec::new(),
            memory_invalidations: Vec::new(),
            commands: Vec::new(),
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
            "vkCreatePipelineCache" => unsafe {
                transmute::<vk::PFN_vkCreatePipelineCache, _>(
                    Some(FakeVulkan::create_pipeline_cache)
                )
            },
            "vkDestroyPipelineCache" => unsafe {
                transmute::<vk::PFN_vkDestroyPipelineCache, _>(
                    Some(FakeVulkan::destroy_pipeline_cache)
                )
            },
            "vkCreateDescriptorPool" => unsafe {
                transmute::<vk::PFN_vkCreateDescriptorPool, _>(
                    Some(FakeVulkan::create_descriptor_pool)
                )
            },
            "vkDestroyDescriptorPool" => unsafe {
                transmute::<vk::PFN_vkDestroyDescriptorPool, _>(
                    Some(FakeVulkan::destroy_descriptor_pool)
                )
            },
            "vkCreateDescriptorSetLayout" => unsafe {
                transmute::<vk::PFN_vkCreateDescriptorSetLayout, _>(
                    Some(FakeVulkan::create_descriptor_set_layout)
                )
            },
            "vkDestroyDescriptorSetLayout" => unsafe {
                transmute::<vk::PFN_vkDestroyDescriptorSetLayout, _>(
                    Some(FakeVulkan::destroy_descriptor_set_layout)
                )
            },
            "vkCreatePipelineLayout" => unsafe {
                transmute::<vk::PFN_vkCreatePipelineLayout, _>(
                    Some(FakeVulkan::create_pipeline_layout)
                )
            },
            "vkDestroyPipelineLayout" => unsafe {
                transmute::<vk::PFN_vkDestroyPipelineLayout, _>(
                    Some(FakeVulkan::destroy_pipeline_layout)
                )
            },
            "vkCreateGraphicsPipelines" => unsafe {
                transmute::<vk::PFN_vkCreateGraphicsPipelines, _>(
                    Some(FakeVulkan::create_graphics_pipelines)
                )
            },
            "vkCreateComputePipelines" => unsafe {
                transmute::<vk::PFN_vkCreateComputePipelines, _>(
                    Some(FakeVulkan::create_compute_pipelines)
                )
            },
            "vkDestroyPipeline" => unsafe {
                transmute::<vk::PFN_vkDestroyPipeline, _>(
                    Some(FakeVulkan::destroy_pipeline)
                )
            },
            "vkFlushMappedMemoryRanges" => unsafe {
                transmute::<vk::PFN_vkFlushMappedMemoryRanges, _>(
                    Some(FakeVulkan::flush_mapped_memory_ranges)
                )
            },
            "vkInvalidateMappedMemoryRanges" => unsafe {
                transmute::<vk::PFN_vkInvalidateMappedMemoryRanges, _>(
                    Some(FakeVulkan::invalidate_mapped_memory_ranges)
                )
            },
            "vkQueueSubmit" => unsafe {
                transmute::<vk::PFN_vkQueueSubmit, _>(
                    Some(FakeVulkan::queue_submit)
                )
            },
            "vkAllocateDescriptorSets" => unsafe {
                transmute::<vk::PFN_vkAllocateDescriptorSets, _>(
                    Some(FakeVulkan::allocate_descriptor_sets)
                )
            },
            "vkFreeDescriptorSets" => unsafe {
                transmute::<vk::PFN_vkFreeDescriptorSets, _>(
                    Some(FakeVulkan::free_descriptor_sets)
                )
            },
            "vkUpdateDescriptorSets" => unsafe {
                transmute::<vk::PFN_vkUpdateDescriptorSets, _>(
                    Some(FakeVulkan::update_descriptor_sets)
                )
            },
            "vkBeginCommandBuffer" => unsafe {
                transmute::<vk::PFN_vkBeginCommandBuffer, _>(
                    Some(FakeVulkan::begin_command_buffer)
                )
            },
            "vkEndCommandBuffer" => unsafe {
                transmute::<vk::PFN_vkEndCommandBuffer, _>(
                    Some(FakeVulkan::end_command_buffer)
                )
            },
            "vkCmdBeginRenderPass" => unsafe {
                transmute::<vk::PFN_vkCmdBeginRenderPass, _>(
                    Some(FakeVulkan::begin_render_pass)
                )
            },
            "vkCmdEndRenderPass" => unsafe {
                transmute::<vk::PFN_vkCmdEndRenderPass, _>(
                    Some(FakeVulkan::end_render_pass)
                )
            },
            "vkCmdBindPipeline" => unsafe {
                transmute::<vk::PFN_vkCmdBindPipeline, _>(
                    Some(FakeVulkan::bind_pipeline)
                )
            },
            "vkCmdBindVertexBuffers" => unsafe {
                transmute::<vk::PFN_vkCmdBindVertexBuffers, _>(
                    Some(FakeVulkan::bind_vertex_buffers)
                )
            },
            "vkCmdBindIndexBuffer" => unsafe {
                transmute::<vk::PFN_vkCmdBindIndexBuffer, _>(
                    Some(FakeVulkan::bind_index_buffer)
                )
            },
            "vkCmdDraw" => unsafe {
                transmute::<vk::PFN_vkCmdDraw, _>(
                    Some(FakeVulkan::draw)
                )
            },
            "vkCmdDrawIndexed" => unsafe {
                transmute::<vk::PFN_vkCmdDrawIndexed, _>(
                    Some(FakeVulkan::draw_indexed)
                )
            },
            "vkCmdDispatch" => unsafe {
                transmute::<vk::PFN_vkCmdDispatch, _>(
                    Some(FakeVulkan::dispatch)
                )
            },
            "vkCmdClearAttachments" => unsafe {
                transmute::<vk::PFN_vkCmdClearAttachments, _>(
                    Some(FakeVulkan::clear_attachments)
                )
            },
            "vkCmdPipelineBarrier" => unsafe {
                transmute::<vk::PFN_vkCmdPipelineBarrier, _>(
                    Some(FakeVulkan::pipeline_barrier)
                )
            },
            "vkCmdCopyImageToBuffer" => unsafe {
                transmute::<vk::PFN_vkCmdCopyImageToBuffer, _>(
                    Some(FakeVulkan::copy_image_to_buffer)
                )
            },
            "vkCmdPushConstants" => unsafe {
                transmute::<vk::PFN_vkCmdPushConstants, _>(
                    Some(FakeVulkan::push_constants)
                )
            },
            "vkCmdBindDescriptorSets" => unsafe {
                transmute::<vk::PFN_vkCmdBindDescriptorSets, _>(
                    Some(FakeVulkan::bind_descriptor_sets)
                )
            },
            "vkResetFences" => unsafe {
                transmute::<vk::PFN_vkResetFences, _>(
                    Some(FakeVulkan::reset_fences)
                )
            },
            "vkWaitForFences" => unsafe {
                transmute::<vk::PFN_vkWaitForFences, _>(
                    Some(FakeVulkan::wait_for_fences)
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

    pub fn add_dispatchable_handle<T>(&mut self, data: HandleType) -> *mut T {
        self.handles.push(Handle {
            freed: false,
            data,
        });

        self.handles.len() as *mut T
    }

    #[cfg(target_pointer_width = "64")]
    pub fn add_handle<T>(&mut self, data: HandleType) -> *mut T {
        self.add_dispatchable_handle(data)
    }

    #[cfg(not(target_pointer_width = "64"))]
    pub fn add_handle(&mut self, data: HandleType) -> u64 {
        self.handles.push(Handle {
            freed: false,
            data,
        });

        self.handles.len() as u64
    }

    pub fn dispatchable_handle_to_index<T>(handle: *mut T) -> usize {
        let handle_num = handle as usize;

        assert!(handle_num > 0);

        handle_num - 1
    }

    #[cfg(target_pointer_width = "64")]
    pub fn handle_to_index<T>(handle: *mut T) -> usize {
        FakeVulkan::dispatchable_handle_to_index(handle)
    }

    #[cfg(not(target_pointer_width = "64"))]
    pub fn handle_to_index(handle: u64) -> usize {
        assert!(handle > 0);

        (handle - 1) as usize
    }

    pub fn get_dispatchable_handle<T>(&self, handle: *mut T) -> &Handle {
        let index = FakeVulkan::dispatchable_handle_to_index(handle);
        let handle = &self.handles[index];

        assert!(!handle.freed);

        handle
    }

    pub fn get_dispatchable_handle_mut<T>(
        &mut self,
        handle: *mut T,
    ) -> &mut Handle {
        let index = FakeVulkan::dispatchable_handle_to_index(handle);
        let handle = &mut self.handles[index];

        assert!(!handle.freed);

        handle
    }

    #[cfg(target_pointer_width = "64")]
    pub fn get_handle<T>(&self, handle: *mut T) -> &Handle {
        let handle = &self.handles[FakeVulkan::handle_to_index(handle)];

        assert!(!handle.freed);

        handle
    }

    #[cfg(not(target_pointer_width = "64"))]
    pub fn get_handle(&self, handle: u64) -> &Handle {
        let handle = &self.handles[FakeVulkan::handle_to_index(handle)];

        assert!(!handle.freed);

        handle
    }

    #[cfg(target_pointer_width = "64")]
    pub fn get_freed_handle<T>(&self, handle: *mut T) -> &Handle {
        &self.handles[FakeVulkan::handle_to_index(handle)]
    }

    #[cfg(not(target_pointer_width = "64"))]
    pub fn get_freed_handle(&self, handle: u64) -> &Handle {
        &self.handles[FakeVulkan::handle_to_index(handle)]
    }

    #[cfg(target_pointer_width = "64")]
    pub fn get_handle_mut<T>(&mut self, handle: *mut T) -> &mut Handle {
        let handle = &mut self.handles[FakeVulkan::handle_to_index(handle)];

        assert!(!handle.freed);

        handle
    }

    #[cfg(not(target_pointer_width = "64"))]
    pub fn get_handle_mut(&mut self, handle: u64) -> &mut Handle {
        let handle = &mut self.handles[FakeVulkan::handle_to_index(handle)];

        assert!(!handle.freed);

        handle
    }

    fn check_device(&self, device: vk::VkDevice) {
        let handle = self.get_dispatchable_handle(device);
        assert!(matches!(handle.data, HandleType::Device));
    }

    fn check_command_pool(&self, command_pool: vk::VkCommandPool) {
        let handle = self.get_handle(command_pool);
        assert!(matches!(handle.data, HandleType::CommandPool));
    }

    fn check_image(&self, image: vk::VkImage) {
        let handle = self.get_handle(image);
        assert!(matches!(handle.data, HandleType::Image));
    }

    fn check_descriptor_pool(&self, descriptor_pool: vk::VkDescriptorPool) {
        let handle = self.get_handle(descriptor_pool);
        assert!(matches!(handle.data, HandleType::DescriptorPool));
    }

    fn check_image_view(&self, image_view: vk::VkImageView) {
        let handle = self.get_handle(image_view);
        assert!(matches!(handle.data, HandleType::ImageView));
    }

    fn check_pipeline_cache(&self, pipeline_cache: vk::VkPipelineCache) {
        let handle = self.get_handle(pipeline_cache);
        assert!(matches!(handle.data, HandleType::PipelineCache));
    }

    fn check_fence(&self, fence: vk::VkFence) {
        let handle = self.get_handle(fence);
        assert!(matches!(handle.data, HandleType::Fence { .. }));
    }

    fn check_framebuffer(&self, framebuffer: vk::VkFramebuffer) {
        let handle = self.get_handle(framebuffer);
        assert!(matches!(handle.data, HandleType::Framebuffer));
    }

    fn check_render_pass(&self, render_pass: vk::VkRenderPass) {
        let handle = self.get_handle(render_pass);
        assert!(matches!(handle.data, HandleType::RenderPass { .. }));
    }

    fn check_pipeline(&self, pipeline: vk::VkPipeline) {
        let handle = self.get_handle(pipeline);
        assert!(matches!(handle.data, HandleType::Pipeline { .. }));
    }

    fn check_buffer(&self, buffer: vk::VkBuffer) {
        let handle = self.get_handle(buffer);
        assert!(matches!(handle.data, HandleType::Buffer { .. }));
    }

    fn check_memory(&self, memory: vk::VkDeviceMemory) {
        let handle = self.get_handle(memory);
        assert!(matches!(handle.data, HandleType::Memory { .. }));
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
            *instance_out =
                fake_vulkan.add_dispatchable_handle(HandleType::Instance);
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
            *device_out =
                fake_vulkan.add_dispatchable_handle(HandleType::Device);
        }

        res
    }

    extern "C" fn destroy_device(
        device: vk::VkDevice,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        let handle = fake_vulkan.get_dispatchable_handle_mut(device);
        assert!(matches!(handle.data, HandleType::Device));
        handle.freed = true;
    }

    extern "C" fn destroy_instance(
        instance: vk::VkInstance,
        _allocator: *const vk::VkAllocationCallbacks
    ) {
        let fake_vulkan = FakeVulkan::current();

        let handle = fake_vulkan.get_dispatchable_handle_mut(instance);
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

        let handle = fake_vulkan.get_handle_mut(command_pool);
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
                *command_buffers.add(i) = fake_vulkan.add_dispatchable_handle(
                    HandleType::CommandBuffer {
                        command_pool: FakeVulkan::handle_to_index(
                            command_pool_handle
                        ),
                        commands: Vec::new(),
                        begun: false,
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

            let command_buffer_handle =
                fake_vulkan.get_dispatchable_handle_mut(command_buffer);

            match command_buffer_handle.data {
                HandleType::CommandBuffer { command_pool: handle_pool, .. } => {
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
            *fence_out = fake_vulkan.add_handle(HandleType::Fence {
                reset_count: 0,
                wait_count: 0,
            });
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

        let handle = fake_vulkan.get_handle_mut(fence);
        assert!(matches!(handle.data, HandleType::Fence { .. }));
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

        let handle = fake_vulkan.get_handle_mut(render_pass);
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

        let handle = fake_vulkan.get_handle_mut(image_view);
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

        let handle = fake_vulkan.get_handle_mut(image);
        assert!(matches!(handle.data, HandleType::Image));
        handle.freed = true;
    }

    extern "C" fn create_buffer(
        device: vk::VkDevice,
        create_info: *const vk::VkBufferCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        buffer_out: *mut vk::VkBuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateBuffer");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let create_info = unsafe { &*create_info };

        unsafe {
            *buffer_out = fake_vulkan.add_handle(
                HandleType::Buffer {
                    create_info: create_info.clone(),
                    memory: None,
                }
            );
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

        let handle = fake_vulkan.get_handle_mut(buffer);
        assert!(matches!(handle.data, HandleType::Buffer { .. }));
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

        let handle = fake_vulkan.get_handle_mut(framebuffer);
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

    extern "C" fn get_buffer_memory_requirements(
        device: vk::VkDevice,
        buffer: vk::VkBuffer,
        memory_requirements: *mut vk::VkMemoryRequirements,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let size = match fake_vulkan.get_handle(buffer).data {
            HandleType::Buffer { ref create_info, .. } => create_info.size,
            _ => unreachable!("mismatched handle"),
        };

        unsafe {
            *memory_requirements = fake_vulkan.memory_requirements.clone();
            (*memory_requirements).size = size;
        }
    }

    extern "C" fn get_image_memory_requirements(
        device: vk::VkDevice,
        image: vk::VkImage,
        memory_requirements: *mut vk::VkMemoryRequirements,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);
        fake_vulkan.check_image(image);

        unsafe {
            *memory_requirements = fake_vulkan.memory_requirements;
        }
    }

    extern "C" fn allocate_memory(
        device: vk::VkDevice,
        allocate_info: *const vk::VkMemoryAllocateInfo,
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
                contents: vec![
                    0u8;
                    (*allocate_info).allocationSize as usize
                ],
                mapped: false,
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

        let handle = fake_vulkan.get_handle_mut(memory);

        match handle.data {
            HandleType::Memory { mapped, .. } => assert!(!mapped),
            _ => unreachable!("mismatched handle"),
        }

        handle.freed = true;
    }

    extern "C" fn bind_buffer_memory(
        device: vk::VkDevice,
        buffer: vk::VkBuffer,
        memory: vk::VkDeviceMemory,
        _memory_offset: vk::VkDeviceSize,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkBindBufferMemory");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);
        fake_vulkan.check_memory(memory);

        let HandleType::Buffer { memory: ref mut buffer_memory, .. } =
            fake_vulkan.get_handle_mut(buffer).data
        else { unreachable!("mismatched handle"); };

        assert!(buffer_memory.is_none());

        *buffer_memory = Some(memory);

        res
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
        offset: vk::VkDeviceSize,
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

        let HandleType::Memory { ref mut mapped, ref mut contents } =
            fake_vulkan.get_handle_mut(memory).data
        else { unreachable!("mismatched handle"); };

        assert!(!*mapped);
        assert!(
            size == vk::VK_WHOLE_SIZE as vk::VkDeviceSize
                || (offset + size) as usize <= contents.len()
        );

        unsafe {
            *data_out = contents[offset as usize..].as_mut_ptr().cast();
        }

        *mapped = true;

        res
    }

    extern "C" fn unmap_memory(
        device: vk::VkDevice,
        memory: vk::VkDeviceMemory
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let HandleType::Memory { ref mut mapped, .. } =
            fake_vulkan.get_handle_mut(memory).data
        else { unreachable!("mismatched handle"); };

        assert!(*mapped);
        *mapped = false;
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

        let handle = fake_vulkan.get_handle_mut(shader_module);
        assert!(matches!(handle.data, HandleType::ShaderModule { .. }));
        handle.freed = true;
    }

    extern "C" fn create_pipeline_cache(
        device: vk::VkDevice,
        _create_info: *const vk::VkPipelineCacheCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        pipeline_cache_out: *mut vk::VkPipelineCache,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreatePipelineCache");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *pipeline_cache_out = fake_vulkan.add_handle(
                HandleType::PipelineCache
            );
        }

        res
    }

    extern "C" fn destroy_pipeline_cache(
        device: vk::VkDevice,
        pipeline_cache: vk::VkPipelineCache,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle_mut(pipeline_cache);
        assert!(matches!(handle.data, HandleType::PipelineCache));
        handle.freed = true;
    }

    extern "C" fn create_descriptor_pool(
        device: vk::VkDevice,
        _create_info: *const vk::VkDescriptorPoolCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        descriptor_pool_out: *mut vk::VkDescriptorPool,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateDescriptorPool");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            *descriptor_pool_out = fake_vulkan.add_handle(
                HandleType::DescriptorPool
            );
        }

        res
    }

    extern "C" fn destroy_descriptor_pool(
        device: vk::VkDevice,
        descriptor_pool: vk::VkDescriptorPool,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle_mut(descriptor_pool);
        assert!(matches!(handle.data, HandleType::DescriptorPool));
        handle.freed = true;
    }

    extern "C" fn create_descriptor_set_layout(
        device: vk::VkDevice,
        create_info: *const vk::VkDescriptorSetLayoutCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        descriptor_set_layout_out: *mut vk::VkDescriptorSetLayout,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateDescriptorSetLayout");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            let bindings = std::slice::from_raw_parts(
                (*create_info).pBindings,
                (*create_info).bindingCount as usize,
            ).to_vec();

            *descriptor_set_layout_out = fake_vulkan.add_handle(
                HandleType::DescriptorSetLayout { bindings }
            );
        }

        res
    }

    extern "C" fn destroy_descriptor_set_layout(
        device: vk::VkDevice,
        descriptor_set_layout: vk::VkDescriptorSetLayout,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle_mut(descriptor_set_layout);
        assert!(matches!(handle.data, HandleType::DescriptorSetLayout { .. }));
        handle.freed = true;
    }

    extern "C" fn create_pipeline_layout(
        device: vk::VkDevice,
        create_info: *const vk::VkPipelineLayoutCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        pipeline_layout_out: *mut vk::VkPipelineLayout,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreatePipelineLayout");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        unsafe {
            let push_constant_ranges = std::slice::from_raw_parts(
                (*create_info).pPushConstantRanges,
                (*create_info).pushConstantRangeCount as usize,
            ).to_vec();

            let layouts = std::slice::from_raw_parts(
                (*create_info).pSetLayouts,
                (*create_info).setLayoutCount as usize,
            ).to_vec();

            *pipeline_layout_out = fake_vulkan.add_handle(
                HandleType::PipelineLayout(PipelineLayoutCreateInfo {
                    create_info: (*create_info).clone(),
                    push_constant_ranges,
                    layouts,
                }),
            );
        }

        res
    }

    extern "C" fn destroy_pipeline_layout(
        device: vk::VkDevice,
        pipeline_layout: vk::VkPipelineLayout,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle_mut(pipeline_layout);
        assert!(matches!(handle.data, HandleType::PipelineLayout(_)));
        handle.freed = true;
    }

    extern "C" fn create_graphics_pipelines(
        device: vk::VkDevice,
        pipeline_cache: vk::VkPipelineCache,
        create_info_count: u32,
        create_infos: *const vk::VkGraphicsPipelineCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        pipelines_out: *mut vk::VkPipeline,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateGraphicsPipelines");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);
        fake_vulkan.check_pipeline_cache(pipeline_cache);

        for i in 0..create_info_count as usize {
            unsafe {
                let create_info = &*create_infos.add(i);

                *pipelines_out.add(i) = fake_vulkan.add_handle(
                    HandleType::Pipeline(
                        PipelineCreateInfo::Graphics(
                            GraphicsPipelineCreateInfo::new(create_info)
                        )
                    ),
                );
            }
        }

        res
    }

    extern "C" fn create_compute_pipelines(
        device: vk::VkDevice,
        pipeline_cache: vk::VkPipelineCache,
        create_info_count: u32,
        create_infos: *const vk::VkComputePipelineCreateInfo,
        _allocator: *const vk::VkAllocationCallbacks,
        pipelines_out: *mut vk::VkPipeline,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkCreateComputePipelines");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);
        fake_vulkan.check_pipeline_cache(pipeline_cache);

        for i in 0..create_info_count as usize {
            unsafe {
                *pipelines_out.add(i) = fake_vulkan.add_handle(
                    HandleType::Pipeline(PipelineCreateInfo::Compute(
                        (*create_infos.add(i)).clone()
                    )),
                );
            }
        };

        res
    }

    extern "C" fn destroy_pipeline(
        device: vk::VkDevice,
        pipeline: vk::VkPipeline,
        _allocator: *const vk::VkAllocationCallbacks,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let handle = fake_vulkan.get_handle_mut(pipeline);
        assert!(matches!(handle.data, HandleType::Pipeline { .. }));
        handle.freed = true;
    }

    extern "C" fn flush_mapped_memory_ranges(
        device: vk::VkDevice,
        memory_range_count: u32,
        memory_ranges: *const vk::VkMappedMemoryRange,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkFlushMappedMemoryRanges");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let memory_ranges = unsafe {
            std::slice::from_raw_parts(
                memory_ranges,
                memory_range_count as usize,
            )
        };

        fake_vulkan.memory_flushes.extend_from_slice(memory_ranges);

        res
    }

    extern "C" fn invalidate_mapped_memory_ranges(
        device: vk::VkDevice,
        memory_range_count: u32,
        memory_ranges: *const vk::VkMappedMemoryRange,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkInvalidateMappedMemoryRanges");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let memory_ranges = unsafe {
            std::slice::from_raw_parts(
                memory_ranges,
                memory_range_count as usize,
            )
        };

        fake_vulkan.memory_invalidations.extend_from_slice(memory_ranges);

        res
    }

    extern "C" fn queue_submit(
        _queue: vk::VkQueue,
        submit_count: u32,
        submits: *const vk::VkSubmitInfo,
        fence: vk::VkFence,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_fence(fence);

        let res = fake_vulkan.next_result("vkQueueSubmit");

        if res != vk::VK_SUCCESS {
            return res;
        }

        let submits = unsafe {
            std::slice::from_raw_parts(
                submits,
                submit_count as usize,
            )
        };

        for submit in submits.iter() {
            let command_buffers = unsafe {
                std::slice::from_raw_parts(
                    submit.pCommandBuffers,
                    submit.commandBufferCount as usize,
                )
            };

            for &command_buffer in command_buffers.iter() {
                let HandleType::CommandBuffer { ref mut commands, begun, .. } =
                    fake_vulkan.get_dispatchable_handle_mut(command_buffer).data
                else {
                    unreachable!("bad handle type")
                };

                assert!(!begun);

                let commands = mem::take(commands);
                fake_vulkan.commands.extend(commands);
            }
        }

        res
    }

    extern "C" fn allocate_descriptor_sets(
        device: vk::VkDevice,
        allocate_info: *const vk::VkDescriptorSetAllocateInfo,
        descriptor_sets: *mut vk::VkDescriptorSet,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkAllocateDescriptorSets");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        let descriptor_pool_handle = unsafe { (*allocate_info).descriptorPool };

        fake_vulkan.check_descriptor_pool(descriptor_pool_handle);

        let n_buffers = unsafe { (*allocate_info).descriptorSetCount };

        for i in 0..(n_buffers as usize) {
            unsafe {
                *descriptor_sets.add(i) = fake_vulkan.add_handle(
                    HandleType::DescriptorSet { bindings: HashMap::new() }
                );
            }
        }

        res
    }

    extern "C" fn free_descriptor_sets(
        device: vk::VkDevice,
        descriptor_pool: vk::VkDescriptorPool,
        descriptor_set_count: u32,
        descriptor_sets: *const vk::VkDescriptorSet,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkAllocateDescriptorSets");

        if res != vk::VK_SUCCESS {
            return res;
        }

        fake_vulkan.check_device(device);

        fake_vulkan.check_descriptor_pool(descriptor_pool);

        for i in 0..descriptor_set_count as usize {
            let descriptor_set = unsafe {
                *descriptor_sets.add(i)
            };

            let descriptor_set_handle =
                fake_vulkan.get_handle_mut(descriptor_set);

            match descriptor_set_handle.data {
                HandleType::DescriptorSet { .. } => {
                    descriptor_set_handle.freed = true;
                },
                _ => unreachable!("mismatched handle"),
            }
        }

        res
    }

    extern "C" fn update_descriptor_sets(
        device: vk::VkDevice,
        descriptor_write_count: u32,
        descriptor_writes: *const vk::VkWriteDescriptorSet,
        _descriptor_copy_count: u32,
        _descriptor_copies: *const vk::VkCopyDescriptorSet,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let descriptor_writes = unsafe {
            std::slice::from_raw_parts(
                descriptor_writes,
                descriptor_write_count as usize,
            )
        };

        for write in descriptor_writes.iter() {
            assert_eq!(write.sType, vk::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            assert_eq!(write.descriptorCount, 1);

            let HandleType::DescriptorSet { ref mut bindings } =
                fake_vulkan.get_handle_mut(write.dstSet).data
            else { unreachable!("mismatched handle type"); };

            bindings.insert(
                write.dstBinding,
                Binding {
                    descriptor_type: write.descriptorType,
                    info: unsafe { &*write.pBufferInfo }.clone(),
                },
            );
        }
    }

    extern "C" fn begin_command_buffer(
        command_buffer: vk::VkCommandBuffer,
        _begin_info: *const vk::VkCommandBufferBeginInfo,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkBeginCommandBuffer");

        if res != vk::VK_SUCCESS {
            return res;
        }

        let HandleType::CommandBuffer { ref mut begun, .. } =
            fake_vulkan.get_dispatchable_handle_mut(command_buffer).data
        else { unreachable!("mismatched handle"); };

        assert!(!*begun);
        *begun = true;

        res
    }

    extern "C" fn end_command_buffer(
        command_buffer: vk::VkCommandBuffer,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        let res = fake_vulkan.next_result("vkEndCommandBuffer");

        if res != vk::VK_SUCCESS {
            return res;
        }

        let HandleType::CommandBuffer { ref mut begun, .. } =
            fake_vulkan.get_dispatchable_handle_mut(command_buffer).data
        else { unreachable!("mismatched handle"); };

        assert!(*begun);
        *begun = false;

        res
    }

    fn add_command(
        &mut self,
        command_buffer: vk::VkCommandBuffer,
        command: Command,
    ) {
        let HandleType::CommandBuffer { ref mut commands, begun, .. } =
            self.get_dispatchable_handle_mut(command_buffer).data
        else { unreachable!("mismatched handle"); };

        assert!(begun);

        commands.push(command);
    }

    extern "C" fn begin_render_pass(
        command_buffer: vk::VkCommandBuffer,
        render_pass_begin: *const vk::VkRenderPassBeginInfo,
        _contents: vk::VkSubpassContents,
    ) {
        let render_pass_begin = unsafe { &*render_pass_begin };

        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_render_pass(render_pass_begin.renderPass);
        fake_vulkan.check_framebuffer(render_pass_begin.framebuffer);

        fake_vulkan.add_command(
            command_buffer,
            Command::BeginRenderPass(render_pass_begin.clone()),
        );
    }

    extern "C" fn end_render_pass(
        command_buffer: vk::VkCommandBuffer,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.add_command(command_buffer, Command::EndRenderPass);
    }

    extern "C" fn bind_pipeline(
        command_buffer: vk::VkCommandBuffer,
        pipeline_bind_point: vk::VkPipelineBindPoint,
        pipeline: vk::VkPipeline,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_pipeline(pipeline);

        fake_vulkan.add_command(
            command_buffer,
            Command::BindPipeline {
                bind_point: pipeline_bind_point,
                pipeline,
            },
        );
    }

    extern "C" fn bind_vertex_buffers(
        command_buffer: vk::VkCommandBuffer,
        first_binding: u32,
        binding_count: u32,
        buffers: *const vk::VkBuffer,
        offsets: *const vk::VkDeviceSize,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let buffers = unsafe {
            std::slice::from_raw_parts(
                buffers,
                binding_count as usize,
            )
        }.to_owned();

        for &buffer in buffers.iter() {
            fake_vulkan.check_buffer(buffer);
        }

        let offsets = unsafe {
            std::slice::from_raw_parts(
                offsets,
                binding_count as usize,
            )
        }.to_owned();

        fake_vulkan.add_command(
            command_buffer,
            Command::BindVertexBuffers {
                first_binding,
                buffers,
                offsets,
            },
        );
    }

    extern "C" fn bind_index_buffer(
        command_buffer: vk::VkCommandBuffer,
        buffer: vk::VkBuffer,
        offset: vk::VkDeviceSize,
        index_type: vk::VkIndexType,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.add_command(
            command_buffer,
            Command::BindIndexBuffer {
                buffer,
                offset,
                index_type,
            },
        );
    }

    extern "C" fn draw(
        command_buffer: vk::VkCommandBuffer,
        vertex_count: u32,
        instance_count: u32,
        first_vertex: u32,
        first_instance: u32,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.add_command(
            command_buffer,
            Command::Draw {
                vertex_count,
                instance_count,
                first_vertex,
                first_instance,
            },
        );
    }

    extern "C" fn draw_indexed(
        command_buffer: vk::VkCommandBuffer,
        index_count: u32,
        instance_count: u32,
        first_index: u32,
        vertex_offset: i32,
        first_instance: u32,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.add_command(
            command_buffer,
            Command::DrawIndexed {
                index_count,
                instance_count,
                first_index,
                vertex_offset,
                first_instance,
            },
        );
    }

    extern "C" fn dispatch(
        command_buffer: vk::VkCommandBuffer,
        x: u32,
        y: u32,
        z: u32
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.add_command(command_buffer, Command::Dispatch { x, y, z });
    }

    extern "C" fn clear_attachments(
        command_buffer: vk::VkCommandBuffer,
        attachment_count: u32,
        attachments: *const vk::VkClearAttachment,
        rect_count: u32,
        rects: *const vk::VkClearRect,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let attachments = unsafe {
            std::slice::from_raw_parts(
                attachments,
                attachment_count as usize,
            )
        }.iter().map(|attachment| {
            if attachment.aspectMask == vk::VK_IMAGE_ASPECT_COLOR_BIT {
                ClearAttachment::Color {
                    attachment: attachment.colorAttachment,
                    value: unsafe {
                        attachment.clearValue.color.float32.clone()
                    },
                }
            } else {
                assert!(
                    attachment.aspectMask
                        & (vk::VK_IMAGE_ASPECT_DEPTH_BIT
                           | vk::VK_IMAGE_ASPECT_STENCIL_BIT)
                        != 0
                );
                assert_eq!(
                    attachment.aspectMask
                        & !(vk::VK_IMAGE_ASPECT_DEPTH_BIT
                            | vk::VK_IMAGE_ASPECT_STENCIL_BIT),
                    0,
                );
                ClearAttachment::DepthStencil {
                    aspect_mask: attachment.aspectMask,
                    value: unsafe {
                        attachment.clearValue.depthStencil
                    },
                }
            }
        }).collect::<Vec<ClearAttachment>>();

        let rects = unsafe {
            std::slice::from_raw_parts(
                rects,
                rect_count as usize,
            )
        }.to_owned();

        fake_vulkan.add_command(
            command_buffer,
            Command::ClearAttachments {
                attachments,
                rects,
            }
        );
    }

    extern "C" fn pipeline_barrier(
        command_buffer: vk::VkCommandBuffer,
        src_stage_mask: vk::VkPipelineStageFlags,
        dst_stage_mask: vk::VkPipelineStageFlags,
        dependency_flags: vk::VkDependencyFlags,
        memory_barrier_count: u32,
        memory_barriers: *const vk::VkMemoryBarrier,
        buffer_memory_barrier_count: u32,
        buffer_memory_barriers: *const vk::VkBufferMemoryBarrier,
        image_memory_barrier_count: u32,
        image_memory_barriers: *const vk::VkImageMemoryBarrier,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let memory_barriers = unsafe {
            std::slice::from_raw_parts(
                memory_barriers,
                memory_barrier_count as usize,
            )
        }.to_owned();

        let buffer_memory_barriers = unsafe {
            std::slice::from_raw_parts(
                buffer_memory_barriers,
                buffer_memory_barrier_count as usize,
            )
        }.to_owned();

        for barrier in buffer_memory_barriers.iter() {
            fake_vulkan.check_buffer(barrier.buffer);
        }

        let image_memory_barriers = unsafe {
            std::slice::from_raw_parts(
                image_memory_barriers,
                image_memory_barrier_count as usize,
            )
        }.to_owned();

        for barrier in image_memory_barriers.iter() {
            fake_vulkan.check_image(barrier.image);
        }

        fake_vulkan.add_command(
            command_buffer,
            Command::PipelineBarrier {
                src_stage_mask,
                dst_stage_mask,
                dependency_flags,
                memory_barriers,
                buffer_memory_barriers,
                image_memory_barriers,
            },
        );
    }

    extern "C" fn copy_image_to_buffer(
        command_buffer: vk::VkCommandBuffer,
        src_image: vk::VkImage,
        src_image_layout: vk::VkImageLayout,
        dst_buffer: vk::VkBuffer,
        region_count: u32,
        regions: *const vk::VkBufferImageCopy,
    ) {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_image(src_image);
        fake_vulkan.check_buffer(dst_buffer);

        let regions = unsafe {
            std::slice::from_raw_parts(
                regions,
                region_count as usize,
            )
        }.to_owned();

        fake_vulkan.add_command(
            command_buffer,
            Command::CopyImageToBuffer {
                src_image,
                src_image_layout,
                dst_buffer,
                regions,
            },
        );
    }

    extern "C" fn push_constants(
        command_buffer: vk::VkCommandBuffer,
        layout: vk::VkPipelineLayout,
        stage_flags: vk::VkShaderStageFlags,
        offset: u32,
        size: u32,
        values: *const c_void,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let values = unsafe {
            std::slice::from_raw_parts(
                values as *const u8,
                size as usize,
            )
        }.to_owned();

        fake_vulkan.add_command(
            command_buffer,
            Command::PushConstants {
                layout,
                stage_flags,
                offset,
                values,
            },
        );
    }

    extern "C" fn bind_descriptor_sets(
        command_buffer: vk::VkCommandBuffer,
        pipeline_bind_point: vk::VkPipelineBindPoint,
        layout: vk::VkPipelineLayout,
        first_set: u32,
        descriptor_set_count: u32,
        descriptor_sets: *const vk::VkDescriptorSet,
        _dynamic_offset_count: u32,
        _dynamic_offsets: *const u32,
    ) {
        let fake_vulkan = FakeVulkan::current();

        let descriptor_sets = unsafe {
            std::slice::from_raw_parts(
                descriptor_sets,
                descriptor_set_count as usize,
            )
        }.to_owned();

        fake_vulkan.add_command(
            command_buffer,
            Command::BindDescriptorSets {
                pipeline_bind_point,
                layout,
                first_set,
                descriptor_sets,
            },
        );
    }

    extern "C" fn reset_fences(
        device: vk::VkDevice,
        fence_count: u32,
        fences: *const vk::VkFence,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let res = fake_vulkan.next_result("vkResetFences");

        if res != vk::VK_SUCCESS {
            return res;
        }

        let fences = unsafe {
            std::slice::from_raw_parts(
                fences,
                fence_count as usize,
            )
        };

        for &fence in fences.iter() {
            let HandleType::Fence { ref mut reset_count, .. } =
                fake_vulkan.get_handle_mut(fence).data
            else { unreachable!("bad handle"); };

            *reset_count += 1;
        }

        res
    }

    extern "C" fn wait_for_fences(
        device: vk::VkDevice,
        fence_count: u32,
        fences: *const vk::VkFence,
        _wait_all: vk::VkBool32,
        _timeout: u64,
    ) -> vk::VkResult {
        let fake_vulkan = FakeVulkan::current();

        fake_vulkan.check_device(device);

        let res = fake_vulkan.next_result("vkWaitForFences");

        if res != vk::VK_SUCCESS {
            return res;
        }

        let fences = unsafe {
            std::slice::from_raw_parts(
                fences,
                fence_count as usize,
            )
        };

        for &fence in fences.iter() {
            let HandleType::Fence { ref mut wait_count, .. } =
                fake_vulkan.get_handle_mut(fence).data
            else { unreachable!("bad handle"); };

            *wait_count += 1;
        }

        res
    }
}

impl Drop for FakeVulkan {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            let old_value = CURRENT_FAKE_VULKAN.with(|f| f.replace(None));
            // There should only be one FakeVulkan at a time so the
            // one we just dropped should be the one that was set for
            // the current thread.
            assert_eq!(old_value.unwrap(), self as *mut FakeVulkan);
        }
    }
}

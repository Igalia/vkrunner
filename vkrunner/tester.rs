// vkrunner
//
// Copyright (C) 2018, 2023 Neil Roberts
// Copyright (C) 2018 Intel Coporation
// Copyright (C) 2019 Google LLC
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
use crate::context::Context;
use crate::pipeline_set::{PipelineSet, RectangleVertex};
use crate::pipeline_key;
use crate::script::{Script, BufferType, Operation};
use crate::inspect::Inspector;
use crate::vk;
use crate::buffer::{self, MappedMemory, DeviceMemory, Buffer};
use crate::flush_memory::{self, flush_memory};
use crate::tolerance::Tolerance;
use crate::slot;
use crate::format::Component;
use crate::inspect;
use crate::format::Format;
use std::fmt;
use std::ptr;
use std::mem;
use std::rc::Rc;
use std::ffi::c_int;

#[derive(Debug)]
pub struct CommandError {
    pub line_num: usize,
    pub error: Error,
}

#[derive(Debug)]
pub enum Error {
    AllocateDescriptorSetsFailed,
    BeginCommandBufferFailed,
    EndCommandBufferFailed,
    ResetFencesFailed,
    QueueSubmitFailed,
    WaitForFencesFailed,
    ProbeFailed(ProbeFailedError),
    InvalidateMappedMemoryRangesFailed,
    BufferError(buffer::Error),
    FlushMemoryError(flush_memory::Error),
    CommandErrors(Vec<CommandError>),
    InvalidBufferBinding { desc_set: u32, binding: u32 },
    InvalidBufferOffset,
    SsboProbeFailed {
        slot_type: slot::Type,
        layout: slot::Layout,
        expected: Box<[u8]>,
        observed: Box<[u8]>,
    },
}

#[derive(Debug)]
pub struct ProbeFailedError {
    x: u32,
    y: u32,
    expected: [f64; 4],
    observed: [f64; 4],
    n_components: usize,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum State {
    /// Any rendering or computing has finished and we can read the
    /// buffers.
    Idle,
    /// The command buffer has begun
    CommandBuffer,
    /// The render pass has begun
    RenderPass,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::AllocateDescriptorSetsFailed => {
                write!(f, "vkAllocateDescriptorSets failed")
            },
            Error::BeginCommandBufferFailed => {
                write!(f, "vkBeginCommandBuffer failed")
            },
            Error::EndCommandBufferFailed => {
                write!(f, "vkEndCommandBuffer failed")
            },
            Error::ResetFencesFailed => {
                write!(f, "vkResetFences failed")
            },
            Error::QueueSubmitFailed => {
                write!(f, "vkQueueSubmit failed")
            },
            Error::WaitForFencesFailed => {
                write!(f, "vkWaitForFences failed")
            },
            Error::InvalidateMappedMemoryRangesFailed => {
                write!(f, "vkInvalidateMappedMemeroyRangesFailed failed")
            },
            Error::ProbeFailed(e) => e.fmt(f),
            &Error::SsboProbeFailed {
                slot_type,
                layout,
                ref expected,
                ref observed
            } => {
                write!(
                    f,
                    "SSBO probe failed\n\
                     \x20 Reference:",
                )?;
                write_slot(f, slot_type, layout, expected)?;
                write!(
                    f,
                    "\n\
                     \x20 Observed: ",
                )?;
                write_slot(f, slot_type, layout, observed)?;

                Ok(())
            },
            Error::BufferError(e) => e.fmt(f),
            Error::FlushMemoryError(e) => e.fmt(f),
            Error::CommandErrors(errors) => {
                for (num, e) in errors.iter().enumerate() {
                    if num > 0 {
                        writeln!(f)?;
                    }
                    write!(f, "line {}: ", e.line_num)?;
                    e.error.fmt(f)?;
                }
                Ok(())
            },
            Error::InvalidBufferBinding { desc_set, binding } => {
                write!(f, "Invalid buffer binding: {}:{}", desc_set, binding)
            },
            Error::InvalidBufferOffset => {
                write!(f, "Invalid buffer offset")
            },
        }
    }
}

fn write_slot(
    f: &mut fmt::Formatter,
    slot_type: slot::Type,
    layout: slot::Layout,
    values: &[u8],
) -> fmt::Result {
    let base_type = slot_type.base_type();
    let base_type_size = base_type.size();

    for offset in slot_type.offsets(layout) {
        let values = &values[offset..offset + base_type_size];
        write!(f, " {}", slot::BaseTypeInSlice::new(base_type, values))?;
    }

    Ok(())
}

fn format_pixel(f: &mut fmt::Formatter, pixel: &[f64]) -> fmt::Result {
    for component in pixel {
        write!(f, " {}", component)?;
    }

    Ok(())
}

impl fmt::Display for ProbeFailedError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Probe color at ({},{})\n\
            \x20 Expected:",
            self.x,
            self.y,
        )?;
        format_pixel(f, &self.expected[0..self.n_components])?;
        write!(
            f,
            "\n\
             \x20 Observed:"
        )?;
        format_pixel(f, &self.observed[0..self.n_components])
    }
}

impl From<buffer::Error> for Error {
    fn from(e: buffer::Error) -> Error {
        Error::BufferError(e)
    }
}

impl From<flush_memory::Error> for Error {
    fn from(e: flush_memory::Error) -> Error {
        Error::FlushMemoryError(e)
    }
}

#[derive(Debug)]
struct DescriptorSetVec<'a> {
    handles: Vec<vk::VkDescriptorSet>,
    // needed for the destructor
    pipeline_set: &'a PipelineSet,
    window: &'a Window,
}

impl<'a> DescriptorSetVec<'a> {
    fn new(
        window: &'a Window,
        pipeline_set: &'a PipelineSet,
    ) -> Result<DescriptorSetVec<'a>, Error> {
        let layouts = pipeline_set.descriptor_set_layouts();
        let mut handles = Vec::with_capacity(layouts.len());

        if !layouts.is_empty() {
            let allocate_info = vk::VkDescriptorSetAllocateInfo {
                sType: vk::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                pNext: ptr::null(),
                descriptorPool: pipeline_set.descriptor_pool().unwrap(),
                descriptorSetCount: layouts.len() as u32,
                pSetLayouts: layouts.as_ptr(),
            };

            let res = unsafe {
                window.device().vkAllocateDescriptorSets.unwrap()(
                    window.vk_device(),
                    ptr::addr_of!(allocate_info),
                    handles.as_mut_ptr(),
                )
            };

            if res == vk::VK_SUCCESS {
                // SAFETY: We ensured the buffer had the right
                // capacity when we constructed it and the call to
                // vkAllocateDescriptorSets should have filled it with
                // valid values.
                unsafe {
                    handles.set_len(layouts.len());
                }
            } else {
                return Err(Error::AllocateDescriptorSetsFailed);
            }
        }

        Ok(DescriptorSetVec {
            handles,
            pipeline_set,
            window,
        })
    }
}

impl<'a> Drop for DescriptorSetVec<'a> {
    fn drop(&mut self) {
        if self.handles.is_empty() {
            return;
        }

        unsafe {
            self.window.device().vkFreeDescriptorSets.unwrap()(
                self.window.vk_device(),
                self.pipeline_set.descriptor_pool().unwrap(),
                self.handles.len() as u32,
                self.handles.as_ptr(),
            );
        }
    }
}

#[derive(Debug)]
struct TestBuffer {
    map: MappedMemory,
    memory: DeviceMemory,
    buffer: Buffer,
    size: usize,
    // true if the buffer has been modified through the CPU-mapped
    // memory since the last command buffer submission.
    pending_write: bool,

}

impl TestBuffer {
    fn new(
        context: Rc<Context>,
        size: usize,
        usage: vk::VkBufferUsageFlagBits,
    ) -> Result<TestBuffer, Error> {
        let buffer = Buffer::new(Rc::clone(&context), size, usage)?;

        let memory = DeviceMemory::new_buffer(
            Rc::clone(&context),
            vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            buffer.buffer,
        )?;

        let map = MappedMemory::new(context, memory.memory)?;

        Ok(TestBuffer { map, memory, buffer, size, pending_write: false })
    }
}

fn allocate_buffer_objects(
    window: &Window,
    script: &Script,
) -> Result<Vec<TestBuffer>, Error> {
    let mut buffers = Vec::with_capacity(script.buffers().len());

    for script_buffer in script.buffers().iter() {
        let usage = match script_buffer.buffer_type {
            BufferType::Ubo => vk::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            BufferType::Ssbo => vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        };

        buffers.push(TestBuffer::new(
            Rc::clone(window.context()),
            script_buffer.size,
            usage,
        )?);
    }

    Ok(buffers)
}

fn write_descriptor_sets(
    window: &Window,
    script: &Script,
    buffers: &[TestBuffer],
    descriptor_sets: &[vk::VkDescriptorSet],
) {
    let script_buffers = script.buffers();

    let buffer_infos = buffers.iter()
        .map(|buffer| vk::VkDescriptorBufferInfo {
            buffer: buffer.buffer.buffer,
            offset: 0,
            range: vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
        })
        .collect::<Vec<_>>();

    let writes = script_buffers.iter()
        .enumerate()
        .map(|(buffer_num, buffer)| vk::VkWriteDescriptorSet {
            sType: vk::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            pNext: ptr::null(),
            dstSet: descriptor_sets[buffer.desc_set as usize],
            dstBinding: buffer.binding,
            dstArrayElement: 0,
            descriptorCount: 1,
            descriptorType: match buffer.buffer_type {
                BufferType::Ubo => vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                BufferType::Ssbo => vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            },
            pBufferInfo: buffer_infos[buffer_num..].as_ptr(),
            pImageInfo: ptr::null(),
            pTexelBufferView: ptr::null(),
        })
        .collect::<Vec<_>>();

    unsafe {
        window.device().vkUpdateDescriptorSets.unwrap()(
            window.vk_device(),
            writes.len() as u32,
            writes.as_ptr(),
            0, // descriptorCopyCount
            ptr::null(), // pDescriptorCopies
        );
    }
}

fn compare_pixel(
    pixel_a: &[f64],
    pixel_b: &[f64],
    tolerance: &Tolerance,
) -> bool {
    std::iter::zip(pixel_a, pixel_b)
        .enumerate()
        .all(|(component, (&a, &b))| tolerance.equal(component, a, b))
}

#[derive(Debug)]
struct Tester<'a> {
    window: &'a Window,
    pipeline_set: &'a PipelineSet,
    script: &'a Script,
    buffer_objects: Vec<TestBuffer>,
    test_buffers: Vec<TestBuffer>,
    descriptor_sets: DescriptorSetVec<'a>,
    bound_pipeline: Option<usize>,
    bo_descriptor_set_bound: bool,
    first_render: bool,
    state: State,
    vbo_buffer: Option<TestBuffer>,
    index_buffer: Option<TestBuffer>,
    inspector: Option<Inspector>,
}

impl<'a> Tester<'a> {
    fn new(
        window: &'a Window,
        pipeline_set: &'a PipelineSet,
        script: &'a Script,
        inspector: Option<Inspector>,
    ) -> Result<Tester<'a>, Error> {
        let buffer_objects = allocate_buffer_objects(window, script)?;
        let descriptor_sets = DescriptorSetVec::new(window, pipeline_set)?;

        write_descriptor_sets(
            window,
            script,
            &buffer_objects,
            &descriptor_sets.handles,
        );

        Ok(Tester {
            window,
            pipeline_set,
            script,
            buffer_objects,
            test_buffers: Vec::new(),
            descriptor_sets,
            bound_pipeline: None,
            bo_descriptor_set_bound: false,
            first_render: true,
            state: State::Idle,
            vbo_buffer: None,
            index_buffer: None,
            inspector,
        })
    }

    fn add_ssbo_barriers(&mut self) {
        let barriers = self.buffer_objects.iter().enumerate()
            .filter_map(|(buffer_num, buffer)| {
                match self.script.buffers()[buffer_num].buffer_type {
                    BufferType::Ssbo => Some(vk::VkBufferMemoryBarrier {
                        sType: vk::VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                        pNext: ptr::null(),
                        srcAccessMask: vk::VK_ACCESS_SHADER_WRITE_BIT,
                        dstAccessMask: vk::VK_ACCESS_HOST_READ_BIT,
                        srcQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
                        dstQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
                        buffer: buffer.buffer.buffer,
                        offset: 0,
                        size: vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
                    }),
                    _ => None,
                }
            })
            .collect::<Vec<_>>();

        if !barriers.is_empty() {
            unsafe {
                self.window.device().vkCmdPipelineBarrier.unwrap()(
                    self.window.context().command_buffer(),
                    vk::VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    vk::VK_PIPELINE_STAGE_HOST_BIT,
                    0, // dependencyFlags
                    0, // memoryBarrierCount
                    ptr::null(), // pMemoryBarriers
                    barriers.len() as u32, // bufferMemoryBarrierCount
                    barriers.as_ptr(), // pBufferMemoryBarriers
                    0, // imageMemoryBarrierCount
                    ptr::null(), // pImageMemoryBarriers
                );
            }
        }
    }

    fn flush_buffers(&mut self) -> Result<(), Error> {
        for buffer in self.buffer_objects.iter_mut() {
            if !buffer.pending_write {
                continue;
            }

            buffer.pending_write = false;

            flush_memory(
                self.window.context(),
                buffer.memory.memory_type_index as usize,
                buffer.memory.memory,
                0, // offset
                vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
            )?;
        }

        Ok(())
    }

    fn begin_command_buffer(&mut self) -> Result<(), Error> {
        let begin_command_buffer_info = vk::VkCommandBufferBeginInfo {
            sType: vk::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            pNext: ptr::null(),
            flags: 0,
            pInheritanceInfo: ptr::null(),
        };

        let res = unsafe {
            self.window.device().vkBeginCommandBuffer.unwrap()(
                self.window.context().command_buffer(),
                ptr::addr_of!(begin_command_buffer_info),
            )
        };

        if res == vk::VK_SUCCESS {
            self.bound_pipeline = None;
            self.bo_descriptor_set_bound = false;

            Ok(())
        } else {
            Err(Error::BeginCommandBufferFailed)
        }
    }

    fn reset_fence(&self) -> Result<(), Error> {
        let fence = self.window.context().fence();

        let res = unsafe {
            self.window.device().vkResetFences.unwrap()(
                self.window.vk_device(),
                1, // fenceCount,
                ptr::addr_of!(fence),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(())
        } else {
            Err(Error::ResetFencesFailed)
        }
    }

    fn queue_submit(&self) -> Result<(), Error> {
        let command_buffer = self.window.context().command_buffer();
        let wait_dst_stage_mask = vk::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        let submit_info = vk::VkSubmitInfo {
            sType: vk::VK_STRUCTURE_TYPE_SUBMIT_INFO,
            pNext: ptr::null(),
            waitSemaphoreCount: 0,
            pWaitSemaphores: ptr::null(),
            pWaitDstStageMask: ptr::addr_of!(wait_dst_stage_mask),
            commandBufferCount: 1,
            pCommandBuffers: ptr::addr_of!(command_buffer),
            signalSemaphoreCount: 0,
            pSignalSemaphores: ptr::null(),
        };

        let res = unsafe {
            self.window.device().vkQueueSubmit.unwrap()(
                self.window.context().queue(),
                1, // submitCount
                ptr::addr_of!(submit_info),
                self.window.context().fence(),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(())
        } else {
            Err(Error::QueueSubmitFailed)
        }
    }

    fn wait_for_fence(&self) -> Result<(), Error> {
        let fence = self.window.context().fence();

        let res = unsafe {
            self.window.device().vkWaitForFences.unwrap()(
                self.window.vk_device(),
                1, // fenceCount
                ptr::addr_of!(fence),
                vk::VK_TRUE, // waitAll
                u64::MAX, // timeout
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(())
        } else {
            Err(Error::WaitForFencesFailed)
        }
    }

    fn invalidate_window_linear_memory(&self) -> Result<(), Error> {
        if !self.window.need_linear_memory_invalidate() {
            return Ok(());
        }

        let memory_range = vk::VkMappedMemoryRange {
            sType: vk::VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            pNext: ptr::null(),
            memory: self.window.linear_memory(),
            offset: 0,
            size: vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
        };

        let res = unsafe {
            self.window.device().vkInvalidateMappedMemoryRanges.unwrap()(
                self.window.vk_device(),
                1, // memoryRangeCount
                ptr::addr_of!(memory_range),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(())
        } else {
            Err(Error::InvalidateMappedMemoryRangesFailed)
        }
    }

    fn invalidate_ssbos(&self) -> Result<(), Error> {
        let memory_properties = self.window.context().memory_properties();

        let memory_ranges = self.buffer_objects.iter()
            .enumerate()
            .filter_map(|(buffer_num, buffer)| {
                if self.script.buffers()[buffer_num].buffer_type
                    != BufferType::Ssbo
                {
                    return None;
                }

                let memory_type = &memory_properties
                    .memoryTypes[buffer.memory.memory_type_index as usize];

                // We don’t need to do anything if the memory is
                // already coherent
                if memory_type.propertyFlags
                    & vk::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    != 0
                {
                    return None;
                }

                Some(vk::VkMappedMemoryRange {
                    sType: vk::VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                    pNext: ptr::null(),
                    memory: buffer.memory.memory,
                    offset: 0,
                    size: vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
                })
            })
            .collect::<Vec<_>>();

        if memory_ranges.is_empty() {
            Ok(())
        } else {
            let res = unsafe {
                self.window.device().vkInvalidateMappedMemoryRanges.unwrap()(
                    self.window.vk_device(),
                    memory_ranges.len() as u32,
                    memory_ranges.as_ptr(),
                )
            };

            if res == vk::VK_SUCCESS {
                Ok(())
            } else {
                Err(Error::InvalidateMappedMemoryRangesFailed)
            }
        }
    }

    fn end_command_buffer(&mut self) -> Result<(), Error> {
        self.flush_buffers()?;
        self.add_ssbo_barriers();

        let res = unsafe {
            self.window.device().vkEndCommandBuffer.unwrap()(
                self.window.context().command_buffer(),
            )
        };

        if res != vk::VK_SUCCESS {
            return Err(Error::EndCommandBufferFailed);
        }

        self.reset_fence()?;
        self.queue_submit()?;
        self.wait_for_fence()?;
        self.invalidate_window_linear_memory()?;
        self.invalidate_ssbos()?;

        Ok(())
    }

    fn begin_render_pass(&mut self) {
        let render_pass_index = !self.first_render as usize;
        let render_pass = self.window.render_passes()[render_pass_index];
        let window_format = self.window.format();

        let render_pass_begin_info = vk::VkRenderPassBeginInfo {
            sType: vk::VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            pNext: ptr::null(),
            renderPass: render_pass,
            framebuffer: self.window.framebuffer(),
            renderArea: vk::VkRect2D {
                offset: vk::VkOffset2D { x: 0, y: 0 },
                extent: vk::VkExtent2D {
                    width: window_format.width as u32,
                    height: window_format.height as u32,
                },
            },
            clearValueCount: 0,
            pClearValues: ptr::null(),
        };

        unsafe {
            self.window.device().vkCmdBeginRenderPass.unwrap()(
                self.window.context().command_buffer(),
                ptr::addr_of!(render_pass_begin_info),
                vk::VK_SUBPASS_CONTENTS_INLINE,
            );
        }

        self.first_render = false;
    }

    fn add_render_finish_barrier(&self) {
        // Image barrier: transition the layout but also ensure:
        // - rendering is complete before vkCmdCopyImageToBuffer (below) and
        // before any future color attachment accesses
        // - the color attachment writes are visible to vkCmdCopyImageToBuffer
        // and to any future color attachment accesses */
        let render_finish_barrier = vk::VkImageMemoryBarrier {
            sType: vk::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            pNext: ptr::null(),
            srcAccessMask: vk::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            dstAccessMask: vk::VK_ACCESS_TRANSFER_READ_BIT
                | vk::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | vk::VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            oldLayout: vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            newLayout: vk::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            srcQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            dstQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            image: self.window.color_image(),
            subresourceRange: vk::VkImageSubresourceRange {
                aspectMask: vk::VK_IMAGE_ASPECT_COLOR_BIT,
                baseMipLevel: 0,
                levelCount: 1,
                baseArrayLayer: 0,
                layerCount: 1
            },
        };

        unsafe {
            self.window.device().vkCmdPipelineBarrier.unwrap()(
                self.window.context().command_buffer(),
                vk::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                vk::VK_PIPELINE_STAGE_TRANSFER_BIT
                    | vk::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, // dependencyFlags
                0, // memoryBarrierCount
                ptr::null(), // pMemoryBarriers
                0, // bufferMemoryBarrierCount
                ptr::null(), // pBufferMemoryBarriers
                1, // imageMemoryBarrierCount
                ptr::addr_of!(render_finish_barrier),
            );
        }
    }

    fn add_copy_to_linear_buffer(&self) {
        let window_format = self.window.format();

        let copy_region = vk::VkBufferImageCopy {
            bufferOffset: 0,
            bufferRowLength: window_format.width as u32,
            bufferImageHeight: window_format.height as u32,
            imageSubresource: vk::VkImageSubresourceLayers {
                aspectMask: vk::VK_IMAGE_ASPECT_COLOR_BIT,
                mipLevel: 0,
                baseArrayLayer: 0,
                layerCount: 1,
            },
            imageOffset: vk::VkOffset3D { x: 0, y: 0, z: 0 },
            imageExtent: vk::VkExtent3D {
                width: window_format.width as u32,
                height: window_format.height as u32,
                depth: 1 as u32
            },
        };

        unsafe {
            self.window.device().vkCmdCopyImageToBuffer.unwrap()(
                self.window.context().command_buffer(),
                self.window.color_image(),
                vk::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                self.window.linear_buffer(),
                1, // regionCount
                ptr::addr_of!(copy_region),
            );
        }
    }

    fn add_copy_finish_barrier(&self) {
        // Image barrier: transition the layout back but also ensure:
        // - the copy image operation (above) completes before any future color
        // attachment operations
        // No memory dependencies are needed because the first set of operations
        // are reads.
        let render_finish_barrier = vk::VkImageMemoryBarrier {
            sType: vk::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            pNext: ptr::null(),
            srcAccessMask: 0,
            dstAccessMask: 0,
            oldLayout: vk::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            newLayout: vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            srcQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            dstQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            image: self.window.color_image(),
            subresourceRange: vk::VkImageSubresourceRange {
                aspectMask: vk::VK_IMAGE_ASPECT_COLOR_BIT,
                baseMipLevel: 0,
                levelCount: 1,
                baseArrayLayer: 0,
                layerCount: 1
            },
        };

        unsafe {
            self.window.device().vkCmdPipelineBarrier.unwrap()(
                self.window.context().command_buffer(),
                vk::VK_PIPELINE_STAGE_TRANSFER_BIT,
                vk::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, // dependencyFlags
                0, // memoryBarrierCount
                ptr::null(), // pMemoryBarriers
                0, // bufferMemoryBarrierCount
                ptr::null(), // pBufferMemoryBarriers
                1, // imageMemoryBarrierCount
                ptr::addr_of!(render_finish_barrier),
            );
        }
    }

    fn add_write_finish_buffer_memory_barrier(&self) {
        // Buffer barrier: ensure the device transfer writes have
        // completed before the host reads and are visible to host
        // reads.
        let write_finish_buffer_memory_barrier = vk::VkBufferMemoryBarrier {
            sType: vk::VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            pNext: ptr::null(),
            srcAccessMask: vk::VK_ACCESS_TRANSFER_WRITE_BIT,
            dstAccessMask: vk::VK_ACCESS_HOST_READ_BIT,
            srcQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            dstQueueFamilyIndex: vk::VK_QUEUE_FAMILY_IGNORED as u32,
            buffer: self.window.linear_buffer(),
            offset: 0,
            size: vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
        };

        unsafe {
            self.window.device().vkCmdPipelineBarrier.unwrap()(
                self.window.context().command_buffer(),
                vk::VK_PIPELINE_STAGE_TRANSFER_BIT,
                vk::VK_PIPELINE_STAGE_HOST_BIT,
                0, // dependencyFlags
                0, // memoryBarrierCount
                ptr::null(), // pMemoryBarriers
                1, // bufferMemoryBarrierCount
                ptr::addr_of!(write_finish_buffer_memory_barrier),
                0, // imageMemoryBarrierCount
                ptr::null(), // pImageMemoryBarriers
            );
        }
    }

    fn end_render_pass(&self) {
        unsafe {
            self.window.device().vkCmdEndRenderPass.unwrap()(
                self.window.context().command_buffer(),
            );
        }

        self.add_render_finish_barrier();
        self.add_copy_to_linear_buffer();
        self.add_copy_finish_barrier();
        self.add_write_finish_buffer_memory_barrier();
    }

    fn forward_state(&mut self) -> Result<(), Error> {
        match &self.state {
            State::Idle => {
                self.begin_command_buffer()?;
                self.state = State::CommandBuffer;
            },
            State::CommandBuffer => {
                self.begin_render_pass();
                self.state = State::RenderPass;
            },
            State::RenderPass => unreachable!(
                "Tried to advance after last state"
            ),
        }

        Ok(())
    }

    fn backward_state(&mut self) -> Result<(), Error> {
        match &self.state {
            State::Idle => unreachable!(
                "Tried to go backward to before the first state"
            ),
            State::CommandBuffer => {
                self.end_command_buffer()?;
                self.state = State::Idle;
            },
            State::RenderPass => {
                self.end_render_pass();
                self.state = State::CommandBuffer;
            },
        }

        Ok(())
    }

    fn goto_state(&mut self, state: State) -> Result<(), Error> {
        while (self.state as usize) < state as usize {
            self.forward_state()?;
        }
        while (self.state as usize) > state as usize {
            self.backward_state()?;
        }

        Ok(())
    }

    fn bind_bo_descriptor_set_at_binding_point(
        &self,
        binding_point: vk::VkPipelineBindPoint
    ) {
        unsafe {
            self.window.device().vkCmdBindDescriptorSets.unwrap()(
                self.window.context().command_buffer(),
                binding_point,
                self.pipeline_set.layout(),
                0, // firstSet
                self.descriptor_sets.handles.len() as u32,
                self.descriptor_sets.handles.as_ptr(),
                0, // dynamicOffsetCount
                ptr::null(), // pDynamicOffsets
            );
        }
    }

    fn bind_bo_descriptor_set(&mut self) {
        if self.bo_descriptor_set_bound
            || self.descriptor_sets.handles.is_empty()
        {
            return;
        }

        if self.pipeline_set.stages() & !vk::VK_SHADER_STAGE_COMPUTE_BIT != 0 {
            self.bind_bo_descriptor_set_at_binding_point(
                vk::VK_PIPELINE_BIND_POINT_GRAPHICS,
            );
        }

        if self.pipeline_set.stages() & vk::VK_SHADER_STAGE_COMPUTE_BIT != 0 {
            self.bind_bo_descriptor_set_at_binding_point(
                vk::VK_PIPELINE_BIND_POINT_COMPUTE,
            );
        }

        self.bo_descriptor_set_bound = true;
    }

    fn bind_pipeline(&mut self, pipeline_num: usize) {
        if Some(pipeline_num) == self.bound_pipeline {
            return;
        }

        let key = &self.script.pipeline_keys()[pipeline_num];

        let bind_point = match key.pipeline_type() {
            pipeline_key::Type::Graphics => vk::VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_key::Type::Compute => vk::VK_PIPELINE_BIND_POINT_COMPUTE,
        };

        unsafe {
            self.window.device().vkCmdBindPipeline.unwrap()(
                self.window.context().command_buffer(),
                bind_point,
                self.pipeline_set.pipelines()[pipeline_num],
            );
        }

        self.bound_pipeline = Some(pipeline_num);
    }

    fn get_buffer_object(
        &mut self,
        desc_set: u32,
        binding: u32,
    ) -> Result<&mut TestBuffer, Error> {
        match self.script
            .buffers()
            .binary_search_by(|buffer| {
                buffer.desc_set
                    .cmp(&desc_set)
                    .then_with(|| buffer.binding.cmp(&binding))
            })
        {
            Ok(buffer_num) => Ok(&mut self.buffer_objects[buffer_num]),
            Err(_) => Err(Error::InvalidBufferBinding { desc_set, binding }),
        }
    }

    fn get_vbo_buffer(&mut self) -> Result<Option<&TestBuffer>, Error> {
        if let Some(ref buffer) = self.vbo_buffer {
            Ok(Some(buffer))
        } else if let Some(vbo) = self.script.vertex_data() {
            let buffer = TestBuffer::new(
                Rc::clone(self.window.context()),
                vbo.raw_data().len(),
                vk::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            )?;

            unsafe {
                std::slice::from_raw_parts_mut(
                    buffer.map.pointer as *mut u8,
                    buffer.size
                ).copy_from_slice(vbo.raw_data());
            }

            flush_memory(
                self.window.context(),
                buffer.memory.memory_type_index as usize,
                buffer.memory.memory,
                0, // offset
                vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
            )?;

            Ok(Some(&*self.vbo_buffer.insert(buffer)))
        } else {
            Ok(None)
        }
    }

    fn get_index_buffer(&mut self) -> Result<&TestBuffer, Error> {
        match self.index_buffer {
            Some(ref buffer) => Ok(buffer),
            None => {
                let indices = self.script.indices();

                let buffer = TestBuffer::new(
                    Rc::clone(self.window.context()),
                    indices.len() * mem::size_of::<u16>(),
                    vk::VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                )?;

                unsafe {
                    std::slice::from_raw_parts_mut(
                        buffer.map.pointer as *mut u16,
                        indices.len(),
                    ).copy_from_slice(indices);
                }

                flush_memory(
                    self.window.context(),
                    buffer.memory.memory_type_index as usize,
                    buffer.memory.memory,
                    0, // offset
                    vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
                )?;

                Ok(&*self.index_buffer.insert(buffer))
            }
        }
    }

    fn draw_rect(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::DrawRect { x, y, w, h, pipeline_key } = op else {
            unreachable!("bad op");
        };

        let buffer = TestBuffer::new(
            Rc::clone(self.window.context()),
            mem::size_of::<RectangleVertex>() * 4,
            vk::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        )?;

        self.goto_state(State::RenderPass)?;

        let mut v: *mut RectangleVertex = buffer.map.pointer.cast();

        unsafe {
            *v = RectangleVertex {
                x: x,
                y: y,
                z: 0.0,
            };
            v = v.add(1);

            *v = RectangleVertex {
                x: x + w,
                y: y,
                z: 0.0,
            };
            v = v.add(1);

            *v = RectangleVertex {
                x: x,
                y: y + h,
                z: 0.0,
            };
            v = v.add(1);

            *v = RectangleVertex {
                x: x + w,
                y: y + h,
                z: 0.0,
            };
        }

        flush_memory(
            self.window.context(),
            buffer.memory.memory_type_index as usize,
            buffer.memory.memory,
            0, // offset
            vk::VK_WHOLE_SIZE as vk::VkDeviceSize,
        )?;

        self.bind_bo_descriptor_set();
        self.bind_pipeline(pipeline_key);

        let command_buffer = self.window.context().command_buffer();
        let buffer_handle = buffer.buffer.buffer;
        let offset = 0;

        unsafe {
            self.window.device().vkCmdBindVertexBuffers.unwrap()(
                command_buffer,
                0, // firstBinding
                1, // bindingCount
                ptr::addr_of!(buffer_handle),
                ptr::addr_of!(offset),
            );
            self.window.device().vkCmdDraw.unwrap()(
                command_buffer,
                4, // vertexCount
                1, // instanceCount
                0, // firstVertex
                0, // firstinstance
            );
        }

        self.test_buffers.push(buffer);

        Ok(())
    }

    fn draw_arrays(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::DrawArrays {
            indexed,
            vertex_count,
            instance_count,
            first_vertex,
            first_instance,
            pipeline_key,
            ..
        } = op else {
            unreachable!("bad op");
        };

        self.goto_state(State::RenderPass)?;

        let context = Rc::clone(self.window.context());

        if let Some(buffer) = self.get_vbo_buffer()? {
            let offset = 0;

            unsafe {
                context.device().vkCmdBindVertexBuffers.unwrap()(
                    context.command_buffer(),
                    0, // firstBinding
                    1, // bindingCount
                    ptr::addr_of!(buffer.buffer.buffer),
                    ptr::addr_of!(offset)
                );
            }
        }

        self.bind_bo_descriptor_set();
        self.bind_pipeline(pipeline_key);

        if indexed {
            let index_buffer = self.get_index_buffer()?;

            unsafe {
                context.device().vkCmdBindIndexBuffer.unwrap()(
                    context.command_buffer(),
                    index_buffer.buffer.buffer,
                    0, // offset
                    vk::VK_INDEX_TYPE_UINT16,
                );
                context.device().vkCmdDrawIndexed.unwrap()(
                    context.command_buffer(),
                    vertex_count,
                    instance_count,
                    0, // firstIndex
                    first_vertex as i32,
                    first_instance,
                );
            }
        } else {
            unsafe {
                context.device().vkCmdDraw.unwrap()(
                    context.command_buffer(),
                    vertex_count,
                    instance_count,
                    first_vertex,
                    first_instance,
                );
            }
        }

        Ok(())
    }

    fn dispatch_compute(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::DispatchCompute { x, y, z, pipeline_key } = op else {
            unreachable!("bad op");
        };

        self.goto_state(State::CommandBuffer)?;

        self.bind_bo_descriptor_set();
        self.bind_pipeline(pipeline_key);

        unsafe {
            self.window.device().vkCmdDispatch.unwrap()(
                self.window.context().command_buffer(),
                x,
                y,
                z,
            );
        }

        Ok(())
    }

    fn probe_rect(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::ProbeRect {
            n_components,
            x,
            y,
            w,
            h,
            ref color,
            ref tolerance,
        } = op else {
            unreachable!("bad op");
        };

        // End the render to copy the framebuffer into the linear buffer
        self.goto_state(State::Idle)?;

        let linear_memory_map: *const u8 =
            self.window.linear_memory_map().cast();
        let stride = self.window.linear_memory_stride();
        let format = self.window.format().color_format;
        let format_size = format.size();
        let n_components = n_components as usize;

        for y_offset in 0..h {
            let mut p = unsafe {
                linear_memory_map.add(
                    (y_offset + y) as usize * stride + x as usize * format_size
                )
            };

            for x_offset in 0..w {
                let source = unsafe {
                    std::slice::from_raw_parts(p, format_size)
                };

                let pixel = format.load_pixel(source);

                if !compare_pixel(
                    &pixel[0..n_components],
                    &color[0..n_components],
                    tolerance,
                ) {
                    return Err(Error::ProbeFailed(ProbeFailedError {
                        x: x + x_offset,
                        y: y + y_offset,
                        expected: color.clone(),
                        observed: pixel,
                        n_components,
                    }));
                }

                unsafe {
                    p = p.add(format_size);
                }
            }
        }

        Ok(())
    }

    fn probe_ssbo(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::ProbeSsbo {
            desc_set,
            binding,
            comparison,
            offset,
            slot_type,
            layout,
            ref values,
            ref tolerance,
        } = op else {
            unreachable!("bad op");
        };

        self.goto_state(State::Idle)?;

        let buffer = self.get_buffer_object(desc_set, binding)?;

        let buffer_slice = unsafe {
            std::slice::from_raw_parts(
                buffer.map.pointer as *const u8,
                buffer.size,
            )
        };

        let type_size = slot_type.size(layout);
        let observed_stride = slot_type.array_stride(layout);
        // The values are tightly packed in the operation buffer so we
        // don’t want to use the observed_stride
        let n_values = values.len() / type_size;

        if offset
            + (n_values - 1) * observed_stride
            + type_size
            > buffer_slice.len()
        {
            return Err(Error::InvalidBufferOffset);
        }

        let buffer_slice = &buffer_slice[offset..];

        for i in 0..n_values {
            let observed = &buffer_slice[i * observed_stride
                                         ..i * observed_stride + type_size];
            let expected = &values[i * type_size..(i + 1) * type_size];

            if !comparison.compare(
                tolerance,
                slot_type,
                layout,
                observed,
                expected,
            ) {
                return Err(Error::SsboProbeFailed {
                    slot_type,
                    layout,
                    expected: expected.into(),
                    observed: observed.into(),
                });
            }
        }

        Ok(())
    }

    fn set_push_command(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::SetPushCommand { offset, ref data } = op else {
            unreachable!("bad op");
        };

        if (self.state as usize) < State::CommandBuffer as usize {
            self.goto_state(State::CommandBuffer)?;
        }

        unsafe {
            self.window.device().vkCmdPushConstants.unwrap()(
                self.window.context().command_buffer(),
                self.pipeline_set.layout(),
                self.pipeline_set.stages(),
                offset as u32,
                data.len() as u32,
                data.as_ptr().cast(),
            );
        }

        Ok(())
    }

    fn set_buffer_data(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::SetBufferData {
            desc_set,
            binding,
            offset,
            ref data
        } = op else {
            unreachable!("bad op");
        };

        let buffer = self.get_buffer_object(desc_set, binding)
            .expect(
                "The script parser should make a buffer mentioned by \
                 any buffer data command and the tester should make a \
                 buffer for every buffer described by the script"
            );

        let buffer_slice = unsafe {
            std::slice::from_raw_parts_mut(
                (buffer.map.pointer as *mut u8).add(offset),
                data.len(),
            )
        };

        buffer_slice.copy_from_slice(data);

        buffer.pending_write = true;

        Ok(())
    }

    fn clear(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        let &Operation::Clear { ref color, depth, stencil } = op else {
            unreachable!("bad op");
        };

        let window_format = self.window.format();

        let depth_stencil_flags = match window_format.depth_stencil_format {
            Some(format) => {
                format.parts().iter().map(|part| match part.component {
                    Component::D => vk::VK_IMAGE_ASPECT_DEPTH_BIT,
                    Component::S => vk::VK_IMAGE_ASPECT_STENCIL_BIT,
                    _ => 0,
                }).fold(0, |a, b| a | b)
            },
            None => 0,
        };

        self.goto_state(State::RenderPass)?;

        let clear_attachments = [
            vk::VkClearAttachment {
                aspectMask: vk::VK_IMAGE_ASPECT_COLOR_BIT,
                colorAttachment: 0,
                clearValue: vk::VkClearValue {
                    color: vk::VkClearColorValue {
                        float32: color.clone(),
                    },
                },
            },
            vk::VkClearAttachment {
                aspectMask: depth_stencil_flags,
                colorAttachment: 0,
                clearValue: vk::VkClearValue {
                    depthStencil: vk::VkClearDepthStencilValue {
                        depth,
                        stencil,
                    },
                },
            },
        ];

        let clear_rect = vk::VkClearRect {
            rect: vk::VkRect2D {
                offset: vk::VkOffset2D { x: 0, y: 0 },
                extent: vk::VkExtent2D {
                    width: self.window.format().width as u32,
                    height: self.window.format().height as u32,
                },
            },
            baseArrayLayer: 0,
            layerCount: 1,
        };

        let n_attachments = 1 + (depth_stencil_flags != 0) as usize;

        unsafe {
            self.window.device().vkCmdClearAttachments.unwrap()(
                self.window.context().command_buffer(),
                n_attachments as u32,
                ptr::addr_of!(clear_attachments[0]),
                1, // rectCount
                ptr::addr_of!(clear_rect),
            );
        }

        Ok(())
    }

    fn run_operation(
        &mut self,
        op: &Operation,
    ) -> Result<(), Error> {
        match op {
            Operation::DrawRect { .. } => self.draw_rect(op),
            Operation::DrawArrays { .. } => self.draw_arrays(op),
            Operation::DispatchCompute { .. } => self.dispatch_compute(op),
            Operation::ProbeRect { .. } => self.probe_rect(op),
            Operation::ProbeSsbo { .. } => self.probe_ssbo(op),
            Operation::SetPushCommand { .. } => self.set_push_command(op),
            Operation::SetBufferData { .. } => self.set_buffer_data(op),
            Operation::Clear { .. } => self.clear(op),
        }
    }

    fn inspect(&self) {
        let Some(inspector) = self.inspector.as_ref() else { return; };

        let buffers = self.buffer_objects
            .iter()
            .enumerate()
            .map(|(buffer_num, buffer)| {
                inspect::Buffer {
                    binding: self.script.buffers()[buffer_num].binding as c_int,
                    size: buffer.size,
                    data: buffer.map.pointer,
                }
            })
            .collect::<Vec<_>>();

        let window_format = self.window.format();

        let data = inspect::Data {
            color_buffer: inspect::Image {
                width: window_format.width as c_int,
                height: window_format.height as c_int,
                stride: self.window.linear_memory_stride(),
                format: window_format.color_format as *const Format,
                data: self.window.linear_memory_map(),
            },
            n_buffers: buffers.len(),
            buffers: if buffers.is_empty() {
                ptr::null()
            } else {
                buffers.as_ptr()
            },
        };

        inspector.inspect(&data);
    }
}

pub fn run(
    window: &Window,
    pipeline_set: &PipelineSet,
    script: &Script,
    inspector: Option<Inspector>,
) -> Result<(), Error> {
    let mut tester = Tester::new(window, pipeline_set, script, inspector)?;
    let mut errors = Vec::new();

    for command in script.commands().iter() {
        if let Err(e) = tester.run_operation(&command.op) {
            errors.push(CommandError {
                line_num: command.line_num,
                error: e,
            });
        }
    }

    if let Err(error) = tester.goto_state(State::Idle) {
        let line_num = match script.commands().last() {
            Some(command) => command.line_num,
            None => 1,
        };

        errors.push(CommandError { line_num, error });
    }

    tester.inspect();

    if errors.is_empty() {
        Ok(())
    } else {
        Err(Error::CommandErrors(errors))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, Command, HandleType, ClearAttachment};
    use crate::requirements::Requirements;
    use crate::logger::Logger;
    use crate::source::Source;
    use crate::window_format::WindowFormat;
    use crate::config::Config;
    use std::ffi::c_void;

    #[derive(Debug)]
    struct TestData {
        pipeline_set: PipelineSet,
        window: Rc<Window>,
        context: Rc<Context>,
        fake_vulkan: Box<FakeVulkan>,
    }

    impl TestData {
        fn new_full(
            source: &str,
            inspector: Option<Inspector>,
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
            fake_vulkan.physical_devices[0].format_properties.insert(
                vk::VK_FORMAT_D24_UNORM_S8_UINT,
                vk::VkFormatProperties {
                    linearTilingFeatures: 0,
                    optimalTilingFeatures:
                    vk::VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
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

            let source = Source::from_string(source.to_string());
            let script = Script::load(&Config::new(), &source).unwrap();

            let window = Rc::new(Window::new(
                Rc::clone(&context),
                script.window_format(),
            ).unwrap());

            let mut logger = Logger::new(None, ptr::null_mut());

            let pipeline_set = PipelineSet::new(
                &mut logger,
                Rc::clone(&window),
                &script,
                false, // show_disassembly
            ).unwrap();

            run(
                &window,
                &pipeline_set,
                &script,
                inspector,
            )?;

            Ok(TestData {
                pipeline_set,
                window,
                context,
                fake_vulkan,
            })
        }

        fn new(source: &str) -> Result<TestData, Error> {
            TestData::new_full(
                source,
                None, // inspector
            )
        }
    }

    #[test]
    fn rectangle() {
        let test_data = TestData::new(
            "[test]\n\
             draw rect -1 -1 2 2"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        let &Command::BeginRenderPass(ref begin_info) = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(begin_info.renderPass, test_data.window.render_passes()[0]);
        assert_eq!(begin_info.framebuffer, test_data.window.framebuffer());
        assert_eq!(begin_info.renderArea.offset.x, 0);
        assert_eq!(begin_info.renderArea.offset.y, 0);
        assert_eq!(
            begin_info.renderArea.extent.width as usize,
            WindowFormat::default().width,
        );
        assert_eq!(
            begin_info.renderArea.extent.height as usize,
            WindowFormat::default().height,
        );
        assert_eq!(begin_info.clearValueCount, 0);

        let &Command::BindPipeline {
            bind_point,
            pipeline,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(test_data.pipeline_set.pipelines().len(), 1);
        assert_eq!(test_data.pipeline_set.pipelines()[0], pipeline);
        assert_eq!(bind_point, vk::VK_PIPELINE_BIND_POINT_GRAPHICS);

        let &Command::BindVertexBuffers {
            first_binding,
            ref buffers,
            ref offsets,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(first_binding, 0);
        assert_eq!(buffers.len(), 1);
        assert_eq!(offsets, &[0]);

        let HandleType::Buffer { memory: Some(memory), .. } =
            test_data.fake_vulkan.get_freed_handle(buffers[0]).data
        else { unreachable!("Failed to get buffer memory"); };

        let HandleType::Memory { ref contents, .. } =
            test_data.fake_vulkan.get_freed_handle(memory).data
        else { unreachable!("Mismatched handle"); };

        let mut expected_contents = Vec::<u8>::new();
        for component in [
            -1f32, -1f32, 0f32,
            1f32, -1f32, 0f32,
            -1f32, 1f32, 0f32,
            1f32, 1f32, 0f32,
        ] {
            expected_contents.extend(&component.to_ne_bytes());
        }
        assert_eq!(contents, &expected_contents);

        let &Command::Draw {
            vertex_count,
            instance_count,
            first_vertex,
            first_instance,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(vertex_count, 4);
        assert_eq!(instance_count, 1);
        assert_eq!(first_vertex, 0);
        assert_eq!(first_instance, 0);

        assert!(matches!(commands.next(), Some(Command::EndRenderPass)));

        let &Command::PipelineBarrier {
            ref image_memory_barriers,
            ..
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };
        assert_eq!(image_memory_barriers.len(), 1);
        assert_eq!(
            image_memory_barriers[0].image,
            test_data.window.color_image()
        );

        let &Command::CopyImageToBuffer {
            src_image,
            dst_buffer,
            ..
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(src_image, test_data.window.color_image());
        assert_eq!(dst_buffer, test_data.window.linear_buffer());

        let &Command::PipelineBarrier {
            ref image_memory_barriers,
            ..
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };
        assert_eq!(image_memory_barriers.len(), 1);
        assert_eq!(
            image_memory_barriers[0].image,
            test_data.window.color_image()
        );

        let &Command::PipelineBarrier {
            ref buffer_memory_barriers,
            ..
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };
        assert_eq!(buffer_memory_barriers.len(), 1);
        assert_eq!(
            buffer_memory_barriers[0].buffer,
            test_data.window.linear_buffer()
        );

        assert!(commands.next().is_none());

        // There should only be one flush with the RectangleVertex vbo
        assert_eq!(test_data.fake_vulkan.memory_flushes.len(), 1);

        assert_eq!(test_data.fake_vulkan.memory_invalidations.len(), 1);
        assert_eq!(
            test_data.fake_vulkan.memory_invalidations[0].memory,
            test_data.window.linear_memory(),
        );

        let HandleType::Fence { reset_count, wait_count } =
            test_data.fake_vulkan.get_freed_handle(
                test_data.context.fence()
            ).data
        else { unreachable!("Bad handle"); };

        assert_eq!(reset_count, 1);
        assert_eq!(wait_count, 1);
    }

    #[test]
    fn vbo() {
        let test_data = TestData::new(
            "[vertex data]\n\
             0/R32_SFLOAT\n\
             1\n\
             2\n\
             3\n\
             [test]\n\
             draw arrays TRIANGLE_LIST 0 3"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        assert!(matches!(
            commands.next(),
            Some(Command::BeginRenderPass { .. })
        ));

        let &Command::BindVertexBuffers {
            first_binding,
            ref buffers,
            ref offsets,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(first_binding, 0);
        assert_eq!(buffers.len(), 1);
        assert_eq!(offsets, &[0]);

        let HandleType::Buffer { memory: Some(memory), .. } =
            test_data.fake_vulkan.get_freed_handle(buffers[0]).data
        else { unreachable!("Failed to get buffer memory"); };

        let HandleType::Memory { ref contents, .. } =
            test_data.fake_vulkan.get_freed_handle(memory).data
        else { unreachable!("Mismatched handle"); };

        let mut expected_contents = Vec::<u8>::new();
        for component in [1f32, 2f32, 3f32] {
            expected_contents.extend(&component.to_ne_bytes());
        }
        assert_eq!(contents, &expected_contents);

        assert!(matches!(commands.next(), Some(Command::BindPipeline { .. })));

        let &Command::Draw {
            vertex_count,
            instance_count,
            first_vertex,
            first_instance,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(vertex_count, 3);
        assert_eq!(instance_count, 1);
        assert_eq!(first_vertex, 0);
        assert_eq!(first_instance, 0);
    }

    #[test]
    fn dispatch_compute() {
        let test_data = TestData::new(
            "[test]\n\
             compute 1 2 3"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        assert!(matches!(commands.next(), Some(Command::BindPipeline { .. })));

        let &Command::Dispatch { x, y, z } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!((x, y, z), (1, 2, 3));

        assert!(commands.next().is_none());
    }

    #[test]
    fn clear() {
        let test_data = TestData::new(
            "[test]\n\
             clear color 1 2 3 4
             clear"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        assert!(matches!(
            commands.next(),
            Some(Command::BeginRenderPass { .. })
        ));

        let &Command::ClearAttachments {
            ref attachments,
            ref rects,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(attachments.len(), 1);

        match &attachments[0] {
            &ClearAttachment::Color { attachment, value } => {
                assert_eq!(attachment, 0);
                assert_eq!(value, [1f32, 2f32, 3f32, 4f32]);
            },
            _ => unreachable!("unexepected clear attachment type"),
        }

        assert_eq!(rects.len(), 1);
        assert_eq!(
            rects[0].rect.extent.width as usize,
            WindowFormat::default().width
        );
        assert_eq!(
            rects[0].rect.extent.height as usize,
            WindowFormat::default().height
        );
    }

    #[test]
    fn clear_depth_stencil() {
        let test_data = TestData::new(
            "[require]\n\
             depthstencil D24_UNORM_S8_UINT\n\
             [test]\n\
             clear depth 2.0\n\
             clear stencil 5\n\
             clear"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        assert!(matches!(
            commands.next(),
            Some(Command::BeginRenderPass { .. })
        ));

        let &Command::ClearAttachments {
            ref attachments,
            ref rects,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(attachments.len(), 2);

        match &attachments[1] {
            &ClearAttachment::DepthStencil { aspect_mask, value } => {
                assert_eq!(
                    aspect_mask,
                    vk::VK_IMAGE_ASPECT_DEPTH_BIT
                        | vk::VK_IMAGE_ASPECT_STENCIL_BIT
                );
                assert_eq!(value.depth, 2.0);
                assert_eq!(value.stencil, 5);
            },
            _ => unreachable!("unexepected clear attachment type"),
        }

        assert_eq!(rects.len(), 1);
        assert_eq!(
            rects[0].rect.extent.width as usize,
            WindowFormat::default().width
        );
        assert_eq!(
            rects[0].rect.extent.height as usize,
            WindowFormat::default().height
        );
    }

    #[test]
    fn push_constants() {
        let test_data = TestData::new(
            "[test]\n\
             push uint8_t 1 12\n\
             push u8vec2 2 13 14"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        let &Command::PushConstants {
            layout,
            stage_flags,
            offset,
            ref values,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(layout, test_data.pipeline_set.layout());
        assert_eq!(stage_flags, 0);
        assert_eq!(offset, 1);
        assert_eq!(values.as_slice(), [12].as_slice());

        let &Command::PushConstants {
            layout,
            stage_flags,
            offset,
            ref values,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(layout, test_data.pipeline_set.layout());
        assert_eq!(stage_flags, 0);
        assert_eq!(offset, 2);
        assert_eq!(values.as_slice(), [13, 14].as_slice());
    }

    #[test]
    fn set_buffer_data() {
        let test_data = TestData::new(
            "[fragment shader]\n\
             03 02 23 07\n\
             [test]\n\
             ssbo 5 subdata uint8_t 1 1 2 3\n\
             # draw command to make it flush the memory\n\
             draw rect -1 -1 2 2"
        ).unwrap();

        let &Command::BindDescriptorSets {
            first_set,
            ref descriptor_sets,
            ..
        } = test_data.fake_vulkan.commands.iter().find(|command| {
            matches!(command, Command::BindDescriptorSets { .. })
        }).unwrap()
        else { unreachable!() };

        assert_eq!(first_set, 0);
        assert_eq!(descriptor_sets.len(), 1);

        let HandleType::DescriptorSet {
            ref bindings
        } = test_data.fake_vulkan.get_freed_handle(descriptor_sets[0]).data
        else { unreachable!("bad handle"); };

        let buffer_handle = bindings[&5].info.buffer;

        let HandleType::Buffer {
            memory: Some(memory_handle),
            ..
        } = test_data.fake_vulkan.get_freed_handle(buffer_handle).data
        else { unreachable!("failed to get buffer memory"); };

        let HandleType::Memory {
            ref contents,
            ..
        } = test_data.fake_vulkan.get_freed_handle(memory_handle).data
        else { unreachable!("bad handle"); };

        assert_eq!(contents, &[0, 1, 2, 3]);

        test_data.fake_vulkan.memory_flushes.iter().find(|flush| {
            flush.memory == memory_handle
        }).expect("expected ssbo memory to be flushed");
    }

    #[test]
    fn probe_ssbo_success() {
        TestData::new(
            "[test]\n\
             ssbo 5 subdata uint8_t 1 1 2 3\n\
             probe ssbo u8vec4 5 0 == 0 1 2 3"
        ).expect("expected ssbo probe to succeed");
    }

    #[test]
    fn probe_ssbo_fail() {
        let error = TestData::new(
            "[test]\n\
             ssbo 5 subdata uint8_t 1 1 2 3\n\
             probe ssbo u8vec4 5 0 == 0 1 2 4"
        ).unwrap_err();

        assert_eq!(
            &error.to_string(),
            "line 3: SSBO probe failed\n\
             \x20 Reference: 0 1 2 4\n\
             \x20 Observed:  0 1 2 3",
        );
    }

    #[test]
    fn probe_rect_success() {
        TestData::new(
            "[test]\n\
             probe all rgba 0 0 0 0"
        ).expect("expected probe to succeed");
    }

    #[test]
    fn probe_rect_fail() {
        let error = TestData::new(
            "[test]\n\
             probe all rgba 1 0 0 0\n\
             probe all rgba 1 2 0 0"
        ).unwrap_err();

        assert_eq!(
            &error.to_string(),
            "line 2: Probe color at (0,0)\n\
             \x20 Expected: 1 0 0 0\n\
             \x20 Observed: 0 0 0 0\n\
             line 3: Probe color at (0,0)\n\
             \x20 Expected: 1 2 0 0\n\
             \x20 Observed: 0 0 0 0"
        );
    }

    #[test]
    fn indices() {
        let test_data = TestData::new(
            "[indices]\n\
             0 1 2\n\
             [test]\n\
             draw arrays indexed TRIANGLE_LIST 0 3"
        ).unwrap();

        let mut commands = test_data.fake_vulkan.commands.iter();

        println!("{:#?}",commands);

        assert!(matches!(
            commands.next(),
            Some(Command::BeginRenderPass { .. })
        ));

        assert!(matches!(commands.next(), Some(Command::BindPipeline { .. })));

        let &Command::BindIndexBuffer {
            buffer,
            offset,
            index_type,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(offset, 0);
        assert_eq!(index_type, vk::VK_INDEX_TYPE_UINT16);

        let HandleType::Buffer { memory: Some(memory), .. } =
            test_data.fake_vulkan.get_freed_handle(buffer).data
        else { unreachable!("Failed to get buffer memory"); };

        let HandleType::Memory { ref contents, .. } =
            test_data.fake_vulkan.get_freed_handle(memory).data
        else { unreachable!("Mismatched handle"); };

        let mut expected_contents = Vec::<u8>::new();
        for component in 0u16..3u16 {
            expected_contents.extend(&component.to_ne_bytes());
        }
        assert_eq!(contents, &expected_contents);

        let &Command::DrawIndexed {
            index_count,
            instance_count,
            first_index,
            vertex_offset,
            first_instance,
        } = commands.next().unwrap()
        else { unreachable!("Bad command"); };

        assert_eq!(index_count, 3);
        assert_eq!(instance_count, 1);
        assert_eq!(first_index, 0);
        assert_eq!(vertex_offset, 0);
        assert_eq!(first_instance, 0);
    }

    extern "C" fn inspector_cb(data: &inspect::Data, user_data: *mut c_void) {
        unsafe {
            *(user_data as *mut bool) = true;
        }

        let window_format = WindowFormat::default();

        assert_eq!(data.color_buffer.width as usize, window_format.width);
        assert_eq!(data.color_buffer.height as usize, window_format.height);
        assert!(data.color_buffer.stride >= window_format.width * 4);
        assert_eq!(
            data.color_buffer.format,
            window_format.color_format as *const Format
        );
        assert!(!data.color_buffer.data.is_null());

        assert_eq!(data.n_buffers, 1);

        let buffer = unsafe { &*data.buffers };

        assert_eq!(buffer.binding, 5);
        assert_eq!(buffer.size, 1024);
        assert!(!buffer.data.is_null());
    }

    #[test]
    fn inspector() {
        let mut inspector_called = false;

        let inspector = inspect::Inspector::new(
            inspector_cb,
            ptr::addr_of_mut!(inspector_called).cast(),
        );

        TestData::new_full(
            "[test]\n\
             ssbo 5 1024",
            Some(inspector),
        ).expect("expected test to pass");

        assert!(inspector_called);
    }
}

// vkrunner
//
// Copyright (C) 2013, 2014, 2015, 2017, 2023 Neil Roberts
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

use crate::context::Context;
use crate::window_format::WindowFormat;
use crate::format::{Format, Component};
use crate::vk;
use crate::result;
use crate::vulkan_funcs;
use crate::buffer::{self, MappedMemory, DeviceMemory, Buffer};
use std::rc::Rc;
use std::fmt;
use std::ffi::c_void;
use std::ptr;

/// Struct containing the framebuffer and all of the objects on top of
/// the [Context] needed to construct it. It also keeps a reference to
/// the Context which can be retrieved publically so this works as a
/// central object to share the essential Vulkan resources used for
/// running tests.
#[derive(Debug)]
pub struct Window {
    format: WindowFormat,

    // These are listed in the reverse order that they are created so
    // that they will be destroyed in the right order too

    need_linear_memory_invalidate: bool,
    linear_memory_stride: usize,
    linear_memory_map: MappedMemory,
    linear_memory: DeviceMemory,
    linear_buffer: Buffer,

    framebuffer: Framebuffer,

    _depth_stencil_resources: Option<DepthStencilResources>,

    _color_image_view: ImageView,
    _memory: DeviceMemory,
    color_image: Image,

    // The first render pass is used for the first render and has a
    // loadOp of DONT_CARE. The second is used for subsequent renders
    // and loads the framebuffer contents.
    render_pass: [RenderPass; 2],

    context: Rc<Context>,
}

#[derive(Debug)]
struct DepthStencilResources {
    // These are listed in the reverse order that they are created so
    // that they will be destroyed in the right order too
    image_view: ImageView,
    _memory: DeviceMemory,
    _image: Image,
}

#[derive(Debug)]
pub enum WindowError {
    IncompatibleFormat(String),
    RenderPassError,
    ImageError,
    ImageViewError,
    BufferError(buffer::Error),
    FramebufferError,
}

impl fmt::Display for WindowError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            WindowError::IncompatibleFormat(s) => write!(f, "{}", s),
            WindowError::BufferError(e) => e.fmt(f),
            WindowError::RenderPassError => write!(
                f,
                "Error creating render pass",
            ),
            WindowError::ImageError => write!(
                f,
                "Error creating vkImage",
            ),
            WindowError::ImageViewError => write!(
                f,
                "Error creating vkImageView",
            ),
            WindowError::FramebufferError => write!(
                f,
                "Error creating vkFramebuffer",
            ),
        }
    }
}

impl WindowError {
    pub fn result(&self) -> result::Result {
        match self {
            WindowError::IncompatibleFormat(_) => result::Result::Skip,
            WindowError::RenderPassError => result::Result::Fail,
            WindowError::ImageError => result::Result::Fail,
            WindowError::ImageViewError => result::Result::Fail,
            WindowError::FramebufferError => result::Result::Fail,
            WindowError::BufferError(_) => result::Result::Fail,
        }
    }
}

impl From<buffer::Error> for WindowError {
    fn from(e: buffer::Error) -> WindowError {
        WindowError::BufferError(e)
    }
}

fn check_format(
    context: &Context,
    format: &Format,
    flags: vk::VkFormatFeatureFlags,
) -> bool {
    let mut format_properties: vk::VkFormatProperties = Default::default();

    unsafe {
        context.instance().vkGetPhysicalDeviceFormatProperties.unwrap()(
            context.physical_device(),
            format.vk_format,
            &mut format_properties as *mut vk::VkFormatProperties,
        );
    }

    format_properties.optimalTilingFeatures & flags == flags
}

fn check_window_format(
    context: &Context,
    window_format: &WindowFormat,
) -> Result<(), WindowError> {
    if !check_format(
        context,
        window_format.color_format,
        vk::VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
            | vk::VK_FORMAT_FEATURE_BLIT_SRC_BIT,
    ) {
        return Err(WindowError::IncompatibleFormat(format!(
            "Format {} is not supported as a color attachment and blit source",
            window_format.color_format.name,
        )));
    }

    if let Some(depth_stencil_format) = window_format.depth_stencil_format {
        if !check_format(
            context,
            depth_stencil_format,
            vk::VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        ) {
            return Err(WindowError::IncompatibleFormat(format!(
                "Format {} is not supported as a depth/stencil attachment",
                depth_stencil_format.name,
            )));
        }
    }

    Ok(())
}

#[derive(Debug)]
struct RenderPass {
    render_pass: vk::VkRenderPass,
    // Needed for the destructor
    context: Rc<Context>,
}

impl RenderPass {
    fn new(
        context: Rc<Context>,
        window_format: &WindowFormat,
        first_render: bool,
    ) -> Result<RenderPass, WindowError> {
        let has_stencil = match window_format.depth_stencil_format {
            None => false,
            Some(format) => {
                format
                    .parts()
                    .into_iter()
                    .find(|p| p.component == Component::S)
                    .is_some()
            },
        };

        let attachment_descriptions = [
            vk::VkAttachmentDescription {
                flags: 0,
                format: window_format.color_format.vk_format,
                samples: vk::VK_SAMPLE_COUNT_1_BIT,
                loadOp: if first_render {
                    vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE
                } else {
                    vk::VK_ATTACHMENT_LOAD_OP_LOAD
                },
                storeOp: vk::VK_ATTACHMENT_STORE_OP_STORE,
                stencilLoadOp: vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                stencilStoreOp: vk::VK_ATTACHMENT_STORE_OP_DONT_CARE,
                initialLayout: if first_render {
                    vk::VK_IMAGE_LAYOUT_UNDEFINED
                } else {
                    vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                },
                finalLayout: vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            vk::VkAttachmentDescription {
                flags: 0,
                format: match window_format.depth_stencil_format {
                    Some(f) => f.vk_format,
                    None => 0,
                },
                samples: vk::VK_SAMPLE_COUNT_1_BIT,
                loadOp: if first_render {
                    vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE
                } else {
                    vk::VK_ATTACHMENT_LOAD_OP_LOAD
                },
                storeOp: vk::VK_ATTACHMENT_STORE_OP_STORE,
                stencilLoadOp: if first_render || !has_stencil {
                    vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE
                } else {
                    vk::VK_ATTACHMENT_LOAD_OP_LOAD
                },
                stencilStoreOp: if has_stencil {
                    vk::VK_ATTACHMENT_STORE_OP_STORE
                } else {
                    vk::VK_ATTACHMENT_STORE_OP_DONT_CARE
                },
                initialLayout: if first_render {
                    vk::VK_IMAGE_LAYOUT_UNDEFINED
                } else {
                    vk::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                },
                finalLayout:
                vk::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        ];

        let color_attachment_reference = vk::VkAttachmentReference {
            attachment: 0,
            layout: vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        let depth_stencil_attachment_reference = vk::VkAttachmentReference {
            attachment: 1,
            layout: vk::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        let subpass_descriptions = [
            vk::VkSubpassDescription {
                flags: 0,
                pipelineBindPoint: vk::VK_PIPELINE_BIND_POINT_GRAPHICS,
                inputAttachmentCount: 0,
                pInputAttachments: ptr::null(),
                colorAttachmentCount: 1,
                pColorAttachments: ptr::addr_of!(color_attachment_reference),
                pResolveAttachments: ptr::null(),
                pDepthStencilAttachment:
                if window_format.depth_stencil_format.is_some() {
                    ptr::addr_of!(depth_stencil_attachment_reference)
                } else {
                    ptr::null()
                },
                preserveAttachmentCount: 0,
                pPreserveAttachments: ptr::null(),
            },
        ];

        let render_pass_create_info = vk::VkRenderPassCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            attachmentCount: attachment_descriptions.len() as u32
                - window_format.depth_stencil_format.is_none() as u32,
            pAttachments: ptr::addr_of!(attachment_descriptions[0]),
            subpassCount: subpass_descriptions.len() as u32,
            pSubpasses: ptr::addr_of!(subpass_descriptions[0]),
            dependencyCount: 0,
            pDependencies: ptr::null(),
        };

        let mut render_pass: vk::VkRenderPass = ptr::null_mut();

        let res = unsafe {
            context.device().vkCreateRenderPass.unwrap()(
                context.vk_device(),
                ptr::addr_of!(render_pass_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(render_pass)
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(RenderPass { render_pass, context })
        } else {
            Err(WindowError::RenderPassError)
        }
    }
}

impl Drop for RenderPass {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkDestroyRenderPass.unwrap()(
                self.context.vk_device(),
                self.render_pass,
                ptr::null(), // allocator
            );
        }
    }
}

#[derive(Debug)]
struct Image {
    image: vk::VkImage,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for Image {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkDestroyImage.unwrap()(
                self.context.vk_device(),
                self.image,
                ptr::null(), // allocator
            );
        }
    }
}

impl Image {
    fn new_from_create_info(
        context: Rc<Context>,
        image_create_info: &vk::VkImageCreateInfo,
    ) -> Result<Image, WindowError> {
        let mut image: vk::VkImage = ptr::null_mut();

        let res = unsafe {
            context.device().vkCreateImage.unwrap()(
                context.vk_device(),
                image_create_info as *const vk::VkImageCreateInfo,
                ptr::null(), // allocator
                ptr::addr_of_mut!(image),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(Image { image, context })
        } else {
            Err(WindowError::ImageError)
        }
    }

    fn new_color(
        context: Rc<Context>,
        window_format: &WindowFormat,
    ) -> Result<Image, WindowError> {
        let image_create_info = vk::VkImageCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            imageType: vk::VK_IMAGE_TYPE_2D,
            format: window_format.color_format.vk_format,
            extent: vk::VkExtent3D {
                width: window_format.width as u32,
                height: window_format.height as u32,
                depth: 1,
            },
            mipLevels: 1,
            arrayLayers: 1,
            samples: vk::VK_SAMPLE_COUNT_1_BIT,
            tiling: vk::VK_IMAGE_TILING_OPTIMAL,
            usage: vk::VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | vk::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            sharingMode: vk::VK_SHARING_MODE_EXCLUSIVE,
            queueFamilyIndexCount: 0,
            pQueueFamilyIndices: ptr::null(),
            initialLayout: vk::VK_IMAGE_LAYOUT_UNDEFINED,
        };

        Image::new_from_create_info(context, &image_create_info)
    }

    fn new_depth_stencil(
        context: Rc<Context>,
        format: &Format,
        width: usize,
        height: usize,
    ) -> Result<Image, WindowError> {
        let image_create_info = vk::VkImageCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            imageType: vk::VK_IMAGE_TYPE_2D,
            format: format.vk_format,
            extent: vk::VkExtent3D {
                width: width as u32,
                height: height as u32,
                depth: 1,
            },
            mipLevels: 1,
            arrayLayers: 1,
            samples: vk::VK_SAMPLE_COUNT_1_BIT,
            tiling: vk::VK_IMAGE_TILING_OPTIMAL,
            usage: vk::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            sharingMode: vk::VK_SHARING_MODE_EXCLUSIVE,
            queueFamilyIndexCount: 0,
            pQueueFamilyIndices: ptr::null(),
            initialLayout: vk::VK_IMAGE_LAYOUT_UNDEFINED,
        };

        Image::new_from_create_info(context, &image_create_info)
    }
}

#[derive(Debug)]
struct ImageView {
    image_view: vk::VkImageView,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for ImageView {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkDestroyImageView.unwrap()(
                self.context.vk_device(),
                self.image_view,
                ptr::null(), // allocator
            );
        }
    }
}

impl ImageView {
    fn new(
        context: Rc<Context>,
        format: &Format,
        image: vk::VkImage,
        aspect_mask: vk::VkImageAspectFlags,
    ) -> Result<ImageView, WindowError> {
        let image_view_create_info = vk::VkImageViewCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            image,
            viewType: vk::VK_IMAGE_VIEW_TYPE_2D,
            format: format.vk_format,
            components: vk::VkComponentMapping {
                r: vk::VK_COMPONENT_SWIZZLE_R,
                g: vk::VK_COMPONENT_SWIZZLE_G,
                b: vk::VK_COMPONENT_SWIZZLE_B,
                a: vk::VK_COMPONENT_SWIZZLE_A,
            },
            subresourceRange: vk::VkImageSubresourceRange {
                aspectMask: aspect_mask,
                baseMipLevel: 0,
                levelCount: 1,
                baseArrayLayer: 0,
                layerCount: 1
            },
        };

        let mut image_view: vk::VkImageView = ptr::null_mut();

        let res = unsafe {
            context.device().vkCreateImageView.unwrap()(
                context.vk_device(),
                ptr::addr_of!(image_view_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(image_view),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(ImageView { image_view, context })
        } else {
            Err(WindowError::ImageViewError)
        }
    }
}

impl DepthStencilResources {
    fn new(
        context: Rc<Context>,
        format: &Format,
        width: usize,
        height: usize,
    ) -> Result<DepthStencilResources, WindowError> {
        let image = Image::new_depth_stencil(
            Rc::clone(&context),
            format,
            width,
            height,
        )?;
        let memory = DeviceMemory::new_image(
            Rc::clone(&context),
            0, // memory_type_flags
            image.image,
        )?;
        let image_view = ImageView::new(
            context,
            format,
            image.image,
            format.depth_stencil_aspect_flags(),
        )?;

        Ok(DepthStencilResources { _image: image, _memory: memory, image_view })
    }
}

#[derive(Debug)]
struct Framebuffer {
    framebuffer: vk::VkFramebuffer,
    // Needed for the destructor
    context: Rc<Context>,
}

impl Drop for Framebuffer {
    fn drop(&mut self) {
        unsafe {
            self.context.device().vkDestroyFramebuffer.unwrap()(
                self.context.vk_device(),
                self.framebuffer,
                ptr::null(), // allocator
            );
        }
    }
}

impl Framebuffer {
    fn new(
        context: Rc<Context>,
        window_format: &WindowFormat,
        render_pass: vk::VkRenderPass,
        color_image_view: vk::VkImageView,
        depth_stencil_image_view: Option<vk::VkImageView>,
    ) -> Result<Framebuffer, WindowError> {
        let mut attachments = vec![color_image_view];

        if let Some(image_view) = depth_stencil_image_view {
            attachments.push(image_view);
        }

        let framebuffer_create_info = vk::VkFramebufferCreateInfo {
            sType: vk::VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            pNext: ptr::null(),
            flags: 0,
            renderPass: render_pass,
            attachmentCount: attachments.len() as u32,
            pAttachments: attachments.as_ptr(),
            width: window_format.width as u32,
            height: window_format.height as u32,
            layers: 1,
        };

        let mut framebuffer: vk::VkFramebuffer = ptr::null_mut();

        let res = unsafe {
            context.device().vkCreateFramebuffer.unwrap()(
                context.vk_device(),
                ptr::addr_of!(framebuffer_create_info),
                ptr::null(), // allocator
                ptr::addr_of_mut!(framebuffer),
            )
        };

        if res == vk::VK_SUCCESS {
            Ok(Framebuffer { framebuffer, context })
        } else {
            Err(WindowError::FramebufferError)
        }
    }
}

fn need_linear_memory_invalidate(
    context: &Context,
    linear_memory_type: u32,
) -> bool {
    context
        .memory_properties()
        .memoryTypes[linear_memory_type as usize]
        .propertyFlags
        & vk::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT == 0
}

impl Window {
    pub fn new(
        context: Rc<Context>,
        format: &WindowFormat,
    ) -> Result<Window, WindowError> {
        check_window_format(&context, format)?;

        let render_pass = [
            RenderPass::new(Rc::clone(&context), format, true)?,
            RenderPass::new(Rc::clone(&context), format, false)?,
        ];

        let color_image = Image::new_color(Rc::clone(&context), format)?;
        let memory = DeviceMemory::new_image(
            Rc::clone(&context),
            0, // memory_type_flags
            color_image.image,
        )?;

        let color_image_view = ImageView::new(
            Rc::clone(&context),
            format.color_format,
            color_image.image,
            vk::VK_IMAGE_ASPECT_COLOR_BIT,
        )?;

        let depth_stencil_resources = match &format.depth_stencil_format {
            Some(depth_stencil_format) => Some(DepthStencilResources::new(
                Rc::clone(&context),
                depth_stencil_format,
                format.width,
                format.height,
            )?),
            None => None,
        };

        let framebuffer = Framebuffer::new(
            Rc::clone(&context),
            format,
            render_pass[0].render_pass,
            color_image_view.image_view,
            depth_stencil_resources.as_ref().map(|r| r.image_view.image_view),
        )?;

        let linear_memory_stride = format.color_format.size() * format.width;

        let linear_buffer = Buffer::new(
            Rc::clone(&context),
            linear_memory_stride * format.height,
            vk::VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        )?;
        let linear_memory = DeviceMemory::new_buffer(
            Rc::clone(&context),
            vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            linear_buffer.buffer,
        )?;

        let linear_memory_map = MappedMemory::new(
            Rc::clone(&context),
            linear_memory.memory,
        )?;

        Ok(Window {
            format: format.clone(),

            need_linear_memory_invalidate: need_linear_memory_invalidate(
                &context,
                linear_memory.memory_type_index,
            ),
            linear_memory_stride,
            linear_memory_map,
            linear_memory,
            linear_buffer,

            framebuffer,

            _depth_stencil_resources: depth_stencil_resources,

            _color_image_view: color_image_view,
            _memory: memory,
            color_image,

            render_pass,

            context,
        })
    }

    /// Retrieve the [Context] that the Window was created with.
    pub fn context(&self) -> &Rc<Context> {
        &self.context
    }

    /// Retrieve the [WindowFormat] that the Window was created for.
    pub fn format(&self) -> &WindowFormat {
        &self.format
    }

    /// Retrieve the [Device](vulkan_funcs::Device) that the
    /// Window was created from. This is just a convenience function
    /// for getting the device from the [Context].
    pub fn device(&self) -> &vulkan_funcs::Device {
        self.context.device()
    }

    /// Retrieve the [VkDevice](vk::VkDevice) that the window was
    /// created from. This is just a convenience function for getting
    /// the device from the [Context].
    pub fn vk_device(&self) -> vk::VkDevice {
        self.context.vk_device()
    }

    /// Get the two [VkRenderPasses](vk::VkRenderPass) that were
    /// created for the window. The first render pass should be used
    /// for the first render and the second one should be used for all
    /// subsequent renders.
    pub fn render_passes(&self) -> [vk::VkRenderPass; 2] {
        [
            self.render_pass[0].render_pass,
            self.render_pass[1].render_pass,
        ]
    }

    /// Get the vulkan handle to the linear memory that can be used to
    /// copy framebuffer results into in order to inspect it.
    pub fn linear_memory(&self) -> vk::VkDeviceMemory {
        self.linear_memory.memory
    }

    /// Get the `VkBuffer` that represents the linear memory buffer
    pub fn linear_buffer(&self) -> vk::VkBuffer {
        self.linear_buffer.buffer
    }

    /// Get the pointer to the mapping that the Window holds to
    /// examine the linear memory buffer.
    pub fn linear_memory_map(&self) -> *const c_void {
        self.linear_memory_map.pointer
    }

    /// Get the stride of the linear memory buffer
    pub fn linear_memory_stride(&self) -> usize {
        self.linear_memory_stride
    }

    /// Return whether the mapping for the linear memory buffer owned
    /// by the Window needs to be invalidated with
    /// `vkInvalidateMappedMemoryRanges` before it can be read after
    /// it has been modified. This is will be true if the memory type
    /// used for the linear memory buffer doesn’t have the
    /// `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` flag set.
    pub fn need_linear_memory_invalidate(&self) -> bool {
        self.need_linear_memory_invalidate
    }

    /// Return the `VkFramebuffer` that was created for the window.
    pub fn framebuffer(&self) -> vk::VkFramebuffer {
        self.framebuffer.framebuffer
    }

    /// Return the `VkImage` that was created for the color buffer of
    /// the window.
    pub fn color_image(&self) -> vk::VkImage {
        self.color_image.image
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, HandleType};
    use crate::requirements::Requirements;

    fn base_fake_vulkan() -> Box<FakeVulkan> {
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

        fake_vulkan
    }

    fn get_render_pass_attachments(
        fake_vulkan: &mut FakeVulkan,
        render_pass: vk::VkRenderPass,
    ) -> &[vk::VkAttachmentDescription] {
        match &fake_vulkan.get_handle(render_pass).data {
            HandleType::RenderPass { attachments } => attachments.as_slice(),
            _ => unreachable!("mismatched handle type"),
        }
    }

    struct TestResources {
        // These two need to be dropped in this order
        context: Rc<Context>,
        fake_vulkan: Box<FakeVulkan>,
    }

    fn test_simple_function_error(
        function_name: &str,
        expected_error_string: &str,
    ) -> TestResources {
        let mut fake_vulkan = base_fake_vulkan();
        fake_vulkan.queue_result(
            function_name.to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let err = Window::new(
            Rc::clone(&context),
            &Default::default(), // format
        ).unwrap_err();

        assert_eq!(&err.to_string(), expected_error_string);
        assert_eq!(err.result(), result::Result::Fail);

        TestResources { context, fake_vulkan }
    }

    #[test]
    fn basic() {
        let mut fake_vulkan = base_fake_vulkan();

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let window = Window::new(
            Rc::clone(&context),
            &Default::default() // format
        ).unwrap();

        assert!(Rc::ptr_eq(window.context(), &context));
        assert_eq!(
            window.format().color_format.vk_format,
            vk::VK_FORMAT_B8G8R8A8_UNORM
        );
        assert_eq!(
            window.device() as *const vulkan_funcs::Device,
            context.device() as *const vulkan_funcs::Device,
        );
        assert_eq!(window.vk_device(), context.vk_device());
        assert_ne!(window.render_passes()[0], window.render_passes()[1]);
        assert!(!window.linear_memory().is_null());
        assert!(!window.linear_buffer().is_null());
        assert!(!window.linear_memory_map().is_null());
        assert_eq!(
            window.linear_memory_stride(),
            window.format().color_format.size()
                * window.format().width
        );
        assert!(window.need_linear_memory_invalidate());
        assert!(!window.framebuffer().is_null());
        assert!(!window.color_image().is_null());

        let rp = get_render_pass_attachments(
            fake_vulkan.as_mut(),
            window.render_passes()[0]
        );
        assert_eq!(rp.len(), 1);
        assert_eq!(rp[0].loadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[0].initialLayout, vk::VK_IMAGE_LAYOUT_UNDEFINED);
        assert_eq!(rp[0].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);

        let rp = get_render_pass_attachments(
            fake_vulkan.as_mut(),
            window.render_passes()[1]
        );
        assert_eq!(rp.len(), 1);
        assert_eq!(rp[0].loadOp, vk::VK_ATTACHMENT_LOAD_OP_LOAD);
        assert_eq!(
            rp[0].initialLayout,
            vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );
        assert_eq!(rp[0].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    }

    #[test]
    fn depth_stencil() {
        let mut fake_vulkan = base_fake_vulkan();

        let mut depth_format = WindowFormat::default();

        depth_format.depth_stencil_format = Some(Format::lookup_by_vk_format(
            vk::VK_FORMAT_D24_UNORM_S8_UINT,
        ));

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let window = Window::new(
            Rc::clone(&context),
            &depth_format,
        ).unwrap();

        let rp = get_render_pass_attachments(
            fake_vulkan.as_mut(),
            window.render_passes()[0]
        );
        assert_eq!(rp.len(), 2);
        assert_eq!(rp[0].loadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[0].initialLayout, vk::VK_IMAGE_LAYOUT_UNDEFINED);
        assert_eq!(rp[0].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[1].loadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[1].initialLayout, vk::VK_IMAGE_LAYOUT_UNDEFINED);
        assert_eq!(rp[1].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[1].stencilStoreOp, vk::VK_ATTACHMENT_STORE_OP_STORE);

        let rp = get_render_pass_attachments(
            fake_vulkan.as_mut(),
            window.render_passes()[1]
        );
        assert_eq!(rp.len(), 2);
        assert_eq!(rp[0].loadOp, vk::VK_ATTACHMENT_LOAD_OP_LOAD);
        assert_eq!(
            rp[0].initialLayout,
            vk::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );
        assert_eq!(rp[0].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        assert_eq!(rp[1].loadOp, vk::VK_ATTACHMENT_LOAD_OP_LOAD);
        assert_eq!(
            rp[1].initialLayout,
            vk::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        );
        assert_eq!(rp[1].stencilLoadOp, vk::VK_ATTACHMENT_LOAD_OP_LOAD);
        assert_eq!(rp[1].stencilStoreOp, vk::VK_ATTACHMENT_STORE_OP_STORE);
    }

    #[test]
    fn incompatible_format() {
        let mut fake_vulkan = base_fake_vulkan();

        fake_vulkan
            .physical_devices[0]
            .format_properties
            .insert(
                vk::VK_FORMAT_R8_UNORM,
                vk::VkFormatProperties {
                    linearTilingFeatures: 0,
                    optimalTilingFeatures: 0,
                    bufferFeatures: 0,
                },
            );

        let mut depth_format = WindowFormat::default();

        depth_format.depth_stencil_format = Some(Format::lookup_by_vk_format(
            vk::VK_FORMAT_R8_UNORM,
        ));

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let err = Window::new(
            Rc::clone(&context),
            &depth_format,
        ).unwrap_err();

        assert_eq!(
            &err.to_string(),
            "Format R8_UNORM is not supported as a depth/stencil attachment",
        );
        assert_eq!(err.result(), result::Result::Skip);

        fake_vulkan
            .physical_devices[0]
            .format_properties
            .get_mut(&vk::VK_FORMAT_B8G8R8A8_UNORM)
            .unwrap()
            .optimalTilingFeatures
            &= !vk::VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let err = Window::new(
            Rc::clone(&context),
            &Default::default(), // format
        ).unwrap_err();

        assert_eq!(
            &err.to_string(),
            "Format B8G8R8A8_UNORM is not supported as a color attachment \
             and blit source",
        );
        assert_eq!(err.result(), result::Result::Skip);
    }

    #[test]
    fn render_pass_error() {
        let mut res = test_simple_function_error(
            "vkCreateRenderPass",
            "Error creating render pass"
        );

        // Try making the second render pass fail too
        res.fake_vulkan.queue_result(
            "vkCreateRenderPass".to_string(),
            vk::VK_SUCCESS
        );
        res.fake_vulkan.queue_result(
            "vkCreateRenderPass".to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        let err = Window::new(
            Rc::clone(&res.context),
            &Default::default(), // format
        ).unwrap_err();

        assert_eq!(&err.to_string(), "Error creating render pass");
    }

    #[test]
    fn image_error() {
        let mut res = test_simple_function_error(
            "vkCreateImage",
            "Error creating vkImage"
        );

        // Also try the depth/stencil image
        res.fake_vulkan.queue_result(
            "vkCreateImage".to_string(),
            vk::VK_SUCCESS
        );
        res.fake_vulkan.queue_result(
            "vkCreateImage".to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        let mut depth_format = WindowFormat::default();

        depth_format.depth_stencil_format = Some(Format::lookup_by_vk_format(
            vk::VK_FORMAT_D24_UNORM_S8_UINT,
        ));

        let err = Window::new(
            Rc::clone(&res.context),
            &depth_format,
        ).unwrap_err();

        assert_eq!(&err.to_string(), "Error creating vkImage");
    }

    #[test]
    fn image_view_error() {
        let mut res = test_simple_function_error(
            "vkCreateImageView",
            "Error creating vkImageView"
        );

        // Also try the depth/stencil image
        res.fake_vulkan.queue_result(
            "vkCreateImageView".to_string(),
            vk::VK_SUCCESS
        );
        res.fake_vulkan.queue_result(
            "vkCreateImageView".to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        let mut depth_format = WindowFormat::default();

        depth_format.depth_stencil_format = Some(Format::lookup_by_vk_format(
            vk::VK_FORMAT_D24_UNORM_S8_UINT,
        ));

        let err = Window::new(
            Rc::clone(&res.context),
            &depth_format,
        ).unwrap_err();

        assert_eq!(&err.to_string(), "Error creating vkImageView");
    }

    #[test]
    fn allocate_store_error() {
        let mut res = test_simple_function_error(
            "vkAllocateMemory",
            "vkAllocateMemory failed"
        );

        // Make the second allocate fail to so that the depth/stencil
        // image will fail
        res.fake_vulkan.queue_result(
            "vkAllocateMemory".to_string(),
            vk::VK_SUCCESS,
        );
        res.fake_vulkan.queue_result(
            "vkAllocateMemory".to_string(),
            vk::VK_ERROR_UNKNOWN,
        );

        let mut depth_format = WindowFormat::default();

        depth_format.depth_stencil_format = Some(Format::lookup_by_vk_format(
            vk::VK_FORMAT_D24_UNORM_S8_UINT,
        ));

        let err = Window::new(
            Rc::clone(&res.context),
            &depth_format,
        ).unwrap_err();

        assert_eq!(&err.to_string(), "vkAllocateMemory failed");
        assert_eq!(err.result(), result::Result::Fail);

        // Remove the host visible bit so the linear memory won’t allocate
        let memory_properties =
            &mut res.fake_vulkan.physical_devices[0].memory_properties;
        memory_properties.memoryTypes[0].propertyFlags &=
            !vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        res.fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let err = Window::new(
            Rc::clone(&context),
            &Default::default(), // format
        ).unwrap_err();

        assert_eq!(
            &err.to_string(),
            "Couldn’t find suitable memory type to allocate buffer",
        );
        assert_eq!(err.result(), result::Result::Fail);
    }

    #[test]
    fn map_memory_error() {
        test_simple_function_error("vkMapMemory", "vkMapMemory failed");
    }

    #[test]
    fn buffer_error() {
        test_simple_function_error("vkCreateBuffer", "Error creating vkBuffer");
    }

    #[test]
    fn framebuffer_error() {
        test_simple_function_error(
            "vkCreateFramebuffer",
            "Error creating vkFramebuffer",
        );
    }

    #[test]
    fn rectangular_framebuffer() {
        let fake_vulkan = base_fake_vulkan();

        fake_vulkan.set_override();
        let context = Rc::new(Context::new(
            &Requirements::new(),
            None
        ).unwrap());

        let mut format = WindowFormat::default();

        format.width = 200;
        format.height = 100;

        let window = Window::new(
            Rc::clone(&context),
            &format,
        ).unwrap();

        assert_eq!(
            window.linear_memory_stride(),
            200 * format.color_format.size()
        );

        let HandleType::Memory { ref contents, .. } =
            fake_vulkan.get_handle(window.linear_memory()).data
        else { unreachable!("Mismatched handle"); };

        assert_eq!(
            contents.len(),
            window.linear_memory_stride() * 100,
        );
    }
}

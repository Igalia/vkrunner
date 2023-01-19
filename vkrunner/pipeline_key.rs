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

use crate::shader_stage;
use crate::vk;
use crate::parse_num;
use crate::hex;
use crate::util;
use std::fmt;
use std::mem;

#[repr(C)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Type {
    Graphics,
    Compute,
}

/// Notes whether the pipeline will be used to draw a rectangle or
/// whether it will use the data in the `[vertex data]` section of the
/// script.
#[repr(C)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Source {
    Rectangle,
    VertexData,
}

/// The failure code returned by [Key::set]
#[derive(Copy, Clone, Debug)]
pub enum SetPropertyError<'a> {
    /// The property was not recognised by VkRunner
    NotFound { property: &'a str },
    /// The property was recognised but the value string was not in a
    /// valid format
    InvalidValue { value: &'a str },
}

impl<'a> fmt::Display for SetPropertyError<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SetPropertyError::NotFound { property } => {
                write!(f, "Unknown property: {}", property)
            },
            SetPropertyError::InvalidValue { value } => {
                write!(f, "Invalid value: {}", value)
            },
        }
    }
}

enum PropertyType {
    Bool,
    Int,
    Float,
}

struct Property {
    prop_type: PropertyType,
    num: usize,
    name: &'static str,
}

include!{"pipeline_key_data.rs"}

struct EnumValue {
    name: &'static str,
    value: i32,
}

include!{"enum_table.rs"}

/// A set of properties that can be used to create a VkPipeline. The
/// intention is that this will work as a key so that the program can
/// detect if the same state is used multiple times so it can reuse
/// the same key.
#[derive(Clone)]
pub struct Key {
    pipeline_type: Type,
    source: Source,

    entrypoints: [Option<String>; shader_stage::N_STAGES],

    bool_properties: [bool; N_BOOL_PROPERTIES],
    int_properties: [i32; N_INT_PROPERTIES],
    float_properties: [f32; N_FLOAT_PROPERTIES],
}

impl Key {
    pub fn set_pipeline_type(&mut self, pipeline_type: Type) {
        self.pipeline_type = pipeline_type;
    }

    pub fn pipeline_type(&self) -> Type {
        self.pipeline_type
    }

    pub fn set_source(&mut self, source: Source) {
        self.source = source;
    }

    pub fn source(&self) -> Source {
        self.source
    }

    pub fn set_topology(&mut self, topology: vk::VkPrimitiveTopology) {
        self.int_properties[TOPOLOGY_PROP_NUM] = topology as i32;
    }

    pub fn set_patch_control_points(&mut self, patch_control_points: u32) {
        self.int_properties[PATCH_CONTROL_POINTS_PROP_NUM] =
            patch_control_points as i32;
    }

    pub fn set_entrypoint(
        &mut self,
        stage: shader_stage::Stage,
        entrypoint: String,
    ) {
        self.entrypoints[stage as usize] = Some(entrypoint);
    }

    pub fn entrypoint(&self, stage: shader_stage::Stage) -> &str {
        match &self.entrypoints[stage as usize] {
            Some(s) => &s[..],
            None => "main",
        }
    }

    fn find_prop(
        property: &str
    ) -> Result<&'static Property, SetPropertyError> {
        PROPERTIES.binary_search_by(|p| p.name.cmp(property))
            .and_then(|pos| Ok(&PROPERTIES[pos]))
            .or_else(|_| Err(SetPropertyError::NotFound { property }))
    }

    fn set_bool<'a>(
        &mut self,
        prop: &Property,
        value: &'a str,
    ) -> Result<(), SetPropertyError<'a>> {
        let value = if value == "true" {
            true
        } else if value == "false" {
            false
        } else {
            match parse_num::parse_i32(value) {
                Ok((v, tail)) if tail.is_empty() => v != 0,
                _ => return Err(SetPropertyError::InvalidValue { value }),
            }
        };

        self.bool_properties[prop.num] = value;

        Ok(())
    }

    // Looks up the given enum name in ENUM_VALUES using a binary
    // search. Any trailing characters that can’t be part of an enum
    // name are cut. If successful it returns the enum value and a
    // slice containing the part of the name that was cut. Otherwise
    // returns None.
    fn lookup_enum(name: &str) -> Option<(i32, &str)> {
        let length = name
            .chars()
            .take_while(|&c| c.is_alphanumeric() || c == '_')
            .count();

        let part = &name[0..length];

        ENUM_VALUES.binary_search_by(|enum_value| enum_value.name.cmp(part))
            .ok()
            .and_then(|pos| Some((ENUM_VALUES[pos].value, &name[length..])))
            .or_else(|| None)
    }

    fn set_int<'a>(
        &mut self,
        prop: &Property,
        value: &'a str,
    ) -> Result<(), SetPropertyError<'a>> {
        let mut value_part = value;
        let mut num_value = 0i32;

        loop {
            value_part = value_part.trim_start();

            if let Ok((v, tail)) = parse_num::parse_i32(value_part) {
                num_value |= v;
                value_part = tail;
            } else if let Some((v, tail)) = Key::lookup_enum(value_part) {
                num_value |= v;
                value_part = tail;
            } else {
                break Err(SetPropertyError::InvalidValue { value });
            }

            value_part = value_part.trim_start();

            if value_part.is_empty() {
                self.int_properties[prop.num] = num_value;
                break Ok(());
            }

            // If there’s anything left after the number it must be
            // the ‘|’ operator
            if !value_part.starts_with('|') {
                break Err(SetPropertyError::InvalidValue { value });
            }

            // Skip the ‘|’ character
            value_part = &value_part[1..];
        }
    }

    fn set_float<'a>(
        &mut self,
        prop: &Property,
        value: &'a str,
    ) -> Result<(), SetPropertyError<'a>> {
        match hex::parse_f32(value) {
            Ok((v, tail)) if tail.is_empty() => {
                self.float_properties[prop.num] = v;
                Ok(())
            },
            _ => Err(SetPropertyError::InvalidValue { value }),
        }
    }

    /// Set a property on the pipeline key. The property name is one
    /// of the members of any of the structs pointed to by
    /// `VkGraphicsPipelineCreateInfo`. For example, if `prop_name` is
    /// `"polygonMode"` then it will set the `polygonMode` field of
    /// the `VkPipelineRasterizationStateCreateInfo` struct pointed to
    /// by the `VkGraphicsPipelineCreateInfo` struct.
    ///
    /// The `value` will be interpreted depending on the type of the
    /// property. It will be one of the following three basic types:
    ///
    /// bool: The value can either be `true`, `false` or an integer
    /// value. If it’s an integer the bool will be true if the value
    /// is non-zero.
    ///
    /// int: The value can either be an integer value or one of the
    /// enum names used by the properties. You can also use the `|`
    /// operator to bitwise or values. This is useful for setting
    /// flags. For example `VK_COLOR_COMPONENT_R_BIT |
    /// VK_COLOR_COMPONENT_G_BIT` can be used as a value to set the
    /// `colorWriteMask` property.
    ///
    /// float: The value will be interperted as a floating-point value
    /// using [hex::parse_f32].
    pub fn set<'a>(
        &mut self,
        prop_name: &'a str,
        value: &'a str
    ) -> Result<(), SetPropertyError<'a>> {
        let prop = Key::find_prop(prop_name)?;

        let value = value.trim();

        match prop.prop_type {
            PropertyType::Bool => self.set_bool(prop, value),
            PropertyType::Int => self.set_int(prop, value),
            PropertyType::Float => self.set_float(prop, value),
        }
    }

    fn alloc_struct_size(
        buf: &mut Vec<u8>,
        struct_type: vk::VkStructureType,
        size: usize,
        align: usize
    ) -> usize {
        let offset = util::align(buf.len(), align);
        buf.resize(offset + size, 0);
        buf[offset..offset + mem::size_of::<vk::VkStructureType>()]
            .copy_from_slice(&struct_type.to_ne_bytes());
        offset
    }

    fn alloc_struct<T>(
        buf: &mut Vec<u8>,
        struct_type: vk::VkStructureType,
    ) -> usize {
        Key::alloc_struct_size(
            buf,
            struct_type,
            mem::size_of::<T>(),
            mem::align_of::<T>(),
        )
    }

    fn alloc_create_info() -> Box<[u8]> {
        let mut buf = Vec::new();

        // Allocate all of the structures before setting the pointers
        // because the addresses will change when the vec grows
        let base_offset =
            Key::alloc_struct::<vk::VkGraphicsPipelineCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            );

        assert_eq!(base_offset, 0);

        let input_assembly =
            Key::alloc_struct::<vk::VkPipelineInputAssemblyStateCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            );
        let tessellation =
            Key::alloc_struct::<vk::VkPipelineTessellationStateCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            );
        let rasterization =
            Key::alloc_struct::<vk::VkPipelineRasterizationStateCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            );
        let color_blend =
            Key::alloc_struct::<vk::VkPipelineColorBlendStateCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            );
        let color_blend_attachment =
            Key::alloc_struct::<vk::VkPipelineColorBlendAttachmentState>(
                &mut buf,
                0, // no struture type
            );
        let depth_stencil =
            Key::alloc_struct::<vk::VkPipelineDepthStencilStateCreateInfo>(
                &mut buf,
                vk::VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            );

        let mut buf = buf.into_boxed_slice();

        let create_info = unsafe {
            &mut *(buf.as_mut_ptr() as *mut vk::VkGraphicsPipelineCreateInfo)
        };

        let base_ptr = buf.as_ptr() as *const u8;

        // SAFETY: The pointer adds should all be within the single
        // allocated buf
        unsafe {
            create_info.pInputAssemblyState =
                base_ptr.add(input_assembly).cast();
            create_info.pTessellationState =
                base_ptr.add(tessellation).cast();
            create_info.pRasterizationState =
                base_ptr.add(rasterization).cast();
            create_info.pColorBlendState =
                base_ptr.add(color_blend).cast();
            create_info.pDepthStencilState =
                base_ptr.add(depth_stencil).cast();

            // We need to transmute to get rid of the const
            let color_blend: &mut vk::VkPipelineColorBlendStateCreateInfo =
                mem::transmute(create_info.pColorBlendState);
            color_blend.pAttachments =
                base_ptr.add(color_blend_attachment).cast();
        }

        buf
    }

    /// Allocates a `VkGraphicsPipelineCreateInfo` struct inside a
    /// boxed u8 slice along with the
    /// `VkPipelineInputAssemblyStateCreateInfo`,
    /// `VkPipelineTessellationStateCreateInfo`,
    /// `VkPipelineRasterizationStateCreateInfo`,
    /// `VkPipelineColorBlendStateCreateInfo`,
    /// `VkPipelineColorBlendAttachmentState` and
    /// `VkPipelineDepthStencilStateCreateInfo` structs that it points
    /// to. The properties from the pipeline key are filled in and the
    /// `sType` fields are given the appropriate values. All other
    /// fields are initialised to zero. The structs need to be in a
    /// box because they contain pointers to each other which means
    /// the structs won’t work correctly if the array is moved to a
    /// different address. The `VkGraphicsPipelineCreateInfo` is at
    /// the start of the array and the other structs can be found by
    /// following the internal pointers.
    pub fn to_create_info(&self) -> Box<[u8]> {
        let mut buf = Key::alloc_create_info();
        let create_info = unsafe {
            &mut *(buf.as_mut_ptr() as *mut vk::VkGraphicsPipelineCreateInfo)
        };
        unsafe {
            mem::transmute::<_, &mut vk::VkPipelineColorBlendStateCreateInfo>(
                create_info.pColorBlendState
            ).attachmentCount = 1;
        }
        copy_properties_to_create_info(self, create_info);
        buf
    }
}

impl PartialEq for Key {
    fn eq(&self, other: &Key) -> bool {
        if self.pipeline_type != other.pipeline_type {
            return false;
        }

        match self.pipeline_type {
            Type::Graphics => {
                if self.source != other.source
                    || self.bool_properties != other.bool_properties
                    || self.int_properties != other.int_properties
                    || self.float_properties != other.float_properties
                {
                    return false;
                }

                // Check the entrypoints for all of the stages except
                // the compute stage because that doesn’t affect
                // pipelines used for graphics.
                for i in 0..shader_stage::N_STAGES {
                    if i == shader_stage::Stage::Compute as usize {
                        continue;
                    }
                    if self.entrypoints[i].ne(&other.entrypoints[i]) {
                        return false;
                    }
                }

                true
            },
            Type::Compute => {
                // All of the properties only have an effect when the
                // pipeline is used for graphics so the only thing we
                // care about is the compute entrypoint.
                self.entrypoints[shader_stage::Stage::Compute as usize].eq(
                    &other.entrypoints[shader_stage::Stage::Compute as usize]
                )
            },
        }
    }
}

// Custom debug implementation for Key that reports the properties
// using the property names instead of confusing arrays.
impl fmt::Debug for Key {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Key {{ pipeline_type: {:?}, \
             source: {:?}, \
             entrypoints: {:?}",
            self.pipeline_type,
            self.source,
            &self.entrypoints,
        )?;

        for prop in PROPERTIES.iter() {
            write!(f, ", {}: ", prop.name)?;

            match prop.prop_type {
                PropertyType::Bool => {
                    write!(f, "{}", self.bool_properties[prop.num])?;
                },
                PropertyType::Int => {
                    write!(f, "{}", self.int_properties[prop.num])?;
                },
                PropertyType::Float => {
                    write!(f, "{}", self.float_properties[prop.num])?;
                },
            }
        }

        write!(f, " }}")
    }
}

#[no_mangle]
pub extern "C" fn vr_pipeline_key_get_type(key: *const Key) -> Type {
    unsafe { (*key).pipeline_type() }
}

#[no_mangle]
pub extern "C" fn vr_pipeline_key_get_source(key: *const Key) -> Source {
    unsafe { (*key).source() }
}

#[no_mangle]
pub extern "C" fn vr_pipeline_key_get_entrypoint(
    key: *mut Key,
    stage: shader_stage::Stage,
) -> *mut u8 {
    extern "C" {
        fn vr_strndup(s: *const u8, len: usize) -> *mut u8;
    }

    unsafe {
        let entrypoint = (*key).entrypoint(stage);
        vr_strndup(entrypoint.as_ptr(), entrypoint.len())
    }
}

#[repr(C)]
pub struct CreateInfo {
    create_info: *mut u8,
    len: usize,
}

#[no_mangle]
pub extern "C" fn vr_pipeline_key_to_create_info(
    key: *mut Key,
    ci: *mut CreateInfo,
) {
    unsafe {
        let mut buf = (*key).to_create_info();
        (*ci).len = buf.len();
        (*ci).create_info = buf.as_mut_ptr();
        mem::forget(buf);
    }
}

#[no_mangle]
pub extern "C" fn vr_pipeline_key_destroy_create_info(ci: *mut CreateInfo) {
    unsafe {
        drop(Box::from_raw(std::slice::from_raw_parts_mut(
            (*ci).create_info,
            (*ci).len,
        )));
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use shader_stage::Stage;

    fn get_create_info_topology(key: &Key) -> vk::VkPrimitiveTopology {
        let s = key.to_create_info();
        let create_info = unsafe {
            mem::transmute::<_, &vk::VkGraphicsPipelineCreateInfo>(
                s.as_ptr()
            )
        };
        unsafe {
            (*create_info.pInputAssemblyState).topology
        }
    }

    fn get_create_info_pcp(key: &Key) -> u32 {
        let s = key.to_create_info();
        let create_info = unsafe {
            mem::transmute::<_, &vk::VkGraphicsPipelineCreateInfo>(
                s.as_ptr()
            )
        };
        unsafe {
            (*create_info.pTessellationState).patchControlPoints
        }
    }

    #[test]
    fn test_key_base() {
        let mut key = Key::default();

        key.set_pipeline_type(Type::Graphics);
        assert_eq!(key.pipeline_type(), Type::Graphics);
        key.set_pipeline_type(Type::Compute);
        assert_eq!(key.pipeline_type(), Type::Compute);
        key.set_source(Source::Rectangle);
        assert_eq!(key.source(), Source::Rectangle);
        key.set_source(Source::VertexData);
        assert_eq!(key.source(), Source::VertexData);

        key.set_topology(vk::VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
        assert_eq!(
            get_create_info_topology(&key),
            vk::VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        );
        key.set_topology(vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        assert_eq!(
            get_create_info_topology(&key),
            vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        );

        key.set_patch_control_points(5);
        assert_eq!(get_create_info_pcp(&key), 5);
        key.set_patch_control_points(u32::MAX);
        assert_eq!(get_create_info_pcp(&key), u32::MAX);

        key.set_entrypoint(Stage::Vertex, "mystery_vortex".to_owned());
        assert_eq!(key.entrypoint(Stage::Vertex), "mystery_vortex");
        key.set_entrypoint(Stage::Fragment, "fraggle_rock".to_owned());
        assert_eq!(key.entrypoint(Stage::Fragment), "fraggle_rock");
        assert_eq!(key.entrypoint(Stage::Geometry), "main");
    }

    #[test]
    fn test_all_props() {
        let mut key = Key::default();

        // Check that setting all of the props works without returning
        // an error
        for prop in PROPERTIES.iter() {
            assert!(key.set(prop.name, "1").is_ok());
        }
    }

    fn check_bool_prop(value: &str) -> vk::VkBool32 {
        let mut key = Key::default();

        assert!(key.set("depthTestEnable", value).is_ok());

        let s = key.to_create_info();
        let create_info = unsafe {
            mem::transmute::<_, &vk::VkGraphicsPipelineCreateInfo>(
                s.as_ptr()
            )
        };
        unsafe {
            (*create_info.pDepthStencilState).depthTestEnable
        }
    }

    #[test]
    fn test_bool_props() {
        assert_eq!(check_bool_prop("true"), 1);
        assert_eq!(check_bool_prop("false"), 0);
        assert_eq!(check_bool_prop(" true "), 1);
        assert_eq!(check_bool_prop("   false  "), 0);
        assert_eq!(check_bool_prop("1"), 1);
        assert_eq!(check_bool_prop("42"), 1);
        assert_eq!(check_bool_prop("  0x42  "), 1);
        assert_eq!(check_bool_prop("0"), 0);
        assert_eq!(check_bool_prop("  -0  "), 0);

        let e = Key::default().set("depthTestEnable", "foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: foo");
        let e = Key::default().set("stencilTestEnable", "9 foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: 9 foo");
        let e = Key::default().set("stencilTestEnable", "true fo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: true fo");
    }

    fn check_int_prop(value: &str) -> u32 {
        let mut key = Key::default();

        assert!(key.set("patchControlPoints", value).is_ok());

        get_create_info_pcp(&key)
    }

    #[test]
    fn test_int_props() {
        assert_eq!(check_int_prop("0"), 0);
        assert_eq!(check_int_prop("1"), 1);
        assert_eq!(check_int_prop("-1"), u32::MAX);
        assert_eq!(check_int_prop("  42  "), 42);
        assert_eq!(check_int_prop(" 8 | 1 "), 9);
        assert_eq!(check_int_prop("6|16|1"), 23);
        assert_eq!(check_int_prop("010"), 8);
        assert_eq!(check_int_prop("0x80|010"), 0x88);
        assert_eq!(
            check_int_prop("VK_COLOR_COMPONENT_R_BIT"),
            vk::VK_COLOR_COMPONENT_R_BIT,
        );
        assert_eq!(
            check_int_prop(
                "VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT"
            ),
            vk::VK_COLOR_COMPONENT_R_BIT | vk::VK_COLOR_COMPONENT_G_BIT,
        );

        let e = Key::default().set("patchControlPoints", "").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: ");
        let e = Key::default().set("patchControlPoints", "9 |").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: 9 |");
        let e = Key::default().set("patchControlPoints", "|9").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: |9");
        let e = Key::default().set("patchControlPoints", "9|foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: 9|foo");
        let e = Key::default().set("patchControlPoints", "9foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: 9foo");

        let mut key = Key::default();

        // Check that all enum values can be set without error
        for e in ENUM_VALUES.iter() {
            assert!(key.set("srcColorBlendFactor", e.name).is_ok());
        }
    }

    fn check_float_prop(value: &str) -> f32 {
        let mut key = Key::default();

        assert!(key.set("depthBiasClamp", value).is_ok());

        let s = key.to_create_info();
        let create_info = unsafe {
            mem::transmute::<_, &vk::VkGraphicsPipelineCreateInfo>(
                s.as_ptr()
            )
        };
        unsafe {
            (*create_info.pRasterizationState).depthBiasClamp
        }
    }

    #[test]
    fn test_float_props() {
        assert_eq!(check_float_prop("1"), 1.0);
        assert_eq!(check_float_prop("-1"), -1.0);
        assert_eq!(check_float_prop("1.0e1"), 10.0);
        assert_eq!(check_float_prop("  0x3F800000  "), 1.0);

        let e = Key::default().set("lineWidth", "0.3 foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: 0.3 foo");
        let e = Key::default().set("lineWidth", "foo").unwrap_err();
        assert_eq!(e.to_string(), "Invalid value: foo");
    }

    #[test]
    fn test_unknown_property() {
        let e = Key::default().set("unicornCount", "2").unwrap_err();
        assert_eq!(e.to_string(), "Unknown property: unicornCount");
    }

    #[test]
    fn test_struct_types() {
        let s = Key::default().to_create_info();

        let create_info = unsafe {
            mem::transmute::<_, &vk::VkGraphicsPipelineCreateInfo>(
                s.as_ptr()
            )
        };
        unsafe {
            assert_eq!(
                (*create_info.pDepthStencilState).sType,
                vk::VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            );
            assert_eq!(
                (*create_info.pColorBlendState).sType,
                vk::VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            );
        }
    }

    #[test]
    fn test_eq() {
        let mut key_a = Key::default();
        let mut key_b = key_a.clone();

        assert!(key_a.eq(&key_b));

        key_a.set_source(Source::VertexData);
        assert!(!key_a.eq(&key_b));
        key_b.set_source(Source::VertexData);
        assert!(key_a.eq(&key_b));

        assert!(key_a.set("depthClampEnable", "true").is_ok());
        assert!(!key_a.eq(&key_b));
        assert!(key_b.set("depthClampEnable", "true").is_ok());
        assert!(key_a.eq(&key_b));

        assert!(key_a.set("colorWriteMask", "1").is_ok());
        assert!(!key_a.eq(&key_b));
        assert!(key_b.set("colorWriteMask", "1").is_ok());
        assert!(key_a.eq(&key_b));

        assert!(key_a.set("lineWidth", "3.0").is_ok());
        assert!(!key_a.eq(&key_b));
        assert!(key_b.set("lineWidth", "3.0").is_ok());
        assert!(key_a.eq(&key_b));

        key_a.set_entrypoint(Stage::TessEval, "durberville".to_owned());
        assert!(!key_a.eq(&key_b));
        key_b.set_entrypoint(Stage::TessEval, "durberville".to_owned());
        assert!(key_a.eq(&key_b));

        // Setting the compute entry point shouldn’t affect the
        // equality for graphics pipelines
        key_a.set_entrypoint(Stage::Compute, "saysno".to_owned());
        assert!(key_a.eq(&key_b));
        key_b.set_entrypoint(Stage::Compute, "saysno".to_owned());
        assert!(key_a.eq(&key_b));

        key_a.set_pipeline_type(Type::Compute);
        assert!(!key_a.eq(&key_b));
        key_b.set_pipeline_type(Type::Compute);
        assert!(key_a.eq(&key_b));

        // Setting properties shouldn’t affect the equality for compute shaders
        assert!(key_a.set("lineWidth", "5.0").is_ok());
        assert!(key_a.eq(&key_b));
        key_a.set_source(Source::Rectangle);
        assert!(key_a.eq(&key_b));
        key_a.set_entrypoint(Stage::TessCtrl, "yes".to_owned());
        assert!(key_a.eq(&key_b));

        // Setting the compute entrypoint however should affect the equality
        key_a.set_entrypoint(Stage::Compute, "rclub".to_owned());
        assert!(!key_a.eq(&key_b));
        key_b.set_entrypoint(Stage::Compute, "rclub".to_owned());
        assert!(key_a.eq(&key_b));
    }

    #[test]
    fn test_debug() {
        let mut key = Key::default();

        assert!(key.set("depthWriteEnable", "true").is_ok());
        assert!(key.set("colorWriteMask", "1").is_ok());
        assert!(key.set("lineWidth", "42.0").is_ok());

        assert_eq!(
            format!("{:?}", key),
            "Key { \
             pipeline_type: Graphics, \
             source: Rectangle, \
             entrypoints: [None, None, None, None, None, None], \
             alphaBlendOp: 0, \
             back.compareMask: -1, \
             back.compareOp: 7, \
             back.depthFailOp: 0, \
             back.failOp: 0, \
             back.passOp: 0, \
             back.reference: 0, \
             back.writeMask: -1, \
             blendEnable: false, \
             colorBlendOp: 0, \
             colorWriteMask: 1, \
             cullMode: 0, \
             depthBiasClamp: 0, \
             depthBiasConstantFactor: 0, \
             depthBiasEnable: false, \
             depthBiasSlopeFactor: 0, \
             depthBoundsTestEnable: false, \
             depthClampEnable: false, \
             depthCompareOp: 1, \
             depthTestEnable: false, \
             depthWriteEnable: true, \
             dstAlphaBlendFactor: 7, \
             dstColorBlendFactor: 7, \
             front.compareMask: -1, \
             front.compareOp: 7, \
             front.depthFailOp: 0, \
             front.failOp: 0, \
             front.passOp: 0, \
             front.reference: 0, \
             front.writeMask: -1, \
             frontFace: 0, \
             lineWidth: 42, \
             logicOp: 15, \
             logicOpEnable: false, \
             maxDepthBounds: 0, \
             minDepthBounds: 0, \
             patchControlPoints: 0, \
             polygonMode: 0, \
             primitiveRestartEnable: false, \
             rasterizerDiscardEnable: false, \
             srcAlphaBlendFactor: 6, \
             srcColorBlendFactor: 6, \
             stencilTestEnable: false, \
             topology: 4 \
             }",
        );
    }
}

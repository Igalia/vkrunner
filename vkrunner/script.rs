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

use crate::vbo;
use crate::source::Source;
use crate::stream::{Stream, StreamError};
use crate::tolerance::Tolerance;
use crate::pipeline_key;
use crate::shader_stage::{Stage, N_STAGES};
use crate::slot;
use crate::requirements::Requirements;
use crate::window_format::WindowFormat;
use crate::format::Format;
use crate::hex;
use crate::parse_num;
use crate::vk;
use std::cell::RefCell;
use std::fmt;
use std::ffi::{c_int, c_char, c_void};

#[derive(Debug, Clone)]
pub enum Shader {
    Glsl(String),
    Spirv(String),
    Binary(Vec<u32>),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub enum BufferType {
    Ubo,
    Ssbo,
}

#[derive(Debug, Clone)]
#[repr(C)]
pub struct Buffer {
    pub desc_set: u32,
    pub binding: u32,
    pub buffer_type: BufferType,
    pub size: usize,
}

#[derive(Debug)]
pub struct Script {
    filename: String,
    stages: [Box<[Shader]>; N_STAGES],
    commands: Box<[Command]>,
    pipeline_keys: Box<[pipeline_key::Key]>,
    requirements: Requirements,
    window_format: WindowFormat,
    vertex_data: Option<vbo::Vbo>,
    indices: Box<[u16]>,
    buffers: Box<[Buffer]>,
}

#[derive(Debug, Clone, PartialEq)]
#[repr(C)]
pub enum Operation {
    DrawRect {
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        pipeline_key: usize,
    },
    DrawArrays {
        topology: vk::VkPrimitiveTopology,
        indexed: bool,
        vertex_count: u32,
        instance_count: u32,
        first_vertex: u32,
        first_instance: u32,
        pipeline_key: usize,
    },
    DispatchCompute {
        x: u32,
        y: u32,
        z: u32,
        pipeline_key: usize,
    },
    ProbeRect {
        n_components: u32,
        x: u32,
        y: u32,
        w: u32,
        h: u32,
        color: [f64; 4],
        tolerance: Tolerance,
    },
    ProbeSsbo {
        desc_set: u32,
        binding: u32,
        comparison: slot::Comparison,
        offset: usize,
        slot_type: slot::Type,
        layout: slot::Layout,
        values: Box<[u8]>,
        tolerance: Tolerance,
    },
    SetPushCommand {
        offset: usize,
        data: Box<[u8]>,
    },
    SetBufferData {
        desc_set: u32,
        binding: u32,
        offset: usize,
        data: Box<[u8]>,
    },
    Clear {
        color: [f32; 4],
        depth: f32,
        stencil: u32,
    },
}

#[derive(Debug, Clone, PartialEq)]
#[repr(C)]
pub struct Command {
    pub line_num: usize,
    pub op: Operation,
}

#[derive(Debug)]
pub enum LoadError {
    Stream(StreamError),
    Vbo { line_num: usize, detail: vbo::Error },
    Invalid { line_num: usize, message: String },
    Hex { line_num: usize, detail: hex::ParseError },
    Number { line_num: usize, detail: parse_num::ParseError },
}

impl From<StreamError> for LoadError {
    fn from(error: StreamError) -> LoadError {
        LoadError::Stream(error)
    }
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            LoadError::Stream(e) => e.fmt(f),
            LoadError::Vbo { line_num, detail } => {
                write!(f, "line {}: {}", line_num, detail)
            },
            LoadError::Invalid { line_num, message } => {
                write!(f, "line {}: {}", line_num, message)
            },
            LoadError::Hex { line_num, detail } => {
                write!(f, "line {}: {}", line_num, detail)
            },
            LoadError::Number { line_num, detail } => {
                write!(f, "line {}: {}", line_num, detail)
            },
        }
    }
}

#[derive(Clone, Copy, Debug)]
enum Section {
    None = 0,
    Comment,
    Require,
    Shader,
    VertexData,
    Indices,
    Test,
}

#[derive(PartialEq, Eq, Debug)]
enum MatchResult {
    // The line was successfully parsed
    Matched,
    // The line does not match the command handled by this method so
    // the parser should try parsing it as something else.
    NotMatched,
}

// Macro to handle the match result. The calling function should be a
// Result<(), T> type and the function to call should return
// Result<MatchResult, T>. If the called function matched the line or
// returned an error then it will cause the calling function to
// return. Otherwise it can continue to try the next function.
macro_rules! handle_match_result {
    ($func:expr) => {
        match $func? {
            MatchResult::NotMatched => (),
            MatchResult::Matched => return Ok(()),
        }
    };
}

macro_rules! parse_num_func {
    ($func:ident, $type:ty) => {
        fn $func<'b>(&self, s: &'b str) -> Result<($type, &'b str), LoadError> {
            parse_num::$func(s).or_else(|detail| Err(LoadError::Number {
                line_num: self.stream.line_num(),
                detail,
            }))
        }
    };
}

// Result returned by a line parser method. It can either succeed,
// report that the line isn’t intended as this type of item, or report
// an error.
type ParseResult = Result<MatchResult, LoadError>;

struct Loader<'a> {
    source: &'a Source,
    stream: Stream<'a>,
    current_section: Section,
    had_sections: u32,
    current_source: RefCell<Option<Shader>>,
    current_stage: Stage,
    stages: [Vec::<Shader>; N_STAGES],
    tolerance: Tolerance,
    clear_color: [f32; 4],
    clear_depth: f32,
    clear_stencil: u32,
    commands: Vec<Command>,
    pipeline_keys: Vec<pipeline_key::Key>,
    current_key: pipeline_key::Key,
    push_layout: slot::Layout,
    ubo_layout: slot::Layout,
    ssbo_layout: slot::Layout,
    vertex_data: Option<vbo::Vbo>,
    vbo_parser: Option<vbo::Parser>,
    indices: Vec<u16>,
    requirements: Requirements,
    window_format: WindowFormat,
    buffers: Vec<Buffer>,
}

const DEFAULT_PUSH_LAYOUT: slot::Layout = slot::Layout {
        std: slot::LayoutStd::Std430,
        major: slot::MajorAxis::Column
};

const DEFAULT_UBO_LAYOUT: slot::Layout = slot::Layout {
        std: slot::LayoutStd::Std140,
        major: slot::MajorAxis::Column
};

const DEFAULT_SSBO_LAYOUT: slot::Layout = slot::Layout {
        std: slot::LayoutStd::Std430,
        major: slot::MajorAxis::Column
};

static STAGE_NAMES: [(Stage, &'static str); N_STAGES] = [
    (Stage::Vertex, "vertex"),
    (Stage::TessCtrl, "tessellation control"),
    (Stage::TessEval, "tessellation evaluation"),
    (Stage::Geometry, "geometry"),
    (Stage::Fragment, "fragment"),
    (Stage::Compute, "compute"),
];

// Mapping of topology name to Vulkan topology enum, sorted
// alphabetically so we can do a binary search.
static TOPOLOGY_NAMES: [(&'static str, vk::VkPrimitiveTopology); 22] = [
    // GL names used in Piglit
    ("GL_LINES", vk::VK_PRIMITIVE_TOPOLOGY_LINE_LIST),
    ("GL_LINES_ADJACENCY", vk::VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY),
    ("GL_LINE_STRIP", vk::VK_PRIMITIVE_TOPOLOGY_LINE_STRIP),
    ("GL_LINE_STRIP_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY),
    ("GL_PATCHES", vk::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST),
    ("GL_POINTS", vk::VK_PRIMITIVE_TOPOLOGY_POINT_LIST),
    ("GL_TRIANGLES", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
    ("GL_TRIANGLES_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY),
    ("GL_TRIANGLE_FAN", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN),
    ("GL_TRIANGLE_STRIP", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP),
    ("GL_TRIANGLE_STRIP_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY),
    // Vulkan names
    ("LINE_LIST", vk::VK_PRIMITIVE_TOPOLOGY_LINE_LIST),
    ("LINE_LIST_WITH_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY),
    ("LINE_STRIP", vk::VK_PRIMITIVE_TOPOLOGY_LINE_STRIP),
    ("LINE_STRIP_WITH_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY),
    ("PATCH_LIST", vk::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST),
    ("POINT_LIST", vk::VK_PRIMITIVE_TOPOLOGY_POINT_LIST),
    ("TRIANGLE_FAN", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN),
    ("TRIANGLE_LIST", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
    ("TRIANGLE_LIST_WITH_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY),
    ("TRIANGLE_STRIP", vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP),
    ("TRIANGLE_STRIP_WITH_ADJACENCY",
     vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY),
];

static PASSTHROUGH_VERTEX_SHADER: [u32; 69] = [
    0x07230203, 0x00010000, 0x00070000, 0x0000000c, 0x00000000, 0x00020011,
    0x00000001, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000000,
    0x00000001, 0x6e69616d, 0x00000000, 0x00000002, 0x00000003, 0x00040047,
    0x00000002, 0x0000001e, 0x00000000, 0x00040047, 0x00000003, 0x0000000b,
    0x00000000, 0x00020013, 0x00000004, 0x00030021, 0x00000005, 0x00000004,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000001, 0x00000007, 0x00040020,
    0x00000009, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000002,
    0x00000001, 0x0004003b, 0x00000009, 0x00000003, 0x00000003, 0x00050036,
    0x00000004, 0x00000001, 0x00000000, 0x00000005, 0x000200f8, 0x0000000a,
    0x0004003d, 0x00000007, 0x0000000b, 0x00000002, 0x0003003e, 0x00000003,
    0x0000000b, 0x000100fd, 0x00010038
];

impl Shader {
    fn is_spirv(&self) -> bool {
        match self {
            Shader::Binary(_) | Shader::Spirv(_) => true,
            Shader::Glsl(_) => false,
        }
    }
}

macro_rules! error_at_line {
    ($loader:expr, $($format_arg:expr),+) => {
        LoadError::Invalid {
            line_num: $loader.stream.line_num(),
            message: format!($($format_arg),+),
        }
    };
}

// Utility like String::strip_prefix except that it additionally
// strips any leading whitespace and checks that the prefix is followed
// either by the end of the string or some whitespace. The returned
// tail will include the trailing whitespace if there is any.
fn strip_word_prefix<'a, 'b>(s: &'a str, prefix: &'b str) -> Option<&'a str> {
    match s.trim_start().strip_prefix(prefix) {
        Some(tail) => match tail.chars().next() {
            // Allow the end of the string
            None => Some(tail),
            // Allow another character if it’s whitespace
            Some(ch) => if ch.is_whitespace() {
                Some(tail)
            } else {
                None
            },
        },
        None => None,
    }
}

// Calls strip_word_prefix for each word in the prefix string so that
// for example if you call `strip_words_prefix(s, "hot potato")` then
// it is the same as `strip_word_prefix` except that there can be any
// number of spaces between the “hot” and “potato”.
fn strip_words_prefix<'a, 'b>(
    mut s: &'a str,
    prefix: &'b str
) -> Option<&'a str> {
    for word in prefix.split_whitespace() {
        match strip_word_prefix(s, word) {
            None => return None,
            Some(tail) => s = tail,
        }
    }

    Some(s)
}

// Gets the next word from the string and returns it along with the
// string tail, or None if the string doesn’t have any non-whitespace
// characters
fn next_word(s: &str) -> Option<(&str, &str)> {
    let s = s.trim_start();

    match s.split_whitespace().next() {
        Some(word) => Some((word, &s[word.len()..])),
        None => None,
    }
}

// Remove comments (ie, # upto the end of the line) and trim leading
// and trailing whitespace. If the line ends up empty, return None,
// otherwise return the trimmed string.
fn trim_line_or_skip(line: &str) -> Option<&str> {
    // Remove comments
    let line = line
        .split_once('#')
        .and_then(|(line, _tail)| Some(line))
        .unwrap_or(line)
        .trim();

    if line.is_empty() {
        None
    } else {
        Some(line)
    }
}

impl<'a> Loader<'a> {
    fn new(source: &Source) -> Result<Loader, LoadError> {
        Ok(Loader {
            source,
            stream: Stream::new(source)?,
            current_section: Section::None,
            had_sections: 0,
            current_source: RefCell::new(None),
            current_stage: Stage::Vertex,
            stages: Default::default(),
            tolerance: Default::default(),
            clear_color: [0.0f32; 4],
            clear_depth: 1.0,
            clear_stencil: 0,
            commands: Vec::new(),
            pipeline_keys: Vec::new(),
            current_key: Default::default(),
            push_layout: DEFAULT_PUSH_LAYOUT,
            ubo_layout: DEFAULT_UBO_LAYOUT,
            ssbo_layout: DEFAULT_SSBO_LAYOUT,
            vertex_data: None,
            vbo_parser: None,
            indices: Vec::new(),
            requirements: Requirements::new(),
            window_format: Default::default(),
            buffers: Vec::new(),
        })
    }

    fn end_shader(&mut self) {
        self.stages[self.current_stage as usize]
            .push(self.current_source.take().unwrap());
    }

    fn end_vertex_data(&mut self) -> Result<(), LoadError> {
        match self.vbo_parser.take().unwrap().into_vbo() {
            Ok(vbo) => {
                self.vertex_data.replace(vbo);
                Ok(())
            },
            Err(e) => Err(LoadError::Vbo {
                line_num: self.stream.line_num(),
                detail: e,
            }),
        }
    }

    fn end_section(&mut self) -> Result<(), LoadError> {
        match self.current_section {
            Section::None => (),
            Section::Comment => (),
            Section::Require => (),
            Section::Shader => self.end_shader(),
            Section::VertexData => self.end_vertex_data()?,
            Section::Indices => (),
            Section::Test => (),
        }

        self.current_section = Section::None;

        Ok(())
    }

    fn set_current_section(&mut self, section: Section) {
        self.had_sections |= 1 << (section as u32);
        self.current_section = section;
    }

    fn is_stage_name<'b>(
        line: &'b str,
        suffix: &str
    ) -> Option<(Stage, &'b str)> {
        assert_eq!(STAGE_NAMES.len(), N_STAGES);

        for &(stage, name) in STAGE_NAMES.iter() {
            if let Some(tail) = strip_words_prefix(line, name) {
                if let Some(tail) = strip_word_prefix(tail, suffix) {
                    return Some((stage, tail));
                }
            }
        }

        None
    }

    fn check_add_shader(
        &mut self,
        stage: Stage,
        shader: &Shader
    ) -> Result<(), LoadError> {
        if let Some(other) = self.stages[stage as usize].last() {
            if other.is_spirv() || shader.is_spirv() {
                return Err(error_at_line!(
                    self,
                    "SPIR-V source can not be linked with other shaders in the \
                     same stage"
                ));
            }
        }

        Ok(())
    }

    fn process_stage_header(&mut self, section_name: &str) -> ParseResult {
        let (stage, tail) =
            match Loader::is_stage_name(section_name, "shader") {
                Some(v) => v,
                None => return Ok(MatchResult::NotMatched),
            };

        let (source, tail) =
            if let Some(tail) = strip_word_prefix(tail, "spirv") {
                (Shader::Spirv(String::new()), tail)
            } else if let Some(tail) = strip_word_prefix(tail, "binary") {
                (Shader::Binary(Vec::new()), tail)
            } else {
                (Shader::Glsl(String::new()), tail)
            };

        if !tail.trim_end().is_empty() {
            return Ok(MatchResult::NotMatched);
        };

        self.check_add_shader(stage, &source)?;

        self.current_source.replace(Some(source));
        self.set_current_section(Section::Shader);
        self.current_stage = stage;

        Ok(MatchResult::Matched)
    }

    fn add_passthrough_vertex_shader(&mut self) -> Result<(), LoadError> {
        let source = Shader::Binary(PASSTHROUGH_VERTEX_SHADER.to_vec());

        self.check_add_shader(Stage::Vertex, &source)?;

        // The passthrough vertex shader section shouldn’t have any data
        self.set_current_section(Section::None);

        self.stages[Stage::Vertex as usize].push(source);

        Ok(())
    }

    fn process_section_name(
        &mut self,
        section_name: &str,
    ) -> Result<(), LoadError> {
        if self.process_stage_header(section_name)? == MatchResult::Matched {
            return Ok(());
        }

        let section_name = section_name.trim();

        if section_name == "vertex shader passthrough" {
            self.add_passthrough_vertex_shader()?;
            return Ok(());
        }

        if section_name == "comment" {
            self.set_current_section(Section::Comment);
            return Ok(());
        }

        if section_name == "require" {
            // The require section must come first because the “test”
            // section uses the window size while parsing the
            // commands.
            if self.had_sections & !(1 << (Section::Comment as u32)) != 0 {
                return Err(error_at_line!(
                    self,
                    "[require] must be the first section"
                ));
            }
            self.set_current_section(Section::Require);
            return Ok(());
        }

        if section_name == "test" {
            self.set_current_section(Section::Test);
            return Ok(());
        }

        if section_name == "indices" {
            self.set_current_section(Section::Indices);
            return Ok(());
        }

        if section_name == "vertex data" {
            if !self.vertex_data.is_none() {
                return Err(error_at_line!(
                    self,
                    "Duplicate vertex data section"
                ));
            }
            self.set_current_section(Section::VertexData);
            self.vbo_parser = Some(vbo::Parser::new());
            return Ok(());
        }

        Err(error_at_line!(self, "Unknown section “{}”", section_name))
    }

    fn process_section_header(&mut self, line: &str) -> ParseResult {
        if !line.starts_with('[') {
            return Ok(MatchResult::NotMatched);
        }

        self.end_section()?;

        let section_name = match line.find(']') {
            None => return Err(error_at_line!(self, "Missing ‘]’")),
            Some(pos) => {
                match line.trim_end().split_at(pos) {
                    // Match when the tail is the closing bracket
                    // followed by nothing.
                    (before, "]") => {
                        // Remove the opening ‘[’
                        &before[1..]
                    },
                    // Otherwise there must have been garbage after
                    // the section header
                    _ => {
                        return Err(error_at_line!(
                            self,
                            "Trailing data after ‘]’"
                        ));
                    },
                }
            }
        };

        self.process_section_name(section_name)?;

        Ok(MatchResult::Matched)
    }

    fn process_none_line(&self, line: &str) -> Result<(), LoadError> {
        match trim_line_or_skip(line) {
            Some(_) => Err(error_at_line!(self, "expected empty line")),
            None => Ok(()),
        }
    }

    fn parse_format(&self, line: &str) -> Result<&'static Format, LoadError> {
        let line = line.trim_start();

        if line.is_empty() {
            return Err(error_at_line!(self, "Missing format name"));
        }

        match Format::lookup_by_name(line) {
            None => Err(error_at_line!(self, "Unknown format: {}", line)),
            Some(f) => Ok(f),
        }
    }

    fn parse_half_float<'b>(
        &self,
        s: &'b str
    ) -> Result<(u16, &'b str), LoadError> {
        hex::parse_half_float(s).or_else(|detail| Err(LoadError::Hex {
            line_num: self.stream.line_num(),
            detail,
        }))
    }

    fn parse_f32<'b>(&self, s: &'b str) -> Result<(f32, &'b str), LoadError> {
        hex::parse_f32(s).or_else(|detail| Err(LoadError::Hex {
            line_num: self.stream.line_num(),
            detail,
        }))
    }

    fn parse_f64<'b>(&self, s: &'b str) -> Result<(f64, &'b str), LoadError> {
        hex::parse_f64(s).or_else(|detail| Err(LoadError::Hex {
            line_num: self.stream.line_num(),
            detail,
        }))
    }

    parse_num_func!(parse_u8, u8);
    parse_num_func!(parse_u16, u16);
    parse_num_func!(parse_u32, u32);
    parse_num_func!(parse_u64, u64);
    parse_num_func!(parse_i8, i8);
    parse_num_func!(parse_i16, i16);
    parse_num_func!(parse_i32, i32);
    parse_num_func!(parse_i64, i64);

    fn parse_fbsize(&self, line: &str) -> Result<(usize, usize), LoadError> {
        let (width, tail) = self.parse_u32(line)?;
        let (height, tail) = self.parse_u32(tail)?;

        if tail.is_empty() {
            Ok((width as usize, height as usize))
        } else {
            Err(error_at_line!(self, "Invalid fbsize"))
        }
    }

    fn parse_version(&self, line: &str) -> Result<(u32, u32, u32), LoadError> {
        if let Some((version_str, tail)) = next_word(line) {
            let mut parts = [0u32; 3];

            'parse_parts: {
                for (i, part) in version_str.split('.').enumerate() {
                    if i >= parts.len() {
                        break 'parse_parts;
                    }

                    parts[i] = match part.parse::<u32>() {
                        Ok(v) => v,
                        Err(_) => break 'parse_parts,
                    };
                }

                if tail.trim_start().len() != 0 {
                    break 'parse_parts;
                }

                return Ok((parts[0], parts[1], parts[2]));
            }
        }

        Err(error_at_line!(self, "Invalid Vulkan version"))
    }

    fn parse_desc_set_and_binding<'b>(
        &self,
        line: &'b str
    ) -> Result<(u32, u32, &'b str), LoadError> {
        let (part_a, tail) = self.parse_u32(line)?;

        let (desc_set, binding, tail) =
            if let Some(tail) = tail.strip_prefix(':') {
                let (part_b, tail) = self.parse_u32(tail)?;
                (part_a, part_b, tail)
            } else {
                (0, part_a, tail)
            };

        if let Some(c) = tail.chars().next() {
            if !c.is_whitespace() {
                return Err(error_at_line!(self, "Invalid buffer binding"));
            }
        }

        Ok((desc_set, binding, tail))
    }

    fn parse_glsl_type<'b>(
        &self,
        line: &'b str
    ) -> Result<(slot::Type, &'b str), LoadError> {
        let (type_name, line) = match next_word(line) {
            None => return Err(error_at_line!(self, "Expected GLSL type name")),
            Some(v) => v,
        };

        match slot::Type::from_glsl_type(type_name) {
            None => Err(error_at_line!(
                self,
                "Invalid GLSL type name: {}",
                type_name
            )),
            Some(t) => Ok((t, line)),
        }
    }

    fn parse_slot_base_type<'b>(
        &self,
        line: &'b str,
        base_type: slot::BaseType,
        buf: &mut [u8],
    ) -> Result<&'b str, LoadError> {
        match base_type {
            slot::BaseType::Int => {
                let (value, tail) = self.parse_i32(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::UInt => {
                let (value, tail) = self.parse_u32(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Int8 => {
                let (value, tail) = self.parse_i8(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::UInt8 => {
                let (value, tail) = self.parse_u8(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Int16 => {
                let (value, tail) = self.parse_i16(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::UInt16 => {
                let (value, tail) = self.parse_u16(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Int64 => {
                let (value, tail) = self.parse_i64(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::UInt64 => {
                let (value, tail) = self.parse_u64(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Float16 => {
                let (value, tail) = self.parse_half_float(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Float => {
                let (value, tail) = self.parse_f32(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
            slot::BaseType::Double => {
                let (value, tail) = self.parse_f64(line)?;
                buf.copy_from_slice(&value.to_ne_bytes());
                Ok(tail)
            },
        }
    }

    fn parse_slot_values(
        &self,
        mut line: &str,
        slot_type: slot::Type,
        layout: slot::Layout,
        stride: usize,
    ) -> Result<Box<[u8]>, LoadError> {
        let mut buffer = Vec::<u8>::new();
        let mut n_values = 0;
        let type_size = slot_type.size(layout);
        let base_type = slot_type.base_type();

        loop {
            buffer.resize(n_values * stride + type_size, 0);

            let base_offset = buffer.len() - type_size;

            for offset in slot_type.offsets(layout) {
                line = self.parse_slot_base_type(
                    line,
                    base_type,
                    &mut buffer[base_offset + offset
                                ..base_offset + offset + base_type.size()],
                )?;
            }

            if line.trim_start().is_empty() {
                break Ok(buffer.into_boxed_slice())
            }

            n_values += 1;
        }
    }

    fn parse_buffer_subdata(
        &self,
        line: &str,
        slot_type: slot::Type,
        layout: slot::Layout,
    ) -> Result<Box<[u8]>, LoadError> {
        self.parse_slot_values(
            line,
            slot_type,
            layout,
            slot_type.array_stride(layout),
        )
    }

    fn is_valid_extension_or_feature_name(line: &str) -> bool {
        for ch in line.chars() {
            if !ch.is_ascii_alphanumeric() && ch != '_' {
                return false;
            }
        }

        true
    }

    fn process_require_line(&mut self, line: &str) -> Result<(), LoadError> {
        let line = match trim_line_or_skip(line) {
            Some(l) => l,
            None => return Ok(()),
        };

        if let Some(tail) = strip_word_prefix(line, "framebuffer") {
            self.window_format.color_format = self.parse_format(tail)?;
            return Ok(());
        }

        if let Some(tail) = strip_word_prefix(line, "depthstencil") {
            self.window_format.depth_stencil_format =
                Some(self.parse_format(tail)?);
            return Ok(());
        }

        if let Some(tail) = strip_word_prefix(line, "fbsize") {
            let (width, height) = self.parse_fbsize(tail)?;
            self.window_format.width = width;
            self.window_format.height = height;
            return Ok(());
        }

        if let Some(tail) = strip_word_prefix(line, "vulkan") {
            let (major, minor, patch) = self.parse_version(tail)?;
            self.requirements.add_version(major, minor, patch);
            return Ok(());
        }

        if Loader::is_valid_extension_or_feature_name(line) {
            self.requirements.add(line);
            return Ok(());
        }

        Err(error_at_line!(self, "Invalid require line"))
    }

    fn decode_binary(
        &self,
        data: &mut Vec<u32>,
        line: &str
    ) -> Result<(), LoadError> {
        let line = match trim_line_or_skip(line) {
            Some(l) => l,
            None => return Ok(()),
        };

        for part in line.split_whitespace() {
            let value = match u32::from_str_radix(part, 16) {
                Ok(v) => v,
                Err(_) => return Err(error_at_line!(
                    self,
                    "Invalid hex value: {}",
                    part
                )),
            };

            data.push(value);
        }

        Ok(())
    }

    fn process_shader_line(&self, line: &str) -> Result<(), LoadError> {
        match self.current_source.borrow_mut().as_mut().unwrap() {
            Shader::Glsl(s) => s.push_str(line),
            Shader::Spirv(s) => s.push_str(line),
            Shader::Binary(data) => self.decode_binary(data, line)?,
        }

        Ok(())
    }

    fn process_vertex_data_line(
        &mut self,
        line: &str
    ) -> Result<(), LoadError> {
        match self.vbo_parser.as_mut().unwrap().parse_line(line) {
            Ok(()) => Ok(()),
            Err(e) => Err(LoadError::Vbo {
                line_num: self.stream.line_num(),
                detail: e,
            }),
        }
    }

    fn process_indices_line(
        &mut self,
        line: &str,
    ) -> Result<(), LoadError> {
        let mut line = match trim_line_or_skip(line) {
            Some(l) => l,
            None => return Ok(()),
        };

        loop {
            line = line.trim_start();

            if line.is_empty() {
                break Ok(());
            }

            let (value, tail) = self.parse_u16(line)?;

            self.indices.push(value);
            line = tail;
        }
    }

    fn get_buffer(
        &mut self,
        desc_set: u32,
        binding: u32,
        buffer_type: BufferType,
    ) -> Result<&mut Buffer, LoadError> {
        let position = if let Some(pos) = self.buffers.iter().position(
            |b| b.desc_set == desc_set && b.binding == binding
        ) {
            if self.buffers[pos].buffer_type != buffer_type {
                return Err(error_at_line!(
                    self,
                    "Buffer binding point {}:{} used with different type",
                    desc_set,
                    binding
                ));
            }

            pos
        } else {
            self.buffers.push(Buffer {
                desc_set,
                binding,
                buffer_type,
                size: 0
            });

            self.buffers.len() - 1
        };

        Ok(&mut self.buffers[position])
    }

    fn layout_for_buffer_type(&self, buffer_type: BufferType) -> slot::Layout {
        match buffer_type {
            BufferType::Ubo => self.ubo_layout,
            BufferType::Ssbo => self.ssbo_layout,
        }
    }

    // Adds the given key to the list of pipeline keys if it’s not
    // already there and returns its index, otherwise just drops the
    // new key returns the index of the existing key.
    fn add_pipeline_key(&mut self, key: pipeline_key::Key) -> usize {
        for (pos, existing_key) in self.pipeline_keys.iter().enumerate() {
            if existing_key.eq(&key) {
                return pos;
            }
        }

        self.pipeline_keys.push(key);
        self.pipeline_keys.len() - 1
    }

    fn process_set_buffer_subdata(
        &mut self,
        desc_set: u32,
        binding: u32,
        buffer_type: BufferType,
        line: &str,
    ) -> Result<(), LoadError> {
        let (value_type, line) = self.parse_glsl_type(line)?;
        let (offset, line) = self.parse_u32(line)?;
        let data = self.parse_buffer_subdata(
            line,
            value_type,
            self.layout_for_buffer_type(buffer_type),
        )?;

        let buffer = self.get_buffer(desc_set, binding, buffer_type)?;

        let min_buffer_size = offset as usize + data.len();

        if buffer.size < min_buffer_size {
            buffer.size = min_buffer_size;
        }

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::SetBufferData {
                desc_set,
                binding,
                offset: offset as usize,
                data,
            },
        });

        Ok(())
    }

    fn process_set_buffer_size(
        &mut self,
        desc_set: u32,
        binding: u32,
        buffer_type: BufferType,
        size: usize,
    ) -> Result<(), LoadError> {
        let buffer = self.get_buffer(desc_set, binding, buffer_type)?;

        if size > buffer.size {
            buffer.size = size;
        }

        Ok(())
    }

    fn parse_probe_parts<'b>(
        &self,
        line: &'b str,
        n_parts: usize,
        relative: bool,
    ) -> Result<([u32; 4], &'b str), LoadError> {
        let mut line = match line.trim_start().strip_prefix('(') {
            None => return Err(error_at_line!(self, "Expected ‘(’")),
            Some(tail) => tail,
        };

        let mut result = [0; 4];

        for i in 0..n_parts {
            if relative {
                let (value, tail) = self.parse_f32(line)?;
                let multiplier = if i & 1 == 0 {
                    self.window_format.width
                } else {
                    self.window_format.height
                };
                result[i] = (value * multiplier as f32) as u32;
                line = tail;
            } else {
                let (value, tail) = self.parse_u32(line)?;
                result[i] = value;
                line = tail;
            }

            if i < n_parts - 1 {
                line = match line.trim_start().strip_prefix(',') {
                    None => return Err(error_at_line!(self, "Expected ‘,’")),
                    Some(tail) => tail,
                };
            }
        }

        match line.trim_start().strip_prefix(')') {
            None => Err(error_at_line!(self, "Expected ‘)’")),
            Some(tail) => Ok((result, tail)),
        }
    }

    fn parse_color<'b>(
        &self,
        line: &'b str,
        n_parts: usize,
    ) -> Result<([f64; 4], &'b str), LoadError> {
        let mut line = match line.trim_start().strip_prefix('(') {
            None => return Err(error_at_line!(self, "Expected ‘(’")),
            Some(tail) => tail,
        };

        let mut result = [0.0; 4];

        for i in 0..n_parts {
            let (value, tail) = self.parse_f64(line)?;
            result[i] = value;
            line = tail;

            if i < n_parts - 1 {
                line = match line.trim_start().strip_prefix(',') {
                    None => return Err(error_at_line!(self, "Expected ‘,’")),
                    Some(tail) => tail,
                };
            }
        }

        match line.trim_start().strip_prefix(')') {
            None => Err(error_at_line!(self, "Expected ‘)’")),
            Some(tail) => Ok((result, tail)),
        }
    }

    fn process_probe(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let (relative, line) = match strip_word_prefix(line, "relative") {
            None => (false, line),
            Some(tail) => (true, tail),
        };

        let line = match strip_word_prefix(line, "probe") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        enum RegionType {
            Point,
            Rect,
            All,
        }

        let (region_type, line) =
            if let Some(tail) = strip_word_prefix(line, "rect") {
                (RegionType::Rect, tail)
            } else if let Some(tail) = strip_word_prefix(line, "all") {
                (RegionType::All, tail)
            } else {
                (RegionType::Point, line)
            };

        let (n_components, line) =
            if let Some(tail) = strip_word_prefix(line, "rgb") {
                (3, tail)
            } else if let Some(tail) = strip_word_prefix(line, "rgba") {
                (4, tail)
            } else {
                return Err(error_at_line!(
                    self,
                    "Expected rgb or rgba in probe command"
                ));
            };

        let (x, y, w, h, color, line) = match region_type {
            RegionType::All => {
                if relative {
                    return Err(error_at_line!(
                        self,
                        "‘all’ can’t be used with a relative probe"
                    ));
                }

                // For some reason the “probe all” command has a
                // different syntax for the color

                let mut color = [0.0; 4];
                let mut line = line;

                for i in 0..n_components {
                    let (part, tail) = self.parse_f64(line)?;
                    color[i] = part;
                    line = tail;
                }

                (
                    0,
                    0,
                    self.window_format.width as u32,
                    self.window_format.height as u32,
                    color,
                    line
                )
            },
            RegionType::Point => {
                let (parts, tail) = self.parse_probe_parts(line, 2, relative)?;
                let (color, tail) = self.parse_color(tail, n_components)?;
                (parts[0], parts[1], 1, 1, color, tail)
            },
            RegionType::Rect => {
                let (parts, tail) = self.parse_probe_parts(line, 4, relative)?;
                let (color, tail) = self.parse_color(tail, n_components)?;
                (parts[0], parts[1], parts[2], parts[3], color, tail)
            },
        };

        if !line.is_empty() {
            return Err(error_at_line!(
                self,
                "Extra data after probe command"
            ));
        }

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::ProbeRect {
                n_components: n_components as u32,
                x,
                y,
                w,
                h,
                color,
                tolerance: self.tolerance.clone(),
            },
        });

        Ok(MatchResult::Matched)
    }

    fn process_push(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_word_prefix(line, "push") {
            Some(l) => l,
            None => {
                // Allow “uniform” as an alias for “push” for
                // compatibility with shader-runner scripts.
                match strip_word_prefix(line, "uniform") {
                    Some(l) => l,
                    None => return Ok(MatchResult::NotMatched),
                }
            },
        };

        let (value_type, line) = self.parse_glsl_type(line)?;
        let (offset, line) = self.parse_u32(line)?;
        let data = self.parse_buffer_subdata(
            line,
            value_type,
            self.push_layout,
        )?;

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::SetPushCommand {
                offset: offset as usize,
                data,
            },
        });

        Ok(MatchResult::Matched)
    }

    fn process_uniform_ubo(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_words_prefix(line, "uniform ubo") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let (desc_set, binding, line) = self.parse_desc_set_and_binding(line)?;

        self.process_set_buffer_subdata(
            desc_set,
            binding,
            BufferType::Ubo,
            line
        )?;

        Ok(MatchResult::Matched)
    }

    fn process_draw_rect(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let mut line = match strip_words_prefix(line, "draw rect") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let mut ortho = false;
        let mut patch = false;

        loop {
            if let Some(tail) = strip_word_prefix(line, "ortho") {
                ortho = true;
                line = tail;
            } else if let Some(tail) = strip_word_prefix(line, "patch") {
                patch = true;
                line = tail;
            } else {
                break;
            }
        };

        let (mut x, line) = self.parse_f32(line)?;
        let (mut y, line) = self.parse_f32(line)?;
        let (mut w, line) = self.parse_f32(line)?;
        let (mut h, line) = self.parse_f32(line)?;

        if !line.trim_end().is_empty() {
            return Err(error_at_line!(self, "Extra data at end of line"));
        }

        if ortho {
            let width = self.window_format.width as f32;
            let height = self.window_format.width as f32;
            x = (x * 2.0 / width) - 1.0;
            y = (y * 2.0 / height) - 1.0;
            w *= 2.0 / width;
            h *= 2.0 / height;
        }

        let mut key = self.current_key.clone();
        key.set_pipeline_type(pipeline_key::Type::Graphics);
        key.set_source(pipeline_key::Source::Rectangle);
        key.set_topology(if patch {
            vk::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
        } else {
            vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
        });
        key.set_patch_control_points(4);
        let pipeline_key = self.add_pipeline_key(key);

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::DrawRect {
                x,
                y,
                w,
                h,
                pipeline_key,
            }
        });

        Ok(MatchResult::Matched)
    }

    fn lookup_topology(
        &self,
        name: &str,
    ) -> Result<vk::VkPrimitiveTopology, LoadError> {
        match TOPOLOGY_NAMES.binary_search_by(
            |&(probe, _)| probe.cmp(name)
        ) {
            Ok(pos) => Ok(TOPOLOGY_NAMES[pos].1),
            Err(_pos) => Err(error_at_line!(
                self,
                "Unknown topology: {}",
                name
            )),
        }
    }

    fn process_draw_arrays(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let mut line = match strip_words_prefix(line, "draw arrays") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let mut instanced = false;
        let mut indexed = false;

        loop {
            if let Some(tail) = strip_word_prefix(line, "instanced") {
                instanced = true;
                line = tail;
            } else if let Some(tail) = strip_word_prefix(line, "indexed") {
                indexed = true;
                line = tail;
            } else {
                break;
            }
        }

        let (topology_name, line) = match next_word(line) {
            Some(v) => v,
            None => return Err(error_at_line!(self, "Expected topology name")),
        };

        let topology = self.lookup_topology(topology_name)?;

        let (first_vertex, line) = self.parse_u32(line)?;
        let (vertex_count, line) = self.parse_u32(line)?;
        let (instance_count, line) = if instanced {
            self.parse_u32(line)?
        } else {
            (1, line)
        };

        if !line.is_empty() {
            return Err(error_at_line!(self, "Extra data at end of line"));
        }

        let mut key = self.current_key.clone();
        key.set_pipeline_type(pipeline_key::Type::Graphics);
        key.set_source(pipeline_key::Source::VertexData);
        key.set_topology(topology);
        let pipeline_key = self.add_pipeline_key(key);

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::DrawArrays {
                topology,
                indexed,
                first_vertex,
                vertex_count,
                first_instance: 0,
                instance_count,
                pipeline_key,
            }
        });

        Ok(MatchResult::Matched)
    }

    fn process_compute(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_word_prefix(line, "compute") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let (x, line) = self.parse_u32(line)?;
        let (y, line) = self.parse_u32(line)?;
        let (z, line) = self.parse_u32(line)?;

        if !line.is_empty() {
            return Err(error_at_line!(self, "Extra data at end of line"));
        }

        let mut key = self.current_key.clone();
        key.set_pipeline_type(pipeline_key::Type::Compute);
        let pipeline_key = self.add_pipeline_key(key);

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::DispatchCompute {
                x,
                y,
                z,
                pipeline_key,
            },
        });

        Ok(MatchResult::Matched)
    }

    fn process_buffer_command(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let (line, buffer_type) =
            if let Some(tail) = strip_word_prefix(line, "ssbo") {
                (tail, BufferType::Ssbo)
            } else if let Some(tail) = strip_word_prefix(line, "ubo") {
                (tail, BufferType::Ubo)
            } else {
                return Ok(MatchResult::NotMatched);
            };

        let (desc_set, binding, line) = self.parse_desc_set_and_binding(line)?;

        let line = line.trim_start();

        if let Some(line) = strip_word_prefix(line, "subdata") {
            self.process_set_buffer_subdata(
                desc_set,
                binding,
                buffer_type,
                line
            )?;
            Ok(MatchResult::Matched)
        } else {
            let (size, tail) = self.parse_u32(line)?;

            if tail.is_empty() {
                self.process_set_buffer_size(
                    desc_set,
                    binding,
                    buffer_type,
                    size as usize
                )?;
                Ok(MatchResult::Matched)
            } else {
                Err(error_at_line!(self, "Invalid buffer command"))
            }
        }
    }

    fn process_probe_ssbo(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_words_prefix(line, "probe ssbo") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let (slot_type, line) = self.parse_glsl_type(line)?;
        let (desc_set, binding, line) = self.parse_desc_set_and_binding(line)?;
        let (offset, line) = self.parse_u32(line)?;

        let (operator, line) = match next_word(line) {
            None => return Err(error_at_line!(
                self,
                "Expected comparison operator"
            )),
            Some(v) => v,
        };

        let comparison = match slot::Comparison::from_operator(operator) {
            None => return Err(error_at_line!(
                self,
                "Unknown comparison operator: {}",
                operator
            )),
            Some(c) => c,
        };

        let type_size = slot_type.size(self.ssbo_layout);

        let values = self.parse_slot_values(
            line,
            slot_type,
            self.ssbo_layout,
            type_size, // stride (tightly packed)
        )?;

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::ProbeSsbo {
                desc_set,
                binding,
                comparison,
                offset: offset as usize,
                slot_type,
                layout: self.ssbo_layout,
                values,
                tolerance: self.tolerance.clone(),
            }
        });

        Ok(MatchResult::Matched)
    }

    fn process_clear(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        if line != "clear" {
            return Ok(MatchResult::NotMatched);
        }

        self.commands.push(Command {
            line_num: self.stream.line_num(),
            op: Operation::Clear {
                color: self.clear_color,
                depth: self.clear_depth,
                stencil: self.clear_stencil,
            },
        });

        Ok(MatchResult::Matched)
    }

    fn process_clear_values(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_word_prefix(line, "clear") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        if let Some(line) = strip_word_prefix(line, "color") {
            let (r, tail) = self.parse_f32(line)?;
            let (g, tail) = self.parse_f32(tail)?;
            let (b, tail) = self.parse_f32(tail)?;
            let (a, tail) = self.parse_f32(tail)?;

            return if tail.is_empty() {
                self.clear_color = [r, g, b, a];
                Ok(MatchResult::Matched)
            } else {
                Err(error_at_line!(self, "Invalid clear color command"))
            }
        } else if let Some(line) = strip_word_prefix(line, "depth") {
            let (depth, tail) = self.parse_f32(line)?;

            if tail.is_empty() {
                self.clear_depth = depth;
                Ok(MatchResult::Matched)
            } else {
                Err(error_at_line!(self, "Invalid clear depth command"))
            }
        } else if let Some(line) = strip_word_prefix(line, "stencil") {
            let (stencil, tail) = self.parse_u32(line)?;

            if tail.is_empty() {
                self.clear_stencil = stencil;
                Ok(MatchResult::Matched)
            } else {
                Err(error_at_line!(self, "Invalid clear stencil command"))
            }
        } else {
            Ok(MatchResult::NotMatched)
        }
    }

    fn process_pipeline_property(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let (key, value) = match next_word(line) {
            Some(k) => k,
            None => return Ok(MatchResult::NotMatched),
        };

        let value = value.trim_start();

        match self.current_key.set(key, value) {
            Ok(()) => Ok(MatchResult::Matched),
            Err(pipeline_key::SetPropertyError::NotFound { .. }) => {
                Ok(MatchResult::NotMatched)
            },
            Err(pipeline_key::SetPropertyError::InvalidValue { .. }) => {
                Err(error_at_line!(self, "Invalid value: {}", value))
            },
        }
    }

    fn process_layout(
        &mut self,
        line: &str
    ) -> Result<MatchResult, LoadError> {
        let (line, layout, default) =
            if let Some(tail) = strip_words_prefix(line, "push layout") {
                (tail, &mut self.push_layout, DEFAULT_PUSH_LAYOUT)
            } else if let Some(tail) = strip_words_prefix(line, "ubo layout") {
                (tail, &mut self.ubo_layout, DEFAULT_UBO_LAYOUT)
            } else if let Some(tail) = strip_words_prefix(line, "ssbo layout") {
                (tail, &mut self.ssbo_layout, DEFAULT_SSBO_LAYOUT)
            } else {
                return Ok(MatchResult::NotMatched);
            };

        // Reinitialise the layout with the default values in case not
        // all of the properties are specified
        *layout = default;

        for token in line.split_whitespace() {
            if token == "std140" {
                layout.std = slot::LayoutStd::Std140;
            } else if token == "std430" {
                layout.std = slot::LayoutStd::Std430;
            } else if token == "row_major" {
                layout.major = slot::MajorAxis::Row;
            } else if token == "column_major" {
                layout.major = slot::MajorAxis::Column;
            } else {
                return Err(error_at_line!(
                    self,
                    "Unknown layout parameter “{}”",
                    token
                ));
            }
        }

        Ok(MatchResult::Matched)
    }

    fn process_tolerance(
        &mut self,
        line: &str
    ) -> Result<MatchResult, LoadError> {
        let mut line = match strip_word_prefix(line, "tolerance") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let mut is_percent = false;
        let mut n_args = 0usize;
        let mut value = [0.0f64; 4];

        loop {
            line = line.trim_start();

            if line.is_empty() {
                break;
            }

            if n_args >= 4 {
                return Err(error_at_line!(
                    self,
                    "tolerance command has extra arguments"
                ));
            }

            let (component, tail) = self.parse_f64(line)?;
            value[n_args] = component;
            line = tail;

            let this_is_percent = if let Some(tail) = line.strip_prefix('%') {
                line = tail;
                true
            } else {
                false
            };

            if n_args > 0 && this_is_percent != is_percent {
                return Err(error_at_line!(
                    self,
                    "Either all tolerance values must be a percentage or none"
                ));
            }

            is_percent = this_is_percent;

            n_args += 1;
        }

        if n_args == 1 {
            let first_value = value[0];
            value[1..].fill(first_value);
        } else if n_args != 4 {
            return Err(error_at_line!(
                self,
                "There must be either 1 or 4 tolerance values"
            ));
        }

        self.tolerance = Tolerance::new(value, is_percent);

        Ok(MatchResult::Matched)
    }

    fn process_entrypoint(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let (stage, line) =
            match Loader::is_stage_name(line, "entrypoint") {
                Some(v) => v,
                None => return Ok(MatchResult::NotMatched),
            };

        let entrypoint = line.trim_start();

        if entrypoint.is_empty() {
            return Err(error_at_line!(self, "Missing entrypoint name"));
        }

        self.current_key.set_entrypoint(stage, entrypoint.to_owned());

        Ok(MatchResult::Matched)
    }

    fn process_patch_parameter_vertices(
        &mut self,
        line: &str,
    ) -> Result<MatchResult, LoadError> {
        let line = match strip_words_prefix(line, "patch parameter vertices") {
            Some(l) => l,
            None => return Ok(MatchResult::NotMatched),
        };

        let (pcp, tail) = self.parse_u32(line)?;

        if tail.is_empty() {
            self.current_key.set_patch_control_points(pcp);
            Ok(MatchResult::Matched)
        } else {
            Err(error_at_line!(
                self,
                "Invalid patch parameter vertices command"
            ))
        }
    }

    fn process_test_line(&mut self, line: &str) -> Result<(), LoadError> {
        let line = match trim_line_or_skip(line) {
            Some(l) => l,
            None => return Ok(()),
        };

        // Try each of the possible test line processing functions in
        // turn until one of them matches or returns an error.
        handle_match_result!(self.process_probe_ssbo(line));
        handle_match_result!(self.process_probe(line));
        handle_match_result!(self.process_uniform_ubo(line));
        handle_match_result!(self.process_layout(line));
        handle_match_result!(self.process_push(line));
        handle_match_result!(self.process_draw_rect(line));
        handle_match_result!(self.process_draw_arrays(line));
        handle_match_result!(self.process_entrypoint(line));
        handle_match_result!(self.process_compute(line));
        handle_match_result!(self.process_buffer_command(line));
        handle_match_result!(self.process_clear(line));
        handle_match_result!(self.process_pipeline_property(line));
        handle_match_result!(self.process_clear_values(line));
        handle_match_result!(self.process_tolerance(line));
        handle_match_result!(self.process_patch_parameter_vertices(line));

        Err(error_at_line!(self, "Invalid test command"))
    }

    fn process_line(&mut self, line: &str) -> Result<(), LoadError> {
        if self.process_section_header(line)? == MatchResult::Matched {
            return Ok(());
        }

        match self.current_section {
            Section::None => self.process_none_line(line),
            Section::Comment => Ok(()),
            Section::Require => self.process_require_line(line),
            Section::Shader => self.process_shader_line(line),
            Section::VertexData => self.process_vertex_data_line(line),
            Section::Indices => self.process_indices_line(line),
            Section::Test => self.process_test_line(line),
        }
    }

    fn parse(mut self) -> Result<Script, LoadError> {
        let mut line = String::new();

        loop {
            line.clear();

            if self.stream.read_line(&mut line)? == 0 {
                break;
            }

            self.process_line(&line)?;
        }

        self.end_section()?;

        self.buffers.sort_by(|a, b| {
            a.desc_set
                .cmp(&b.desc_set)
                .then_with(|| a.binding.cmp(&b.binding))
        });

        Ok(Script {
            filename: self.source.filename().to_owned(),
            stages: self.stages.map(|stage| stage.into_boxed_slice()),
            commands: self.commands.into_boxed_slice(),
            pipeline_keys: self.pipeline_keys.into_boxed_slice(),
            requirements: self.requirements,
            window_format: self.window_format,
            vertex_data: self.vertex_data,
            indices: self.indices.into_boxed_slice(),
            buffers: self.buffers.into_boxed_slice(),
        })
    }
}

impl Script {
    pub fn load(source: &Source) -> Result<Script, LoadError> {
        Loader::new(source)?.parse()
    }

    pub fn filename(&self) -> &str {
        self.filename.as_str()
    }

    pub fn shaders(&self, stage: Stage) -> &[Shader] {
        &*self.stages[stage as usize]
    }

    pub fn commands(&self) -> &[Command] {
        &*self.commands
    }

    pub fn pipeline_keys(&self) -> &[pipeline_key::Key] {
        &*self.pipeline_keys
    }

    pub fn requirements(&self) -> &Requirements {
        &self.requirements
    }

    pub fn window_format(&self) -> &WindowFormat {
        &self.window_format
    }

    pub fn vertex_data(&self) -> Option<&vbo::Vbo> {
        self.vertex_data.as_ref()
    }

    pub fn indices(&self) -> &[u16] {
        &*self.indices
    }

    pub fn buffers(&self) -> &[Buffer] {
        &*self.buffers
    }

    pub fn replace_shaders_stage_binary(
        &mut self,
        stage: Stage,
        source: &[u32]
    ) {
        let new_shaders = vec![Shader::Binary(source.to_vec())];
        self.stages[stage as usize] = new_shaders.into_boxed_slice();
    }
}

#[repr(C)]
pub enum SourceType {
    Glsl,
    Spirv,
    Binary,
}

#[repr(C)]
pub struct ShaderCode {
    source_type: SourceType,
    stage: Stage,
    source_length: usize,
    source: *const c_char,
}

#[no_mangle]
pub extern "C" fn vr_script_get_shaders(
    script: &Script,
    _source: &Source,
    mut shader_code: *mut ShaderCode,
) -> c_int {
    extern "C" {
        fn malloc(size: usize) -> *mut c_void;
    }

    static ALL_STAGES: [Stage; N_STAGES] = [
        Stage::Vertex,
        Stage::TessCtrl,
        Stage::TessEval,
        Stage::Geometry,
        Stage::Fragment,
        Stage::Compute,
    ];

    let mut n_shaders = 0;

    for &stage in ALL_STAGES.iter() {

        for shader in script.shaders(stage) {
            let (source_type, ptr, length) = match shader {
                Shader::Glsl(source) => {
                    (
                        SourceType::Glsl,
                        source.as_ptr().cast(),
                        source.len(),
                    )
                },
                Shader::Spirv(source) => {
                    (
                        SourceType::Spirv,
                        source.as_ptr().cast(),
                        source.len(),
                    )
                },
                Shader::Binary(data) => {
                    (
                        SourceType::Binary,
                        data.as_ptr().cast(),
                        data.len() * std::mem::size_of::<u32>(),
                    )
                },
            };

            unsafe {
                (*shader_code).source_type = source_type;
                (*shader_code).stage = stage;
                (*shader_code).source = malloc(length).cast();
                std::ptr::copy_nonoverlapping(
                    ptr as *const c_char,
                    (*shader_code).source as *mut c_char,
                    length,
                );
                (*shader_code).source_length = length;

                // SAFTEY: This caller is supposed to have provided an
                // array big enough to store all of the shaders so
                // this should be within the same allocation.
                shader_code = shader_code.add(1);
            }
        }

        n_shaders += script.shaders(stage).len();
    }

    n_shaders as c_int
}

#[no_mangle]
pub extern "C" fn vr_script_get_num_shaders(script: &Script) -> c_int
{
    script.stages.iter().map(|stage| stage.len()).sum::<usize>() as c_int
}

#[no_mangle]
pub extern "C" fn vr_script_replace_shaders_stage_binary(
    script: &mut Script,
    stage: Stage,
    source_length: usize,
    source: *const u32,
) {
    script.replace_shaders_stage_binary(
        stage,
        // SAFETY: The caller is supposed to provide a valid array
        unsafe { std::slice::from_raw_parts(source, source_length) },
    );
}

#[no_mangle]
pub extern "C" fn vr_script_load(
    config: *const c_void,
    source: &Source
) -> Option<Box<Script>> {
    extern "C" {
        fn vr_error_message_string(config: *const c_void, str: *const c_char);
    }

    match Script::load(source) {
        Err(e) => {
            let mut message = e.to_string();
            message.push('\0');
            unsafe {
                vr_error_message_string(config, message.as_ptr().cast());
            }
            None
        },
        Ok(script) => Some(Box::new(script)),
    }
}

#[no_mangle]
pub extern "C" fn vr_script_free(script: *mut Script) {
    unsafe { Box::from_raw(script) };
}

#[no_mangle]
pub extern "C" fn vr_script_get_filename(script: &Script) -> *mut c_char {
    extern "C" {
        fn vr_strndup(s: *const c_char, len: usize) -> *mut c_char;
    }

    unsafe {
        vr_strndup(
            script.filename().as_ptr().cast(),
            script.filename().len()
        ).cast()
    }
}

#[no_mangle]
pub extern "C" fn vr_script_get_n_pipeline_keys(script: &Script) -> usize {
    script.pipeline_keys().len()
}

#[no_mangle]
pub extern "C" fn vr_script_get_pipeline_key(
    script: &Script,
    key_num: usize,
) -> &pipeline_key::Key {
    &script.pipeline_keys()[key_num]
}

#[no_mangle]
pub extern "C" fn vr_script_get_commands(
    script: &Script,
    commands_out: &mut *const Command,
    n_commands_out: &mut usize
) {
    *commands_out = script.commands().as_ptr();
    *n_commands_out = script.commands().len();
}

#[no_mangle]
pub extern "C" fn vr_script_get_buffers(
    script: &Script,
    buffers_out: &mut *const Buffer,
    n_buffers_out: &mut usize
) {
    *buffers_out = script.buffers().as_ptr();
    *n_buffers_out = script.buffers().len();
}

#[no_mangle]
pub extern "C" fn vr_script_get_indices(
    script: &Script,
    indices_out: &mut *const u16,
    n_indices_out: &mut usize
) {
    *indices_out = script.indices().as_ptr();
    *n_indices_out = script.indices().len();
}

#[no_mangle]
pub extern "C" fn vr_script_get_requirements(
    script: &Script,
) -> &Requirements {
    script.requirements()
}

#[no_mangle]
pub extern "C" fn vr_script_get_window_format(
    script: &Script,
) -> &WindowFormat {
    script.window_format()
}

#[no_mangle]
pub extern "C" fn vr_script_get_vertex_data(
    script: &Script,
) -> Option<&vbo::Vbo> {
    script.vertex_data()
}

#[cfg(test)]
mod test {
    use super::*;
    use std::fs;
    use std::io;
    use std::ffi::CStr;
    use crate::requirements::make_version;

    #[test]
    fn test_strip_word_prefix() {
        assert_eq!(strip_word_prefix("potato", "potato"), Some(""));
        assert_eq!(strip_word_prefix("   potato", "potato"), Some(""));
        assert_eq!(strip_word_prefix("   potato  ", "potato"), Some("  "));
        assert_eq!(strip_word_prefix(" \t potato\t", "potato"), Some("\t"));
        assert_eq!(strip_word_prefix("potato-party", "potato"), None);
        assert_eq!(strip_word_prefix("potato party", "potato"), Some(" party"));
        assert_eq!(strip_word_prefix("potaty", "potato"), None);
        assert_eq!(strip_word_prefix("hotpotato", "potato"), None);
        assert_eq!(strip_word_prefix("potatopie", "potato"), None);

        assert_eq!(strip_words_prefix("potato", "potato"), Some(""));
        assert_eq!(strip_words_prefix("potato pie", "potato pie"), Some(""));
        assert_eq!(strip_words_prefix("potato    pie", "potato pie"), Some(""));
        assert_eq!(strip_words_prefix("potato  pie ", "potato pie"), Some(" "));
        assert_eq!(strip_words_prefix("potato  pies", "potato pie"), None);
        assert_eq!(strip_words_prefix(" hot potato ", "hot potato"), Some(" "));
    }

    #[test]
    fn test_trim_line_or_skip() {
        assert_eq!(trim_line_or_skip("potato"), Some("potato"));
        assert_eq!(trim_line_or_skip("   potato \r\n"), Some("potato"));
        assert_eq!(trim_line_or_skip("   potato # pie \n\n"), Some("potato"));
        assert_eq!(trim_line_or_skip("   potato# pie # pie"), Some("potato"));
        assert_eq!(trim_line_or_skip(""), None);
        assert_eq!(trim_line_or_skip("    \t     \n"), None);
        assert_eq!(trim_line_or_skip("# comment"), None);
        assert_eq!(trim_line_or_skip("    # comment    "), None);
    }

    fn check_error(source: &str, error: &str) {
        let source = Source::from_string(source.to_string());
        let load_error = Script::load(&source).unwrap_err().to_string();
        assert_eq!(error, load_error);
    }

    fn script_from_string(source: String) -> Script {
        let source = Source::from_string(source);
        Script::load(&source).unwrap()
    }

    fn check_test_command(source: &str, op: Operation) -> Script {
        let script = script_from_string(format!("[test]\n{}", source));
        assert_eq!(script.commands().len(), 1);
        assert_eq!(script.commands()[0].line_num, 2);
        assert_eq!(script.commands()[0].op, op);
        script
    }

    fn check_test_command_error(source: &str, error: &str) {
        let source_string = format!("[test]\n{}", source);
        let error = format!("line 2: {}", error);
        check_error(&source_string, &error);
    }

    #[test]
    fn test_probe() {
        check_test_command(
            " relative   probe  rect   rgb \
             ( 1.0,2.0,  3.0, 4.0 ) \
             (5, 6, 7)",
            Operation::ProbeRect {
                n_components: 3,
                x: WindowFormat::default().width as u32,
                y: WindowFormat::default().height as u32 * 2,
                w: WindowFormat::default().width as u32 * 3,
                h: WindowFormat::default().height as u32 * 4,
                color: [5.0, 6.0, 7.0, 0.0],
                tolerance: Tolerance::default(),
            },
        );
        check_test_command(
            "probe rect rgb (1, 2, 3, 4) (5, 6, 7)",
            Operation::ProbeRect {
                n_components: 3,
                x: 1,
                y: 2,
                w: 3,
                h: 4,
                color: [5.0, 6.0, 7.0, 0.0],
                tolerance: Tolerance::default(),
            },
        );
        check_test_command(
            "relative probe rgb (1.0, 2.0) (3, 4, 5)",
            Operation::ProbeRect {
                n_components: 3,
                x: WindowFormat::default().width as u32,
                y: WindowFormat::default().height as u32 * 2,
                w: 1,
                h: 1,
                color: [3.0, 4.0, 5.0, 0.0],
                tolerance: Tolerance::default(),
            },
        );
        check_test_command(
            "probe rgba (1, 2) (3, 4, 5, 6)",
            Operation::ProbeRect {
                n_components: 4,
                x: 1,
                y: 2,
                w: 1,
                h: 1,
                color: [3.0, 4.0, 5.0, 6.0],
                tolerance: Tolerance::default(),
            },
        );
        check_test_command(
            "probe all rgba \t 8 9 0x3FF0000000000000 -12.0",
            Operation::ProbeRect {
                n_components: 4,
                x: 0,
                y: 0,
                w: WindowFormat::default().width as u32,
                h: WindowFormat::default().height as u32,
                color: [8.0, 9.0, 1.0, -12.0],
                tolerance: Tolerance::default(),
            },
        );

        check_test_command_error(
            "probe rgbw (1, 2) (3, 4, 5, 6)",
            "Expected rgb or rgba in probe command",
        );
        check_test_command_error(
            "relative probe all rgb 3 4 5",
            "‘all’ can’t be used with a relative probe",
        );
        check_test_command_error(
            "probe all rgb 3 4 5 BOO!",
            "Extra data after probe command",
        );
        check_test_command_error(
            "probe all rgb",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "probe rgb (1, 2) NOW 3 4 5",
            "Expected ‘(’",
        );
        check_test_command_error(
            "probe rgb (1, 2) (3 4 5)",
            "Expected ‘,’",
        );
        check_test_command_error(
            "probe rgb (1, 2) (3, 4, 5, 6)",
            "Expected ‘)’",
        );
        check_test_command_error(
            "probe rgb point=(3, 4) (10, 20, 30)",
            "Expected ‘(’",
        );
        check_test_command_error(
            "probe rgb (3 4) (10, 20, 30)",
            "Expected ‘,’",
        );
        check_test_command_error(
            "probe rgb (1, 2, 3) (3, 4, 5, 6)",
            "Expected ‘)’",
        );
        check_test_command_error(
            "probe rect rgb (1, 2, 3, 4, 5) (3, 4, 5)",
            "Expected ‘)’",
        );
        check_test_command_error(
            "relative probe rgb (, 2) (3, 4, 5)",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "probe rgb (, 2) (3, 4, 5)",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "probe rgb (1, 2) (, 4, 5)",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "probe rect rgb (1, 2, 1, 2) (, 4, 5)",
            "cannot parse float from empty string",
        );
    }

    #[test]
    fn test_push() {
        check_test_command(
            "push u8vec4 8    4 5 6 7",
            Operation::SetPushCommand {
                offset: 8,
                data: Box::new([4, 5, 6, 7]),
            },
        );
        check_test_command(
            "uniform u8vec3 8 4 5 6 7 8 9",
            Operation::SetPushCommand {
                offset: 8,
                data: Box::new([4, 5, 6, 0, 7, 8, 9]),
            },
        );

        check_test_command_error(
            "push u17vec4 8 4 5 6 7",
            "Invalid GLSL type name: u17vec4",
        );
        check_test_command_error(
            "push uint8_t",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "push uint8_t 8",
            "cannot parse integer from empty string",
        );
    }

    #[test]
    fn test_uniform_ubo() {
        let script = check_test_command(
            "uniform   ubo   1:2 u8vec2 8  1 2 3 4 5 6",
            Operation::SetBufferData {
                desc_set: 1,
                binding: 2,
                offset: 8,
                data: Box::new([
                    1, 2,
                    // Padding for std140 layout
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    3, 4,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    5, 6
                ]),
            }
        );

        assert_eq!(script.buffers().len(), 1);
        assert_eq!(script.buffers()[0].size, 8 + 16 * 2 + 2);
        assert_eq!(script.buffers()[0].buffer_type, BufferType::Ubo);

        check_test_command(
            "uniform   ubo   9 u8vec2 1000042  1 2",
            Operation::SetBufferData {
                desc_set: 0,
                binding: 9,
                offset: 1000042,
                data: Box::new([1, 2]),
            }
        );

        check_test_command_error(
            "uniform ubo 9:2:3 uint8_t 1 1",
            "Invalid buffer binding",
        );
        check_test_command_error(
            "uniform ubo 9: uint8_t 1 1",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "uniform ubo :9 uint8_t 1 1",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "uniform ubo 1 uint8_t",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "uniform ubo 1 uint8_t 8",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "uniform ubo 1 uint63_t",
            "Invalid GLSL type name: uint63_t",
        );
    }

    #[test]
    fn test_buffer_command() {
        let script = check_test_command(
            "ubo 1 subdata u8vec2 8  1 2 3 4 5 6",
            Operation::SetBufferData {
                desc_set: 0,
                binding: 1,
                offset: 8,
                data: Box::new([
                    1, 2,
                    // Padding for std140 layout
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    3, 4,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    5, 6
                ]),
            }
        );

        assert_eq!(script.buffers().len(), 1);
        assert_eq!(script.buffers()[0].size, 8 + 16 * 2 + 2);
        assert_eq!(script.buffers()[0].buffer_type, BufferType::Ubo);

        let script = check_test_command(
            "ssbo 1 subdata u8vec2 8  1 2 3 4 5 6",
            Operation::SetBufferData {
                desc_set: 0,
                binding: 1,
                offset: 8,
                data: Box::new([
                    // No padding for std430 layout
                    1, 2, 3, 4, 5, 6
                ]),
            }
        );

        assert_eq!(script.buffers().len(), 1);
        assert_eq!(script.buffers()[0].size, 8 + 6);
        assert_eq!(script.buffers()[0].buffer_type, BufferType::Ssbo);

        let script = script_from_string(
            "[test]\n\
             ssbo 1 subdata uint8_t 1 2 3 4 5 6\n\
             # Set the size to a size that is smaller than the one set with\n\
             # the subdata\n\
             ssbo 1 4".to_string()
        );
        assert_eq!(script.buffers().len(), 1);
        assert_eq!(script.buffers()[0].size, 6);

        let script = script_from_string(
            "[test]\n\
             ssbo 1 subdata uint8_t 1 2 3 4 5 6\n\
             # Set the size to a size that is greater than the one set with\n\
             # the subdata\n\
             ssbo 1 4000".to_string()
        );
        assert_eq!(script.buffers().len(), 1);
        assert_eq!(script.buffers()[0].size, 4000);

        check_test_command_error(
            "ubo 1: subdata u8vec2 8  1 2 3 4 5 6",
            "cannot parse integer from empty string",
        );

        check_error(
            "[test]\n\
             ubo 1 subdata u8vec2 8  1 2 3 4 5 6\n\
             ssbo 1 subdata u8vec2 8  1 2 3 4 5 6",
            "line 3: Buffer binding point 0:1 used with different type"
        );

        check_error(
            "[test]\n\
             ubo 1 8\n\
             ssbo 1 8",
            "line 3: Buffer binding point 0:1 used with different type"
        );

        check_test_command_error(
            "ubo 1",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "ubo 1 1 potato",
            "Invalid buffer command",
        );
        check_test_command_error(
            "ubo 1 subdata",
            "Expected GLSL type name",
        );
    }

    #[test]
    fn test_draw_rect() {
        let script = check_test_command(
            "  draw    rect  1 2 3 4 ",
            Operation::DrawRect {
                x: 1.0,
                y: 2.0,
                w: 3.0,
                h: 4.0,
                pipeline_key: 0,
            }
        );
        assert_eq!(script.pipeline_keys().len(), 1);
        assert_eq!(
            script.pipeline_keys()[0].pipeline_type(),
            pipeline_key::Type::Graphics,
        );
        let create_info = script.pipeline_keys()[0].to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            assert_eq!(
                (*create_info.pInputAssemblyState).topology,
                vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            );
            assert_eq!(
                (*create_info.pTessellationState).patchControlPoints,
                4
            );
        };

        let script = check_test_command(
            "draw rect ortho patch 0 0 250 250",
            Operation::DrawRect {
                x: -1.0,
                y: -1.0,
                w: WindowFormat::default().width as f32 * 2.0 / 250.0,
                h: WindowFormat::default().height as f32 * 2.0 / 250.0,
                pipeline_key: 0,
            }
        );
        let create_info = script.pipeline_keys()[0].to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            assert_eq!(
                (*create_info.pInputAssemblyState).topology,
                vk::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
            );
        };

        // Same test again but with the words inversed
        let script = check_test_command(
            "draw rect patch ortho 0 0 250 250",
            Operation::DrawRect {
                x: -1.0,
                y: -1.0,
                w: WindowFormat::default().width as f32 * 2.0 / 250.0,
                h: WindowFormat::default().height as f32 * 2.0 / 250.0,
                pipeline_key: 0,
            }
        );
        let create_info = script.pipeline_keys()[0].to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            assert_eq!(
                (*create_info.pInputAssemblyState).topology,
                vk::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
            );
        };

        check_test_command_error(
            "draw rect",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "draw rect 0",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "draw rect 0 0",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "draw rect 0 0 0",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "draw rect 0 0 0 0 foo",
            "Extra data at end of line",
        );
    }

    fn test_slot_base_type(glsl_type: &str, value: &str, values: &[u8]) {
        let source = format!("ssbo 0 subdata {} 0 {}", glsl_type, value);
        check_test_command(
            &source,
            Operation::SetBufferData {
                desc_set: 0,
                binding: 0,
                offset: 0,
                data: values.to_vec().into_boxed_slice(),
            }
        );

        let source = format!(
            "ssbo 0 subdata {} 0 0xfffffffffffffffff",
            glsl_type
        );
        check_test_command_error(
            &source,
            "number too large to fit in target type"
        );
    }

    #[test]
    fn test_all_slot_base_types() {
        test_slot_base_type("int", "-12", &(-12i32).to_ne_bytes());
        test_slot_base_type("uint", "0xffffffff", &0xffffffffu32.to_ne_bytes());
        test_slot_base_type("int8_t", "-128", &(-128i8).to_ne_bytes());
        test_slot_base_type("uint8_t", "42", &42u8.to_ne_bytes());
        test_slot_base_type("int16_t", "-32768", &(-32768i16).to_ne_bytes());
        test_slot_base_type("uint16_t", "1000", &1000u16.to_ne_bytes());
        test_slot_base_type("int64_t", "-1", &(-1i64).to_ne_bytes());
        test_slot_base_type("uint64_t", "1000", &1000u64.to_ne_bytes());
        test_slot_base_type("float16_t", "1.0", &0x3c00u16.to_ne_bytes());
        test_slot_base_type("float", "1.0", &1.0f32.to_ne_bytes());
        test_slot_base_type("double", "2.0", &2.0f64.to_ne_bytes());
    }

    #[test]
    fn test_all_topologies() {
        for &(name, topology) in TOPOLOGY_NAMES.iter() {
            let command = format!("draw arrays {} 8 6", name);
            let script = check_test_command(
                &command,
                Operation::DrawArrays {
                    topology,
                    indexed: false,
                    vertex_count: 6,
                    first_vertex: 8,
                    instance_count: 1,
                    first_instance: 0,
                    pipeline_key: 0,
                },
            );
            let create_info = script.pipeline_keys()[0].to_create_info();
            unsafe {
                let create_info =
                    &*(create_info.as_ptr()
                       as *const vk::VkGraphicsPipelineCreateInfo);
                assert_eq!(
                    (*create_info.pInputAssemblyState).topology,
                    topology,
                );
            };
        }
    }

    #[test]
    fn test_draw_arrays() {
        check_test_command(
            "   draw    arrays    POINT_LIST   8   6   # comment",
            Operation::DrawArrays {
                topology: vk::VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
                indexed: false,
                first_vertex: 8,
                vertex_count: 6,
                instance_count: 1,
                first_instance: 0,
                pipeline_key: 0,
            },
        );

        check_test_command(
            "draw arrays indexed GL_TRIANGLES 1 2",
            Operation::DrawArrays {
                topology: vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                indexed: true,
                first_vertex: 1,
                vertex_count: 2,
                instance_count: 1,
                first_instance: 0,
                pipeline_key: 0,
            },
        );

        check_test_command(
            "draw arrays instanced TRIANGLE_LIST 1 2 56",
            Operation::DrawArrays {
                topology: vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                indexed: false,
                first_vertex: 1,
                vertex_count: 2,
                instance_count: 56,
                first_instance: 0,
                pipeline_key: 0,
            },
        );

        check_test_command_error(
            "draw arrays",
            "Expected topology name",
        );
        check_test_command_error(
            "draw arrays OBLONG_LIST",
            "Unknown topology: OBLONG_LIST",
        );
        check_test_command_error(
            "draw arrays TRIANGLE_LIST",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "draw arrays TRIANGLE_LIST 1",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "draw arrays instanced TRIANGLE_LIST 1 2",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "draw arrays instanced TRIANGLE_LIST 1 2 3 foo",
            "Extra data at end of line",
        );
    }

    #[test]
    fn test_compute() {
        let script = check_test_command(
            "compute 1 2 3",
            Operation::DispatchCompute {
                x: 1,
                y: 2,
                z: 3,
                pipeline_key: 0,
            },
        );
        assert_eq!(script.pipeline_keys().len(), 1);
        assert_eq!(
            script.pipeline_keys()[0].pipeline_type(),
            pipeline_key::Type::Compute,
        );

        check_test_command_error(
            "compute",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "compute 1",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "compute 1 2",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "compute 1 2 3 foo",
            "Extra data at end of line",
        );
    }

    fn check_probe_ssbo_comparison(name: &str, comparison: slot::Comparison) {
        let command = format!("probe ssbo uint8_t 1 2 {} 3", name);
        check_test_command(
            &command,
            Operation::ProbeSsbo {
                desc_set: 0,
                binding: 1,
                offset: 2,
                slot_type: slot::Type::UInt8,
                comparison: comparison,
                layout: slot::Layout {
                    std: slot::LayoutStd::Std430,
                    major: slot::MajorAxis::Column,
                },
                values: Box::new([3]),
                tolerance: Tolerance::default(),
            },
        );
    }

    #[test]
    fn test_probe_ssbo() {
        check_test_command(
            "  probe   ssbo   u8vec2   1:3   42   ==   5   6  # comment ",
            Operation::ProbeSsbo {
                desc_set: 1,
                binding: 3,
                offset: 42,
                slot_type: slot::Type::U8Vec2,
                comparison: slot::Comparison::Equal,
                layout: slot::Layout {
                    std: slot::LayoutStd::Std430,
                    major: slot::MajorAxis::Column,
                },
                values: Box::new([5, 6]),
                tolerance: Tolerance::default(),
            },
        );

        check_probe_ssbo_comparison("==", slot::Comparison::Equal);
        check_probe_ssbo_comparison("~=", slot::Comparison::FuzzyEqual);
        check_probe_ssbo_comparison("!=", slot::Comparison::NotEqual);
        check_probe_ssbo_comparison("<", slot::Comparison::Less);
        check_probe_ssbo_comparison(">=", slot::Comparison::GreaterEqual);
        check_probe_ssbo_comparison(">", slot::Comparison::Greater);
        check_probe_ssbo_comparison("<=", slot::Comparison::LessEqual);

        check_test_command_error(
            "probe ssbo uint33_t 1 42 == 5",
            "Invalid GLSL type name: uint33_t",
        );
        check_test_command_error(
            "probe ssbo uint16_t :1 1 == 5",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "probe ssbo uint16_t 1 foo == 5",
            "invalid digit found in string",
        );
        check_test_command_error(
            "probe ssbo uint16_t 1 1",
            "Expected comparison operator",
        );
        check_test_command_error(
            "probe ssbo uint16_t 1 1 (=) 5",
            "Unknown comparison operator: (=)",
        );
        check_test_command_error(
            "probe ssbo uint16_t 1 1 ==",
            "cannot parse integer from empty string",
        );
    }

    #[test]
    fn test_clear() {
        check_test_command(
            "  clear     # comment ",
            Operation::Clear {
                color: [0.0, 0.0, 0.0, 0.0],
                depth: 1.0,
                stencil: 0,
            },
        );
    }

    #[test]
    fn test_clear_values() {
        let script = script_from_string(
            "[test]\n\
             clear  color  1 2 3 4\n\
             clear   depth   64.0 # comment\n\
             clear  stencil   32\n\
             clear\n\
             clear color 5 6 7 8\n\
             clear depth 10\n\
             clear stencil 33\n\
             clear".to_string(),
        );
        assert_eq!(script.commands().len(), 2);
        assert_eq!(
            script.commands()[0].op,
            Operation::Clear {
                color: [1.0, 2.0, 3.0, 4.0],
                depth: 64.0,
                stencil: 32,
            },
        );
        assert_eq!(
            script.commands()[1].op,
            Operation::Clear {
                color: [5.0, 6.0, 7.0, 8.0],
                depth: 10.0,
                stencil: 33,
            },
        );

        check_test_command_error(
            "clear color",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "clear color 1",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "clear color 1 2",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "clear color 1 2 3",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "clear color 1 2 3 4 foo",
            "Invalid clear color command",
        );
        check_test_command_error(
            "clear depth",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "clear depth 1 foo",
            "Invalid clear depth command",
        );
        check_test_command_error(
            "clear stencil",
            "cannot parse integer from empty string",
        );
        check_test_command_error(
            "clear stencil 1 foo",
            "Invalid clear stencil command",
        );
        check_test_command_error(
            "clear the dining table",
            "Invalid test command",
        );
    }

    fn pipeline_line_width(key: &pipeline_key::Key) -> f32 {
        let create_info = key.to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            (*create_info.pRasterizationState).lineWidth
        }
    }

    fn pipeline_depth_bias_clamp(key: &pipeline_key::Key) -> f32 {
        let create_info = key.to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            (*create_info.pRasterizationState).depthBiasClamp
        }
    }

    #[test]
    fn test_pipeline_properties() {
        let script = script_from_string(
            "[test]\n\
             lineWidth 4.0\n\
             depthBiasClamp 8.0\n\
             draw rect 1 1 2 3\n\
             compute 1 1 1\n\
             \n\
             lineWidth 2.0\n\
             draw rect 1 1 2 3\n\
             # The properties don’t affect the compute pipeline so this\n\
             # resuse the same pipeline\n\
             compute 1 1 1\n\
             \n\
             # Put the line width back to what it was for the first pipeline.\n\
             # This should reuse the same pipeline.
             lineWidth 4.0\n\
             draw rect 1 1 2 3".to_string()
        );
        assert_eq!(script.pipeline_keys().len(), 3);

        assert_eq!(
            script.pipeline_keys()[0].pipeline_type(),
            pipeline_key::Type::Graphics,
        );
        assert_eq!(pipeline_line_width(&script.pipeline_keys()[0]), 4.0);
        assert_eq!(pipeline_depth_bias_clamp(&script.pipeline_keys()[0]), 8.0);

        assert_eq!(
            script.pipeline_keys()[1].pipeline_type(),
            pipeline_key::Type::Compute,
        );

        assert_eq!(
            script.pipeline_keys()[2].pipeline_type(),
            pipeline_key::Type::Graphics,
        );
        assert_eq!(pipeline_line_width(&script.pipeline_keys()[2]), 2.0);
        assert_eq!(pipeline_depth_bias_clamp(&script.pipeline_keys()[2]), 8.0);

        // Test processing a line without a key directly because it’s
        // probably not really possible to trigger this code any other
        // way.
        let source = Source::from_string(String::new());
        let mut loader = Loader::new(&source).unwrap();
        assert!(matches!(
            loader.process_pipeline_property(""),
            Ok(MatchResult::NotMatched)
        ));

        check_test_command_error(
            "lineWidth",
            "Invalid value: ",
        );
        check_test_command_error(
            "lineWidth 8.0 foo",
            "Invalid value: 8.0 foo",
        );
        check_test_command_error(
            "lineWidth foo",
            "Invalid value: foo",
        );
    }

    #[test]
    fn test_layout() {
        let script = script_from_string(
            "[test]\n\
             ssbo    layout   std140   row_major  # comment\n\
             ubo  layout  std430 row_major\n\
             push   layout  row_major  std140\n\
             \n\
             ssbo 1 subdata mat2 0  1 2 3 4\n\
             ubo 0 subdata mat2 0  1 2 3 4\n\
             push mat2 4   1 2 3 4\n\
             \n\
             # Setting only one of the properties resets the other to\n\
             # the default\n\
             ssbo layout row_major\n\
             ubo layout std430 column_major\n\
             push layout std140\n\
             \n\
             ssbo 1 subdata mat2 0  1 2 3 4\n\
             ubo 0 subdata mat2 0  1 2 3 4\n\
             push mat2 4   1 2 3 4".to_string(),
        );

        let mut std140_row = Vec::new();
        std140_row.extend_from_slice(&1.0f32.to_ne_bytes());
        std140_row.extend_from_slice(&3.0f32.to_ne_bytes());
        // std140 pads up to 16 bytes
        std140_row.resize(16, 0);
        std140_row.extend_from_slice(&2.0f32.to_ne_bytes());
        std140_row.extend_from_slice(&4.0f32.to_ne_bytes());

        let mut std140_column = Vec::new();
        std140_column.extend_from_slice(&1.0f32.to_ne_bytes());
        std140_column.extend_from_slice(&2.0f32.to_ne_bytes());
        // std140 pads up to 16 bytes
        std140_column.resize(16, 0);
        std140_column.extend_from_slice(&3.0f32.to_ne_bytes());
        std140_column.extend_from_slice(&4.0f32.to_ne_bytes());

        let mut std430_row = Vec::new();
        std430_row.extend_from_slice(&1.0f32.to_ne_bytes());
        std430_row.extend_from_slice(&3.0f32.to_ne_bytes());
        std430_row.extend_from_slice(&2.0f32.to_ne_bytes());
        std430_row.extend_from_slice(&4.0f32.to_ne_bytes());

        let mut std430_column = Vec::new();
        std430_column.extend_from_slice(&1.0f32.to_ne_bytes());
        std430_column.extend_from_slice(&2.0f32.to_ne_bytes());
        std430_column.extend_from_slice(&3.0f32.to_ne_bytes());
        std430_column.extend_from_slice(&4.0f32.to_ne_bytes());

        assert_eq!(script.commands().len(), 6);

        assert_eq!(
            script.commands()[0].op,
            Operation::SetBufferData {
                desc_set: 0,
                binding: 1,
                offset: 0,
                data: std140_row.clone().into_boxed_slice(),
            },
        );
        assert_eq!(
            script.commands()[1].op,
            Operation::SetBufferData {
                desc_set: 0,
                binding: 0,
                offset: 0,
                data: std430_row.clone().into_boxed_slice(),
            },
        );
        assert_eq!(
            script.commands()[2].op,
            Operation::SetPushCommand {
                offset: 4,
                data: std140_row.clone().into_boxed_slice(),
            },
        );

        assert_eq!(
            script.commands()[3].op,
            Operation::SetBufferData {
                desc_set: 0,
                binding: 1,
                offset: 0,
                data: std430_row.clone().into_boxed_slice(),
            },
        );
        assert_eq!(
            script.commands()[4].op,
            Operation::SetBufferData {
                desc_set: 0,
                binding: 0,
                offset: 0,
                data: std430_column.clone().into_boxed_slice(),
            },
        );
        assert_eq!(
            script.commands()[5].op,
            Operation::SetPushCommand {
                offset: 4,
                data: std140_column.clone().into_boxed_slice(),
            },
        );

        check_test_command_error(
            "ssbo layout std140 hexagonal",
            "Unknown layout parameter “hexagonal”",
        );
    }

    #[test]
    fn test_tolerance() {
        let script = script_from_string(
            "[test]\n\
             tolerance   12%   14%  15%   16% # comment !!\n\
             probe rgb (0, 0) (1, 1, 1)\n\
             tolerance 17 18 19.0 20\n\
             probe rgb (0, 0) (1, 1, 1)\n\
             tolerance 21%
             probe rgb (0, 0) (1, 1, 1)\n\
             tolerance 22\n\
             probe rgb (0, 0) (1, 1, 1)".to_string(),
        );

        assert_eq!(script.commands().len(), 4);
        assert_eq!(
            script.commands()[0].op,
            Operation::ProbeRect {
                n_components: 3,
                x: 0,
                y: 0,
                w: 1,
                h: 1,
                color: [1.0, 1.0, 1.0, 0.0],
                tolerance: Tolerance::new([12.0, 14.0, 15.0, 16.0], true),
            },
        );
        assert_eq!(
            script.commands()[1].op,
            Operation::ProbeRect {
                n_components: 3,
                x: 0,
                y: 0,
                w: 1,
                h: 1,
                color: [1.0, 1.0, 1.0, 0.0],
                tolerance: Tolerance::new([17.0, 18.0, 19.0, 20.0], false),
            },
        );
        assert_eq!(
            script.commands()[2].op,
            Operation::ProbeRect {
                n_components: 3,
                x: 0,
                y: 0,
                w: 1,
                h: 1,
                color: [1.0, 1.0, 1.0, 0.0],
                tolerance: Tolerance::new([21.0, 21.0, 21.0, 21.0], true),
            },
        );
        assert_eq!(
            script.commands()[3].op,
            Operation::ProbeRect {
                n_components: 3,
                x: 0,
                y: 0,
                w: 1,
                h: 1,
                color: [1.0, 1.0, 1.0, 0.0],
                tolerance: Tolerance::new([22.0, 22.0, 22.0, 22.0], false),
            },
        );

        check_test_command_error(
            "tolerance 1 2 3 4 5",
            "tolerance command has extra arguments",
        );
        check_test_command_error(
            "tolerance 1 2 3 4%",
            "Either all tolerance values must be a percentage or none",
        );
        check_test_command_error(
            "tolerance 1% 2% 3% 4",
            "Either all tolerance values must be a percentage or none",
        );
        check_test_command_error(
            "tolerance foo 2 3 4",
            "cannot parse float from empty string",
        );
        check_test_command_error(
            "tolerance 2 3",
            "There must be either 1 or 4 tolerance values",
        );
    }

    #[test]
    fn test_entrypoint() {
        let script = script_from_string(
            "[test]\n\
             vertex    entrypoint   lister  # comment\n\
             tessellation    control    entrypoint  rimmer\n\
             tessellation  evaluation  entrypoint  kryten   \n\
             geometry  entrypoint   kochanski\n\
             fragment   entrypoint   holly\n\
             compute    entrypoint   cat\n\
             draw arrays TRIANGLE_LIST 1 1".to_string()
        );

        assert_eq!(script.pipeline_keys().len(), 1);

        let key = &script.pipeline_keys()[0];

        assert_eq!(key.entrypoint(Stage::Vertex), "lister");
        assert_eq!(key.entrypoint(Stage::TessCtrl), "rimmer");
        assert_eq!(key.entrypoint(Stage::TessEval), "kryten");
        assert_eq!(key.entrypoint(Stage::Geometry), "kochanski");
        assert_eq!(key.entrypoint(Stage::Fragment), "holly");
        assert_eq!(key.entrypoint(Stage::Compute), "cat");

        check_test_command_error(
            "geometry entrypoint",
            "Missing entrypoint name",
        );
    }

    #[test]
    fn test_patch_parameter_vertices() {
        let script = script_from_string(
            "[test]\n\
             patch    parameter   vertices   64\n\
             draw arrays PATCH_LIST 1 2".to_string()
        );

        assert_eq!(script.pipeline_keys().len(), 1);

        let create_info = script.pipeline_keys()[0].to_create_info();
        unsafe {
            let create_info =
                &*(create_info.as_ptr()
                   as *const vk::VkGraphicsPipelineCreateInfo);
            assert_eq!(
                (*create_info.pTessellationState).patchControlPoints,
                64
            );
        };

        check_test_command_error(
            "patch parameter vertices 1e2",
            "invalid digit found in string",
        );
        check_test_command_error(
            "patch parameter vertices 1 foo",
            "Invalid patch parameter vertices command",
        );
    }

    #[test]
    fn test_load_from_invalid_file() {
        let source = Source::from_file("this-file-does-not-exist".to_string());
        let e = Script::load(&source).unwrap_err();
        match e {
            LoadError::Stream(StreamError::IoError(e)) => {
                assert_eq!(e.kind(), io::ErrorKind::NotFound);
            },
            _ => unreachable!("expected StreamError::IoError, got: {}", e),
        };
    }

    fn test_glsl_shader(header: &str, stage: Stage) {
        const SHADER_SOURCE: &'static str =
            "# this comment isn’t really a comment and it should stay\n\
             # in the source.\n\
             \n\
             int\n\
             main()\n\
             {\n\
             gl_FragColor = vec4(1.0);\n\
             }";
        let source = format!("{}\n{}", header, SHADER_SOURCE);
        let script = script_from_string(source);

        for i in 0..N_STAGES {
            if i == stage as usize {
                assert_eq!(script.stages[i].len(), 1);
                match &script.stages[i][0] {
                    Shader::Glsl(s) => assert_eq!(s, SHADER_SOURCE),
                    s @ _ => unreachable!("Unexpected shader type: {:?}", s),
                }
            } else {
                assert_eq!(script.stages[i].len(), 0);
            }
        }
    }

    #[test]
    fn test_glsl_shaders() {
        test_glsl_shader("[vertex shader]", Stage::Vertex);
        test_glsl_shader("[tessellation control shader]", Stage::TessCtrl);
        test_glsl_shader("[tessellation evaluation shader]", Stage::TessEval);
        test_glsl_shader("[ geometry  shader]", Stage::Geometry);
        test_glsl_shader("[  fragment   shader  ]", Stage::Fragment);
        test_glsl_shader("[  compute   shader  ]", Stage::Compute);
    }

    #[test]
    fn test_spirv_shader() {
        let script = script_from_string(
            "[fragment shader  spirv  ]\n\
             <insert spirv shader here>".to_string()
        );
        assert_eq!(script.shaders(Stage::Fragment).len(), 1);

        match &script.shaders(Stage::Fragment)[0] {
            Shader::Spirv(s) => assert_eq!(s, "<insert spirv shader here>"),
            s @ _ => unreachable!("Unexpected shader type: {:?}", s),
        }

        check_error(
            "[fragment shader]\n\
             this is a GLSL shader\n\
            [fragment shader spirv]\n\
             this is a SPIR-V shader",
            "line 3: SPIR-V source can not be linked with other shaders in the \
             same stage",
        );
        check_error(
            "[fragment shader spirv]\n\
             this is a SPIR-V shader\n\
             [fragment shader]\n\
             this is a GLSL shader",
            "line 3: SPIR-V source can not be linked with other shaders in the \
             same stage",
        );
    }

    #[test]
    fn test_binary_shader() {
        let script = script_from_string(
            "[vertex shader   binary   ]\n\
             # comments and blank lines are ok\n\
             \n\
             1 2 3\n\
             4 5 feffffff".to_string()
        );

        match &script.shaders(Stage::Vertex)[0] {
            Shader::Binary(data) => {
                assert_eq!(data, &[1, 2, 3, 4, 5, 0xfeffffff])
            },
            s @ _ => unreachable!("Expected binary shader, got: {:?}", s),
        }

        check_error(
            "[fragment shader binary]\n\
             1ffffffff",
            "line 2: Invalid hex value: 1ffffffff"
        );
    }

    #[test]
    fn test_passthrough_vertex_shader() {
        let script = script_from_string(
            "[vertex shader passthrough]\n\
             # comments and blank lines are allowed in this\n\
             # otherwise empty section\n\
             \n".to_string()
        );
        assert_eq!(script.shaders(Stage::Vertex).len(), 1);
        match &script.shaders(Stage::Vertex)[0] {
            Shader::Binary(s) => assert_eq!(
                s,
                &PASSTHROUGH_VERTEX_SHADER.to_vec(),
            ),
            s @ _ => unreachable!("Unexpected shader type: {:?}", s),
        }

        check_error(
            "[vertex shader]\n\
             this is a GLSL shader\n\
             [vertex shader passthrough]",
            "line 3: SPIR-V source can not be linked with other shaders in the \
             same stage",
        );
        check_error(
            "[vertex shader passthrough]\n\
             this line isn’t allowed",
            "line 2: expected empty line",
        );
    }

    fn run_test_bad_utf8(filename: String) {
        fs::write(&filename, b"enchant\xe9 in latin1").unwrap();

        let source = Source::from_file(filename);
        let error = Script::load(&source).unwrap_err();

        match &error {
            LoadError::Stream(StreamError::IoError(e)) => {
                assert_eq!(e.kind(), io::ErrorKind::InvalidData);
            },
            _ => unreachable!("Expected InvalidData error, got: {}", error),
        }

        assert_eq!(error.to_string(), "stream did not contain valid UTF-8");
    }

    #[test]
    fn test_bad_utf8() {
        let mut filename = std::env::temp_dir();
        filename.push("vkrunner-test-bad-utf8-source");
        let filename_str = filename.to_str().unwrap().to_owned();

        // Catch the unwind to try to remove the file that we created
        // if the test fails
        let r = std::panic::catch_unwind(
            move || run_test_bad_utf8(filename_str)
        );

        if let Err(e) = fs::remove_file(filename) {
            assert_eq!(e.kind(), io::ErrorKind::NotFound);
        }

        if let Err(e) = r {
            std::panic::resume_unwind(e);
        }
    }

    #[test]
    fn test_vertex_data() {
        let script = script_from_string(
            "[vertex data]\n\
             0/R8_UNORM\n\
             1\n\
             2\n\
             3".to_string()
        );

        assert_eq!(script.vertex_data().unwrap().raw_data(), &[1, 2, 3]);

        check_error(
            "[vertex data]\n\
             0/R9_UNORM\n\
             12",
            "line 2: Unknown format: R9_UNORM"
        );
        check_error(
            "[vertex data]\n\
             0/R8_UNORM\n\
             12\n\
             [vertex data]\n\
             0/R8G8_UNORM\n\
             14",
            "line 4: Duplicate vertex data section"
        );
        check_error(
            "[vertex data]\n\
             [indices]",
            "line 2: Missing header line",
        );
        check_error(
            "[vertex data]",
            "line 2: Missing header line",
        );
    }

    #[test]
    fn test_parse_version() {
        let source = Source::from_string(String::new());
        let loader = Loader::new(&source).unwrap();
        assert_eq!(loader.parse_version("  1.2.3  ").unwrap(), (1, 2, 3));
        assert_eq!(loader.parse_version("1.2").unwrap(), (1, 2, 0));
        assert_eq!(loader.parse_version("1").unwrap(), (1, 0, 0));

        assert_eq!(
            loader.parse_version("1.2.3.4").unwrap_err().to_string(),
            "line 0: Invalid Vulkan version",
        );
        assert_eq!(
            loader.parse_version("1.foo.3").unwrap_err().to_string(),
            "line 0: Invalid Vulkan version",
        );
        assert_eq!(
            loader.parse_version("").unwrap_err().to_string(),
            "line 0: Invalid Vulkan version",
        );
        assert_eq!(
            loader.parse_version("1.2 foo").unwrap_err().to_string(),
            "line 0: Invalid Vulkan version",
        );
    }

    #[test]
    fn test_comment_section() {
        let script = script_from_string(
            "[comment]\n\
             this is a comment. It will be ignored.\n\
             # this is a comment within a comment. It will be doubly ignored.\n\
             \n\
             \x20   [this isn’t a section header]\n\
             [test]\n\
             draw arrays TRIANGLE_LIST 1 2".to_string()
        );
        assert_eq!(script.commands().len(), 1);
        assert!(matches!(
            script.commands()[0].op, Operation::DrawArrays { .. }
        ));
    }

    #[test]
    fn test_requires_section() {
        let script = script_from_string(
            "[comment]\n\
             a comment section can appear before the require section\n\
             [require]\n\
             # comments and blank lines are ok\n\
             \n\
             framebuffer R8_UNORM\n\
             depthstencil R8G8_UNORM\n\
             fbsize 12 32\n\
             vulkan 1.2.3\n\
             shaderBufferInt64Atomics\n\
             VK_KHR_multiview".to_string()
        );

        assert_eq!(
            script.window_format().color_format,
            Format::lookup_by_vk_format(vk::VK_FORMAT_R8_UNORM),
        );
        assert_eq!(
            script.window_format().depth_stencil_format,
            Some(Format::lookup_by_vk_format(vk::VK_FORMAT_R8G8_UNORM)),
        );
        assert_eq!(
            (script.window_format().width, script.window_format().height),
            (12, 32),
        );

        let reqs = script.requirements();

        assert_eq!(reqs.version(), make_version(1, 2, 3));

        let mut extensions = std::collections::HashSet::<String>::new();
        extensions.insert("VK_KHR_shader_atomic_int64".to_owned());
        extensions.insert("VK_KHR_multiview".to_owned());

        for &ext in reqs.c_extensions().iter() {
            unsafe {
                let ext_c = CStr::from_ptr(ext as *const c_char);
                let ext = ext_c.to_str().unwrap();
                assert!(extensions.remove(ext));
            }
        }

        assert_eq!(extensions.len(), 0);

        check_error(
            "[require]\n\
             framebuffer R9_UNORM",
            "line 2: Unknown format: R9_UNORM"
        );
        check_error(
            "[require]\n\
             depthstencil R9_UNORM",
            "line 2: Unknown format: R9_UNORM"
        );
        check_error(
            "[require]\n\
             framebuffer   ",
            "line 2: Missing format name"
        );
        check_error(
            "[require]\n\
             fbsize",
            "line 2: cannot parse integer from empty string",
        );
        check_error(
            "[require]\n\
             fbsize 1",
            "line 2: cannot parse integer from empty string",
        );
        check_error(
            "[require]\n\
             fbsize 1 2 3",
            "line 2: Invalid fbsize",
        );
        check_error(
            "[require]\n\
             vulkan one point one",
            "line 2: Invalid Vulkan version",
        );
        check_error(
            "[require]\n\
             extension_name_with spaces",
            "line 2: Invalid require line"
        );
        check_error(
            "[indices]\n\
             1\n\
             [require]\n\
             framebuffer R8_UNORM",
            "line 3: [require] must be the first section"
        );
    }

    #[test]
    fn test_indices() {
        let script = script_from_string(
            "[indices]\n\
             # comments and blank lines are ok\n\
             \n\
             0 1\n\
             2\n\
             3 4 5".to_string()
        );
        assert_eq!(script.indices(), [0, 1, 2, 3, 4, 5]);

        check_error(
            "[indices]\n\
             \n\
             65536",
            "line 3: number too large to fit in target type"
        );
    }

    #[test]
    fn test_filename() {
        let script = script_from_string(String::new());
        assert_eq!(script.filename(), "(string source)");
    }

    #[test]
    fn test_bad_section() {
        check_error(
            "[   reticulated splines   ]",
            "line 1: Unknown section “reticulated splines”",
        );
        check_error(
            "[I forgot to close the door",
            "line 1: Missing ‘]’",
        );
        check_error(
            "[vertex shader] <-- this is a great section",
            "line 1: Trailing data after ‘]’",
        );
    }

    #[test]
    fn test_replace_shaders_stage_binary() {
        let mut script = script_from_string(
            "[vertex shader]\n\
             shader one\n\
             [vertex shader]\n\
             shader two".to_string()
        );

        script.replace_shaders_stage_binary(Stage::Vertex, &[0, 1, 2]);

        assert_eq!(script.shaders(Stage::Vertex).len(), 1);

        match &script.shaders(Stage::Vertex)[0] {
            Shader::Binary(data) => assert_eq!(data, &[0, 1, 2]),
            s @ _ => unreachable!("Unexpected shader type: {:?}", s),
        }
    }

    fn check_buffer_bindings_sorted(script: &Script) {
        for (last_buffer_num, buffer) in script
            .buffers()[1..]
            .iter()
            .enumerate()
        {
            let last_buffer = &script.buffers()[last_buffer_num];
            assert!(buffer.desc_set >= last_buffer.desc_set);
            if buffer.desc_set == last_buffer.desc_set {
                assert!(buffer.binding > last_buffer.binding);
            }
        }
    }

    #[test]
    fn sorted_buffer_bindings() {
        let script = script_from_string(
            "[test]\n\
             ssbo 0:0 10\n\
             ssbo 0:1 10\n\
             ssbo 0:2 10\n\
             ssbo 1:0 10\n\
             ssbo 1:1 10\n\
             ssbo 1:2 10\n\
             ssbo 2:0 10\n\
             ssbo 2:1 10\n\
             ssbo 2:2 10\n".to_string()
        );
        assert_eq!(script.buffers().len(), 9);
        check_buffer_bindings_sorted(&script);

        let script = script_from_string(
            "[test]\n\
             ssbo 2:2 10\n\
             ssbo 2:1 10\n\
             ssbo 2:0 10\n\
             ssbo 1:2 10\n\
             ssbo 1:1 10\n\
             ssbo 1:0 10\n\
             ssbo 0:2 10\n\
             ssbo 0:1 10\n\
             ssbo 0:0 10\n".to_string()
        );
        assert_eq!(script.buffers().len(), 9);
        check_buffer_bindings_sorted(&script);
    }
}

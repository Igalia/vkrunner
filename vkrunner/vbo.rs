// Copyright © 2011, 2016, 2018 Intel Corporation
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

// Based on piglit-vbo.cpp

//! This module adds the facility for specifying vertex data to
//! VkRunner tests using a columnar text format, for example:
//!
//! ```text
//!   0/r32g32b32_sfloat 1/r32_uint      3/int/int       4/int/int
//!   0.0 0.0 0.0        10              0               0       # comment
//!   0.0 1.0 0.0         5              1               1
//!   1.0 1.0 0.0         0              0               1
//! ```
//!
//! The format consists of a row of column headers followed by any
//! number of rows of data. Each column header has the form
//! `ATTRLOC/FORMAT` where `ATTRLOC` is the location of the vertex
//! attribute to be bound to this column and FORMAT is the name of a
//! VkFormat minus the `VK_FORMAT` prefix.
//!
//! Alternatively the column header can use something closer to the
//! Piglit format like `ATTRLOC/GL_TYPE/GLSL_TYPE`. `GL_TYPE` is the
//! GL type of data that follows (“`half`”, “`float`”, “`double`”,
//! “`byte`”, “`ubyte`”, “`short`”, “`ushort`”, “`int`” or “`uint`”),
//! `GLSL_TYPE` is the GLSL type of the data (“`int`”, “`uint`”,
//! “`float`”, “`double`”, “`ivec*`”, “`uvec*`”, “`vec*`”, “`dvec*`”).
//!
//! The data follows the column headers in space-separated form. `#`
//! can be used for comments, as in shell scripts.
//!
//! The text can be parsed either by using the [`str::parse::<Vbo>`]
//! method to parse an entire string, or by constructing a [Parser]
//! object to parse the data line-by-line.

use crate::format::{Format, Mode};
use crate::{util, parse_num, hex};
use std::fmt;
use std::cell::RefCell;
use std::ffi::{c_void, c_uint};

#[derive(Debug, Clone)]
pub enum Error {
    InvalidHeader(String),
    InvalidData(String),
}

/// Struct representing a blob of structured data that can be used as
/// vertex inputs to Vulkan. The Vbo can be constructed either by
/// parsing an entire string slice with the [str::parse] method or by
/// parsing the source line-by-line by constructing a [Parser] object.
#[derive(Debug)]
pub struct Vbo {
    // Description of each attribute
    attribs: Box<[Attrib]>,
    // Raw data buffer containing parsed numbers
    raw_data: Box<[u8]>,
    // Number of bytes in each row of raw_data
    stride: usize,
    // Number of rows in raw_data
    num_rows: usize,
}

#[derive(Debug)]
pub struct Attrib {
    format: &'static Format,

    // Vertex location
    location: u32,
    // Byte offset into the vertex data of this attribute
    offset: usize,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        match &self {
            Error::InvalidHeader(s) => write!(f, "{}", s),
            Error::InvalidData(s) => write!(f, "{}", s),
        }
    }
}

impl Vbo {
    #[inline]
    pub fn attribs(&self) -> &[Attrib] {
        self.attribs.as_ref()
    }

    #[inline]
    pub fn raw_data(&self) -> &[u8] {
        self.raw_data.as_ref()
    }

    #[inline]
    pub fn stride(&self) -> usize {
        self.stride
    }

    #[inline]
    pub fn num_rows(&self) -> usize {
        self.num_rows
    }
}

impl Attrib {
    #[inline]
    pub fn format(&self) -> &'static Format {
        self.format
    }

    #[inline]
    pub fn location(&self) -> u32 {
        self.location
    }

    #[inline]
    pub fn offset(&self) -> usize {
        self.offset
    }
}

/// Helper struct to construct a [Vbo] by parsing data line-by-line.
/// Construct the parser with [new](Parser::new) and then add each
/// line with [parse_line](Parser::parse_line). When the vbo data is
/// complete call [into_vbo](Parser::into_vbo) to finish the parsing
/// and convert the parser into the final Vbo.
#[derive(Debug)]
pub struct Parser {
    // None if we haven’t parsed the header line yet, otherwise an
    // array of attribs
    attribs: Option<Box<[Attrib]>>,

    raw_data: RefCell<Vec<u8>>,

    stride: usize,

    num_rows: usize,
}

macro_rules! invalid_header {
    ($data:expr, $($message:expr),+) => {
        return Err(Error::InvalidHeader(format!($($message),+)))
    };
}

macro_rules! invalid_data {
    ($data:expr, $($message:expr),+) => {
        return Err(Error::InvalidData(format!($($message),+)))
    };
}

impl std::str::FromStr for Vbo {
    type Err = Error;

    fn from_str(s: &str) -> Result<Vbo, Error> {
        let mut data = Parser::new();

        for line in s.lines() {
            data.parse_line(line)?;
        }

        data.into_vbo()
    }
}

impl Parser {
    pub fn new() -> Parser {
        Parser {
            raw_data: RefCell::new(Vec::new()),
            stride: 0,
            attribs: None,
            num_rows: 0,
        }
    }

    fn trim_line(line: &str) -> &str {
        let line = line.trim_start();

        // Remove comments at the end of the line
        let line = match line.trim_start().find('#') {
            Some(end) => &line[0..end],
            None => line,
        };

        line.trim_end()
    }

    fn lookup_gl_type(&self, gl_type: &str) -> Result<(Mode, usize), Error> {
        struct GlType {
            name: &'static str,
            mode: Mode,
            bit_size: usize,
        }
        static GL_TYPES: [GlType; 9] = [
            GlType { name: "byte", mode: Mode::SINT, bit_size: 8 },
            GlType { name: "ubyte", mode: Mode::UINT, bit_size: 8 },
            GlType { name: "short", mode: Mode::SINT, bit_size: 16 },
            GlType { name: "ushort", mode: Mode::UINT, bit_size: 16 },
            GlType { name: "int", mode: Mode::SINT, bit_size: 32 },
            GlType { name: "uint", mode: Mode::UINT, bit_size: 32 },
            GlType { name: "half", mode: Mode::SFLOAT, bit_size: 16 },
            GlType { name: "float", mode: Mode::SFLOAT, bit_size: 32 },
            GlType { name: "double", mode: Mode::SFLOAT, bit_size: 64 },
        ];

        match GL_TYPES.iter().find(|t| t.name == gl_type) {
            Some(t) => Ok((t.mode, t.bit_size)),
            None => invalid_header!(self, "Unknown GL type: {}", gl_type),
        }
    }

    fn components_for_glsl_type(
        &self,
        glsl_type: &str
    ) -> Result<usize, Error> {
        if ["int", "uint", "float", "double"].contains(&glsl_type) {
            return Ok(1);
        }

        let vec_part = match glsl_type.chars().next() {
            Some('i' | 'u' | 'd') => &glsl_type[1..],
            _ => glsl_type,
        };

        if !vec_part.starts_with("vec") {
            invalid_header!(self, "Unknown GLSL type: {}", glsl_type);
        }

        match vec_part[3..].parse::<usize>() {
            Ok(n) if n >= 2 && n <= 4 => Ok(n),
            _ => invalid_header!(self, "Invalid vec size: {}", glsl_type),
        }
    }

    fn decode_type(
        &self,
        gl_type: &str,
        glsl_type: &str
    ) -> Result<&'static Format, Error> {
        let (mode, bit_size) = self.lookup_gl_type(gl_type)?;
        let n_components = self.components_for_glsl_type(glsl_type)?;

        match Format::lookup_by_details(bit_size, mode, n_components) {
            Some(f) => Ok(f),
            None => {
                invalid_header!(
                    self,
                    "Invalid type combo: {}/{}",
                    gl_type,
                    glsl_type
                );
            },
        }
    }

    fn parse_attrib(
        &mut self,
        s: &str,
        offset: usize
    ) -> Result<Attrib, Error> {
        let mut parts = s.split('/');

        let location = match parts.next().unwrap().parse::<u32>() {
            Ok(n) => n,
            Err(_) => invalid_header!(self, "Invalid attrib location in {}", s),
        };

        let format_name = match parts.next() {
            Some(n) => n,
            None => {
                invalid_header!(
                    self,
                    "Column headers must be in the form \
                     location/format. Got: {}",
                    s
                );
            },
        };

        let format = match parts.next() {
            None => match Format::lookup_by_name(format_name) {
                None => {
                    invalid_header!(
                        self,
                        "Unknown format: {}",
                        format_name
                    );
                },
                Some(f) => f,
            },
            Some(glsl_type) => {
                if let Some(_) = parts.next() {
                    invalid_header!(
                        self,
                        "Extra data at end of column header: {}",
                        s
                    );
                }

                self.decode_type(format_name, glsl_type)?
            },
        };

        Ok(Attrib {
            format,
            location,
            offset: util::align(offset, format.alignment()),
        })
    }

    fn parse_header_line(&mut self, line: &str) -> Result<(), Error> {
        let mut attribs = Vec::new();
        let mut stride = 0;
        let mut max_alignment = 1;

        for attrib in line.split_whitespace() {
            let attrib = self.parse_attrib(attrib, stride)?;

            stride = attrib.offset + attrib.format.size();

            let alignment = attrib.format.alignment();

            if alignment > max_alignment {
                max_alignment = alignment;
            }

            attribs.push(attrib);
        }

        self.attribs = Some(attribs.into_boxed_slice());
        self.stride = util::align(stride, max_alignment);

        Ok(())
    }

    #[inline]
    fn write_bytes(data: &mut [u8], bytes: &[u8]) {
        data[0..bytes.len()].copy_from_slice(bytes);
    }

    fn parse_unsigned_datum<'a>(
        &self,
        bit_size: usize,
        text: &'a str,
        data: &mut [u8],
    ) -> Result<&'a str, Error> {
        match bit_size {
            8 => match parse_num::parse_u8(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as unsigned byte")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            16 => match parse_num::parse_u16(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as unsigned short")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            32 => match parse_num::parse_u32(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as unsigned int")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            64 => match parse_num::parse_u64(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as unsigned long")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            _ => unreachable!("unexpected bit size {}", bit_size),
        }
    }

    fn parse_signed_datum<'a>(
        &self,
        bit_size: usize,
        text: &'a str,
        data: &mut [u8],
    ) -> Result<&'a str, Error> {
        match bit_size {
            8 => match parse_num::parse_i8(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as signed byte")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            16 => match parse_num::parse_i16(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as signed short")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            32 => match parse_num::parse_i32(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as signed int")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            64 => match parse_num::parse_i64(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as signed long")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            _ => unreachable!("unexpected bit size {}", bit_size),
        }
    }

    fn parse_float_datum<'a>(
        &self,
        bit_size: usize,
        text: &'a str,
        data: &mut [u8],
    ) -> Result<&'a str, Error> {
        match bit_size {
            16 => match hex::parse_half_float(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as half float")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            32 => match hex::parse_f32(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as float")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            64 => match hex::parse_f64(text) {
                Err(_) => {
                    invalid_data!(self, "Couldn’t parse as double")
                },
                Ok((v, tail)) => {
                    Parser::write_bytes(data, &v.to_ne_bytes());
                    Ok(tail)
                },
            },
            _ => unreachable!("unexpected bit size {}", bit_size),
        }
    }

    // Parse a single number (floating point or integral) from one of
    // the data rows and store it at the start of the `data` slice. If
    // successful it returns the slice in `text` after the number.
    // Otherwise it returns an error.
    fn parse_datum<'a>(
        &self,
        mode: Mode,
        bit_size: usize,
        text: &'a str,
        data: &mut [u8],
    ) -> Result<&'a str, Error> {
        match mode {
            Mode::SFLOAT => self.parse_float_datum(bit_size, text, data),
            Mode::UNORM | Mode::USCALED | Mode::UINT | Mode::SRGB => {
                self.parse_unsigned_datum(bit_size, text, data)
            },
            Mode::SNORM | Mode::SSCALED | Mode::SINT => {
                self.parse_signed_datum(bit_size, text, data)
            },
            Mode::UFLOAT => {
                // This shouldn’t happen because all of the UFLOAT
                // formats are packed so the data will be integers.
                unreachable!("unexpected UFLOAT component");
            },
        }
    }

    // Parse each component of an unpacked data format (floating point
    // or integral) from one of the data rows and store it at the
    // start of the `data` slice. If successful it returns the slice
    // in `text` after the values. Otherwise it returns an error.
    fn parse_unpacked_data<'a>(
        &self,
        format: &'static Format,
        mut text: &'a str,
        mut data: &mut [u8]
    ) -> Result<&'a str, Error> {
        for part in format.parts() {
            text = self.parse_datum(part.mode, part.bits, text, data)?;
            data = &mut data[part.bits / 8..];
        }

        Ok(text)
    }

    fn parse_data_line(&mut self, mut line: &str) -> Result<(), Error> {
        // Allocate space in raw_data for this line
        let old_length = self.raw_data.borrow().len();
        self.raw_data.borrow_mut().resize(old_length + self.stride, 0);

        for attrib in self.attribs.as_ref().unwrap().iter() {
            let data_ptr =
                &mut self.raw_data.borrow_mut()[old_length + attrib.offset..];

            match attrib.format.packed_size() {
                Some(packed_size) => {
                    line = self.parse_unsigned_datum(
                        packed_size,
                        line,
                        data_ptr
                    )?;
                },
                None => {
                    line = self.parse_unpacked_data(
                        attrib.format,
                        line,
                        data_ptr
                    )?;
                }
            }
        }

        if !line.trim_end().is_empty() {
            invalid_data!(self, "Extra data at end of line");
        }

        self.num_rows += 1;

        Ok(())
    }

    /// Add one line of data to the vbo. Returns an error if the line
    /// is invalid.
    pub fn parse_line(&mut self, line: &str) -> Result<(), Error> {
        let line = Parser::trim_line(line);

        // Ignore blank or comment-only lines */
        if line.len() <= 0 {
            return Ok(());
        }

        if self.attribs.is_none() {
            self.parse_header_line(line)?;
        } else {
            self.parse_data_line(line)?;
        }

        Ok(())
    }

    /// Call this at the end of parsing to convert the parser into the
    /// final Vbo. This can fail if the parser didn’t have enough data
    /// to complete the vbo.
    pub fn into_vbo(mut self) -> Result<Vbo, Error> {
        let attribs = match self.attribs.take() {
            None => invalid_header!(data, "Missing header line"),
            Some(a) => a,
        };

        Ok(Vbo {
            attribs,
            raw_data: self.raw_data.into_inner().into_boxed_slice(),
            stride: self.stride,
            num_rows: self.num_rows,
        })
    }
}

#[no_mangle]
pub extern "C" fn vr_vbo_get_raw_data(vbo: *const Vbo) -> *const u8 {
    unsafe { vbo.as_ref().unwrap().raw_data().as_ptr() }
}

#[no_mangle]
pub extern "C" fn vr_vbo_get_stride(vbo: *const Vbo) -> usize {
    unsafe { vbo.as_ref().unwrap().stride() }
}

#[no_mangle]
pub extern "C" fn vr_vbo_get_num_rows(vbo: *const Vbo) -> usize {
    unsafe { vbo.as_ref().unwrap().num_rows() }
}

#[no_mangle]
pub extern "C" fn vr_vbo_get_num_attribs(vbo: *const Vbo) -> usize {
    unsafe { vbo.as_ref().unwrap().attribs().len() }
}

#[no_mangle]
pub extern "C" fn vr_vbo_for_each_attrib(
    vbo: *const Vbo,
    func: unsafe extern "C" fn(&Attrib, *mut c_void),
    user_data: *mut c_void
) {
    let vbo = unsafe { vbo.as_ref().unwrap() };

    for attrib in vbo.attribs().iter() {
        unsafe {
            func(attrib, user_data);
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_vbo_attrib_get_format(
    attrib: *const Attrib
) -> &'static Format {
    unsafe { attrib.as_ref().unwrap().format() }
}

#[no_mangle]
pub extern "C" fn vr_vbo_attrib_get_location(
    attrib: *const Attrib
) -> c_uint {
    unsafe { attrib.as_ref().unwrap().location() as c_uint }
}

#[no_mangle]
pub extern "C" fn vr_vbo_attrib_get_offset(
    attrib: *const Attrib
) -> usize {
    unsafe { attrib.as_ref().unwrap().offset() }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::vk;

    #[test]
    fn test_general() {
        let source = "# position      color \n\
                      0/R32G32_SFLOAT 1/A8B8G8R8_UNORM_PACK32 \n\
                      \n\
                      # Top-left red \n\
                      -1 -1           0xff0000ff \n\
                      0  -1           0xff1200ff";

        let vbo = source.parse::<Vbo>().unwrap();

        assert_eq!(vbo.attribs().len(), 2);
        assert_eq!(vbo.stride(), 4 * 3);
        assert_eq!(vbo.num_rows(), 2);

        assert_eq!(
            vbo.attribs()[0].format(),
            Format::lookup_by_vk_format(vk::VK_FORMAT_R32G32_SFLOAT),
        );
        assert_eq!(vbo.attribs()[0].location(), 0);
        assert_eq!(vbo.attribs()[0].offset(), 0);
        assert_eq!(
            vbo.attribs()[1].format(),
            Format::lookup_by_vk_format(vk::VK_FORMAT_A8B8G8R8_UNORM_PACK32),
        );
        assert_eq!(vbo.attribs()[1].location(), 1);
        assert_eq!(vbo.attribs()[1].offset(), 8);

        assert_eq!(vbo.raw_data().len(), vbo.stride() * vbo.num_rows());

        let mut expected_data = Vec::<u8>::new();
        expected_data.extend(&(-1.0f32).to_ne_bytes());
        expected_data.extend(&(-1.0f32).to_ne_bytes());
        expected_data.extend(0xff0000ffu32.to_ne_bytes());
        expected_data.extend(0.0f32.to_ne_bytes());
        expected_data.extend(&(-1.0f32).to_ne_bytes());
        expected_data.extend(0xff1200ffu32.to_ne_bytes());
        assert_eq!(vbo.raw_data(), &expected_data);
    }

    #[test]
    fn test_no_header() {
        let err = "".parse::<Vbo>().unwrap_err();
        assert_eq!(err.to_string(), "Missing header line");
        assert!(matches!(err, Error::InvalidHeader(_)));
    }

    #[test]
    fn test_line_comment() {
        let source = "0/R32_SFLOAT\n\
                      42.0 # the next number is ignored 32.0";
        let vbo = source.parse::<Vbo>().unwrap();
        assert_eq!(vbo.raw_data(), &42.0f32.to_ne_bytes());
    }

    fn test_gl_type(name: &str, values: &str, expected_bytes: &[u8]) {
        let source = format!("0/{}/int\n{}", name, values);
        let vbo = source.parse::<Vbo>().unwrap();
        assert_eq!(vbo.raw_data(), expected_bytes);
        assert_eq!(vbo.attribs().len(), 1);
        assert_eq!(vbo.attribs()[0].location, 0);
        assert_eq!(vbo.attribs()[0].format.parts().len(), 1);
        assert_eq!(
            vbo.attribs()[0].format.parts()[0].bits,
            expected_bytes.len() * 8
        );
    }

    fn test_glsl_type(name: &str, values: &str, expected_floats: &[f32]) {
        let source = format!("1/float/{}\n{}", name, values);
        let vbo = source.parse::<Vbo>().unwrap();
        let expected_bytes = expected_floats
            .iter()
            .map(|v| v.to_ne_bytes())
            .flatten()
            .collect::<Vec<u8>>();
        assert_eq!(vbo.raw_data(), &expected_bytes);
        assert_eq!(vbo.attribs().len(), 1);
        assert_eq!(vbo.attribs()[0].location, 1);
        assert_eq!(
            vbo.attribs()[0].format.parts().len(),
            expected_floats.len()
        );
        for part in vbo.attribs()[0].format.parts() {
            assert_eq!(part.bits, 32);
        }
    }

    #[test]
    fn test_piglit_style_format() {
        test_gl_type("byte", "-42", &(-42i8).to_ne_bytes());
        test_gl_type("ubyte", "42", &[42u8]);
        test_gl_type("short", "-30000", &(-30000i16).to_ne_bytes());
        test_gl_type("ushort", "65534", &65534u16.to_ne_bytes());
        test_gl_type("int", "-70000", &(-70000i32).to_ne_bytes());
        test_gl_type("uint", "0xffffffff", &u32::MAX.to_ne_bytes());
        test_gl_type("half", "-2", &0xc000u16.to_ne_bytes());
        test_gl_type("float", "1.0000", &1.0f32.to_ne_bytes());
        test_gl_type("double", "32.0000", &32.0f64.to_ne_bytes());

        let err = "1/uverylong/int".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Unknown GL type: uverylong");
        assert!(matches!(err, Error::InvalidHeader(_)));

        test_glsl_type("int", "1.0", &[1.0]);
        test_glsl_type("uint", "2.0", &[2.0]);
        test_glsl_type("float", "3.0", &[3.0]);
        test_glsl_type("double", "4.0", &[4.0]);
        test_glsl_type("vec2", "1.0 2.0", &[1.0, 2.0]);
        test_glsl_type("vec3", "1.0 2.0 3.0", &[1.0, 2.0, 3.0]);
        test_glsl_type("vec4", "1.0 2.0 3.0 4.0", &[1.0, 2.0, 3.0, 4.0]);
        test_glsl_type("ivec2", "1.0 2.0", &[1.0, 2.0]);
        test_glsl_type("uvec2", "1.0 2.0", &[1.0, 2.0]);
        test_glsl_type("dvec2", "1.0 2.0", &[1.0, 2.0]);

        let err = "1/int/ituple2".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Unknown GLSL type: ituple2");
        assert!(matches!(err, Error::InvalidHeader(_)));

        let err = "1/int/ivecfoo".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Invalid vec size: ivecfoo");
        assert!(matches!(err, Error::InvalidHeader(_)));

        let err = "1/int/vec1".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Invalid vec size: vec1");
        assert!(matches!(err, Error::InvalidHeader(_)));

        let err = "1/int/dvec5".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Invalid vec size: dvec5");
        assert!(matches!(err, Error::InvalidHeader(_)));
    }

    #[test]
    fn test_bad_attrib() {
        let err = "foo/int/int".parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), "Invalid attrib location in foo/int/int");
        assert!(matches!(err, Error::InvalidHeader(_)));

        assert_eq!(
            "12".parse::<Vbo>().unwrap_err().to_string(),
            "Column headers must be in the form location/format. \
             Got: 12",
        );

        assert_eq!(
            "1/R76_SFLOAT".parse::<Vbo>().unwrap_err().to_string(),
            "Unknown format: R76_SFLOAT",
        );

        assert_eq!(
            "1/int/int/more_int".parse::<Vbo>().unwrap_err().to_string(),
            "Extra data at end of column header: 1/int/int/more_int",
        );
    }

    #[test]
    fn test_alignment() {
        let source = "1/R8_UNORM 2/R64_SFLOAT 3/R8_UNORM\n \
                      1 12.0 24";
        let vbo = source.parse::<Vbo>().unwrap();
        assert_eq!(vbo.attribs().len(), 3);
        assert_eq!(vbo.attribs()[0].offset, 0);
        assert_eq!(vbo.attribs()[0].format.parts()[0].bits, 8);
        assert_eq!(vbo.attribs()[1].offset, 8);
        assert_eq!(vbo.attribs()[1].format.parts()[0].bits, 64);
        assert_eq!(vbo.attribs()[2].offset, 16);
        assert_eq!(vbo.attribs()[2].format.parts()[0].bits, 8);
        assert_eq!(vbo.stride, 24);
    }

    fn test_type(format: &str, values: &str, expected_bytes: &[u8]) {
        // Add an extra attribute so we can test it got the right offset
        let source = format!("8/{} 9/R8_UNORM\n{} 42", format, values);
        let vbo = source.parse::<Vbo>().unwrap();
        let mut full_expected_bytes = expected_bytes.to_owned();
        full_expected_bytes.push(42);
        assert!(vbo.stride() >= expected_bytes.len() + 1);
        full_expected_bytes.resize(vbo.stride(), 0);
        assert_eq!(vbo.raw_data(), full_expected_bytes);
        assert_eq!(vbo.attribs().len(), 2);
        assert_eq!(
            vbo.attribs()[0].format.parts()[0].bits,
            expected_bytes.len() * 8
        );
    }

    fn test_value_error(format: &str, error_text: &str) {
        let source = format!("0/{}\nfoo", format);
        let err = source.parse::<Vbo>().unwrap_err();
        assert_eq!(&err.to_string(), error_text);
    }

    #[test]
    fn test_parse_datum() {
        test_type("R8_UNORM", "12", &[12u8]);
        test_value_error("R8_UNORM", "Couldn’t parse as unsigned byte");
        test_type("R16_UNORM", "65000", &65000u16.to_ne_bytes());
        test_value_error("R16_USCALED", "Couldn’t parse as unsigned short");
        test_type("R32_UINT", "66000", &66000u32.to_ne_bytes());
        test_value_error("R32_UINT", "Couldn’t parse as unsigned int");
        test_type("R64_UINT", "0xffffffffffffffff", &u64::MAX.to_ne_bytes());
        test_value_error("R64_UINT", "Couldn’t parse as unsigned long");

        test_type("R8_SNORM", "-12", &(-12i8).to_ne_bytes());
        test_value_error("R8_SNORM", "Couldn’t parse as signed byte");
        test_type("R16_SNORM", "-32768", &(-32768i16).to_ne_bytes());
        test_value_error("R16_SNORM", "Couldn’t parse as signed short");
        test_type("R32_SINT", "-66000", &(-66000i32).to_ne_bytes());
        test_value_error("R32_SINT", "Couldn’t parse as signed int");
        test_type("R64_SINT", "0x7fffffffffffffff", &i64::MAX.to_ne_bytes());
        test_value_error("R64_SINT", "Couldn’t parse as signed long");

        test_type("R16_SFLOAT", "0xc000", &0xc000u16.to_ne_bytes());
        test_type("R16_SFLOAT", "-2", &0xc000u16.to_ne_bytes());
        test_value_error("R16_SFLOAT", "Couldn’t parse as half float");
        test_type("R32_SFLOAT", "-2", &(-2.0f32).to_ne_bytes());
        test_value_error("R32_SFLOAT", "Couldn’t parse as float");
        test_type("R64_SFLOAT", "-4", &(-4.0f64).to_ne_bytes());
        test_value_error("R64_SFLOAT", "Couldn’t parse as double");
    }

    #[test]
    fn test_packed_data() {
        let source = "1/B10G11R11_UFLOAT_PACK32\n\
                      0xfedcba98";
        let vbo = source.parse::<Vbo>().unwrap();
        assert_eq!(vbo.raw_data(), &0xfedcba98u32.to_ne_bytes());
    }

    #[test]
    fn test_trailing_data() {
        let source = "1/R8_UNORM\n\
                      23 25 ";
        let err = source.parse::<Vbo>().unwrap_err();
        assert_eq!(err.to_string(), "Extra data at end of line");
    }
}

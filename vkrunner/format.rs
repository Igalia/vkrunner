// vkrunner
//
// Copyright (C) 2018 Intel Corporation
// Copyright 2023 Neil Roberts
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// on the rights to use, copy, modify, merge, publish, distribute, sub
// license, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
// VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

use crate::small_float;
use crate::half_float;
use std::convert::TryInto;
use std::num::NonZeroUsize;
use std::slice;
use std::ffi::CStr;

#[derive(Debug)]
#[repr(C)]
pub struct Format {
    pub vk_format: VkFormat,
    pub name: &'static str,
    pub packed_size: Option<NonZeroUsize>,
    n_parts: usize,
    parts: [Part; 4],
}

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
#[repr(C)]
pub enum Component {
    R,
    G,
    B,
    A,
    D,
    S,
    X,
}

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
#[repr(C)]
pub enum Mode {
    UNORM,
    SNORM,
    USCALED,
    SSCALED,
    UINT,
    SINT,
    UFLOAT,
    SFLOAT,
    SRGB,
}

#[derive(Debug, PartialEq, Eq)]
#[repr(C)]
pub struct Part {
    pub bits: usize,
    pub component: Component,
    pub mode: Mode,
}

include!{"format_table.rs"}

impl PartialEq for Format {
    #[inline]
    fn eq(&self, other: &Format) -> bool {
        // If the Vulkan format enum is the same then everything else
        // about the format should be the same too
        self.vk_format == other.vk_format
    }
}

impl PartialEq<VkFormat> for Format {
    #[inline]
    fn eq(&self, other: &VkFormat) -> bool {
        self.vk_format == *other
    }
}

impl PartialEq<Format> for VkFormat {
    #[inline]
    fn eq(&self, other: &Format) -> bool {
        *self == other.vk_format
    }
}

impl Format {
    pub fn lookup_by_name(name: &str) -> Option<&'static Format> {
        match FORMATS.binary_search_by(|format| format.name.cmp(name)) {
            Ok(pos) => Some(&FORMATS[pos]),
            Err(_) => None,
        }
    }

    pub fn lookup_by_vk_format(vk_format: VkFormat) -> &'static Format {
        for format in FORMATS.iter() {
            if format.vk_format == vk_format {
                return format;
            }
        }

        unreachable!("lookup failed for format {:?}", vk_format);
    }

    pub fn lookup_by_details(
        bit_size: usize,
        mode: Mode,
        n_components: usize
    ) -> Option<&'static Format> {
        static COMP_ORDER: [Component; 4] = [
            Component::R,
            Component::G,
            Component::B,
            Component::A,
        ];

        'format_loop: for format in FORMATS.iter() {
            if format.n_parts != n_components {
                continue;
            }

            if let Some(_) = format.packed_size {
                continue;
            }

            for (i, part) in format.parts().iter().enumerate() {
                if part.bits != bit_size
                    || part.component != COMP_ORDER[i]
                    || part.mode != mode
                {
                    continue 'format_loop;
                }
            }

            return Some(format);
        }

        None
    }

    pub fn parts(&self) -> &[Part] {
        &self.parts[0..self.n_parts]
    }

    pub fn size(&self) -> usize {
        match self.packed_size {
            Some(size) => usize::from(size) / 8,
            None => self.parts().iter().map(|p| p.bits).sum::<usize>() / 8,
        }
    }

    pub fn packed_size(&self) -> Option<usize> {
        self.packed_size.map(|s| usize::from(s))
    }

    pub fn alignment(&self) -> usize {
        match self.packed_size {
            Some(size) => usize::from(size) / 8,
            None => self.parts().iter().map(|p| p.bits).max().unwrap() / 8,
        }
    }
}

impl Mode {
    fn load_packed_part(&self, part: u32, bits: usize) -> f64 {
        assert!(bits < 32);

        match self {
            Mode::SRGB | Mode::UNORM => part as f64 / ((1 << bits) - 1) as f64,
            Mode::SNORM => {
                sign_extend(part, bits) as f64 / ((1 << (bits - 1)) - 1) as f64
            },
            Mode::UINT | Mode::USCALED => part as f64,
            Mode::SSCALED | Mode::SINT => sign_extend(part, bits) as f64,
            Mode::UFLOAT => {
                match bits {
                    10 => small_float::load_unsigned(part, 5, 5),
                    11 => small_float::load_unsigned(part, 5, 6),
                    _ => {
                        unreachable!(
                            "unknown bit size {} in packed UFLOAT \
                             format",
                            bits
                        )
                    },
                }
            },
            Mode::SFLOAT => unreachable!("Unexpected packed SFLOAT format"),
        }
    }

    fn load_part(&self, bits: usize, fb: &[u8]) -> f64 {
        match self {
            Mode::SRGB | Mode::UNORM => {
                match bits {
                    8 => fb[0] as f64 / u8::MAX as f64,
                    16 => {
                        u16::from_ne_bytes(fb[0..2].try_into().unwrap()) as f64
                            / u16::MAX as f64
                    },
                    24 => extract_u24(fb) as f64 / 16777215.0,
                    32 => {
                        u32::from_ne_bytes(fb[0..4].try_into().unwrap()) as f64
                            / u32::MAX as f64
                    },
                    64 => {
                        u64::from_ne_bytes(fb[0..8].try_into().unwrap()) as f64
                            / u64::MAX as f64
                    },
                    _=> unreachable!("unsupported component bit size {}", bits),
                }
            },
            Mode::SNORM => {
                match bits {
                    8 => fb[0] as i8 as f64 / i8::MAX as f64,
                    16 => {
                        i16::from_ne_bytes(fb[0..2].try_into().unwrap()) as f64
                            / i16::MAX as f64
                    },
                    32 => {
                        i32::from_ne_bytes(fb[0..4].try_into().unwrap()) as f64
                            / i32::MAX as f64
                    },
                    64 => {
                        i64::from_ne_bytes(fb[0..8].try_into().unwrap()) as f64
                            / i64::MAX as f64
                    },
                    _=> unreachable!("unsupported component bit size {}", bits),
                }
            },
            Mode::UINT | Mode::USCALED => {
                match bits {
                    8 => fb[0] as f64,
                    16 => {
                        u16::from_ne_bytes(fb[0..2].try_into().unwrap()) as f64
                    },
                    32 => {
                        u32::from_ne_bytes(fb[0..4].try_into().unwrap()) as f64
                    },
                    64 => {
                        u64::from_ne_bytes(fb[0..8].try_into().unwrap()) as f64
                    },
                    _=> unreachable!("unsupported component bit size {}", bits),
                }
            },
            Mode::SINT | Mode::SSCALED => {
                match bits {
                    8 => fb[0] as i8 as f64,
                    16 => {
                        i16::from_ne_bytes(fb[0..2].try_into().unwrap()) as f64
                    },
                    32 => {
                        i32::from_ne_bytes(fb[0..4].try_into().unwrap()) as f64
                    },
                    64 => {
                        i64::from_ne_bytes(fb[0..8].try_into().unwrap()) as f64
                    },
                    _=> unreachable!("unsupported component bit size {}", bits),
                }
            },
            Mode::UFLOAT => unreachable!("unexpected unpacked UFLOAT part"),
            Mode::SFLOAT => {
                match bits {
                    16 => {
                        let bits =
                            u16::from_ne_bytes(fb[0..2].try_into().unwrap());
                        half_float::to_f64(bits)
                    },
                    32 => {
                        f32::from_ne_bytes(fb[0..4].try_into().unwrap()) as f64
                    },
                    64 => f64::from_ne_bytes(fb[0..8].try_into().unwrap()),
                    _ => {
                        unreachable!(
                            "unsupported unpacked SFLOAT size {}",
                            bits
                        );
                    },
                }
            },
        }
    }
}

impl Format {
    fn load_packed_parts(&self, source: &[u8], parts: &mut [f64]) {
        let mut packed_parts = match self.packed_size().unwrap() {
            8 => source[0] as u32,
            16 => u16::from_ne_bytes(source[0..2].try_into().unwrap()) as u32,
            32 => u32::from_ne_bytes(source[0..4].try_into().unwrap()),
            _ => {
                unreachable!(
                    "unsupported packed bit size {}",
                    self.packed_size.unwrap()
                );
            },
        };

        for (i, part) in self.parts().iter().enumerate().rev() {
            let part_bits =
                packed_parts
                & (u32::MAX >> (u32::BITS - part.bits as u32));
            parts[i] = part.mode.load_packed_part(part_bits, part.bits);
            packed_parts >>= part.bits;
        }
    }

    pub fn load_pixel(&self, source: &[u8]) -> [f64; 4] {
        assert!(source.len() >= self.size());
        assert!(self.n_parts <= 4);

        let mut parts = [0.0; 4];

        match self.packed_size {
            Some(_) => self.load_packed_parts(source, &mut parts),
            None => {
                let mut source = source;

                for (i, part) in self.parts().iter().enumerate() {
                    parts[i] = part.mode.load_part(part.bits, source);
                    source = &source[part.bits / 8..];
                }
            },
        }

        // Set all the colour components to zero in case they aren’t
        // contained in the format. The alpha component default to 1.0
        // if it’s not in the format.
        let mut pixel = [0.0, 0.0, 0.0, 1.0];

        for (i, part) in self.parts().iter().enumerate() {
            match part.component {
                Component::R => pixel[0] = parts[i],
                Component::G => pixel[1] = parts[i],
                Component::B => pixel[2] = parts[i],
                Component::A => pixel[3] = parts[i],
                Component::D | Component::S | Component:: X => (),
            }
        }

        pixel
    }
}

fn sign_extend(part: u32, bits: usize) -> i32
{
    let uresult = if part & (1 << (bits - 1)) != 0 {
        ((u32::MAX) << bits) | part
    } else {
        part
    };

    uresult as i32
}

fn extract_u24(bytes: &[u8]) -> u32 {
    let mut value = 0;

    for i in 0..3 {
        let byte_pos;

        #[cfg(target_endian = "little")]
        {
            byte_pos = 2 - i;
        }
        #[cfg(not(target_endian = "little"))]
        {
            byte_pos = i;
        }

        value = ((value) << 8) | bytes[byte_pos] as u32;
    }

    value
}

#[no_mangle]
pub extern "C" fn vr_format_get_size(format: &Format) -> i32 {
    format.size() as i32
}

#[no_mangle]
pub extern "C" fn vr_format_load_pixel(
    format: &Format,
    source: *const u8,
    pixel: *mut f64,
) {
    let source = unsafe { slice::from_raw_parts(source, format.size()) };
    let pixel = unsafe { slice::from_raw_parts_mut(pixel, 4) };
    pixel.copy_from_slice(&format.load_pixel(source));
}

#[no_mangle]
pub extern "C" fn vr_format_get_name(format: &Format) -> *mut u8 {
    extern "C" {
        fn vr_strndup(s: *const u8, len: usize) -> *mut u8;
    }

    unsafe {
        vr_strndup(format.name.as_ptr(), format.name.len())
    }
}

#[no_mangle]
pub extern "C" fn vr_format_lookup_by_name(
    name: *const i8
) -> Option<&'static Format> {
    unsafe {
        Format::lookup_by_name(CStr::from_ptr(name).to_str().unwrap())
    }
}

#[no_mangle]
pub extern "C" fn vr_format_lookup_by_vk_format(
    vk_format: VkFormat
) -> &'static Format {
    Format::lookup_by_vk_format(vk_format)
}

#[no_mangle]
pub extern "C" fn vr_format_lookup_by_details(
    bit_size: i32,
    mode: Mode,
    n_components: i32
) -> Option<&'static Format> {
    Format::lookup_by_details(bit_size as usize, mode, n_components as usize)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_lookup_by_name() {
        // Test that we can find every name
        for format in FORMATS.iter() {
            let other_format = match Format::lookup_by_name(format.name) {
                Some(f) => f,
                None => unreachable!("lookup for {} failed", format.name),
            };

            // Assert that it returns a reference to the same object
            assert!(std::ptr::eq(format, other_format));
        }

        // Test that a similar name fails
        assert!(matches!(Format::lookup_by_name("B8G8R8_SRGBC"), None));
    }

    fn all_format_names() -> Vec<String> {
        let mut names = Vec::new();

        // Extract the names from the source so that we can be sure
        // that every VkFormat has a corresponding entry in the
        // formats array
        let table_source = include_str!("format_table.rs");
        let mut in_enum = false;

        for line in table_source.split("\n") {
            let line = line.trim_start();

            if line.starts_with("pub enum VkFormat") {
                in_enum = true;
            } else if in_enum {
                if line.starts_with("}") {
                    break;
                }

                names.push(line[0..line.find(" = ").unwrap()].to_owned());
            }
        }

        assert!(names.len() == FORMATS.len());

        names
    }

    #[test]
    fn test_every_enum_has_matching_data() {
        for name in all_format_names().iter() {
            match Format::lookup_by_name(name) {
                Some(f) => assert_eq!(name, f.name),
                None => unreachable!("lookup for {} failed", name),
            }
        }
    }

    #[test]
    fn test_lookup_by_vk_format() {
        // Test that we can find every format
        for format in FORMATS.iter() {
            let other_format = Format::lookup_by_vk_format(format.vk_format);
            // Assert that it returns a reference to the same object
            assert!(std::ptr::eq(format, other_format));
        }
    }

    #[test]
    fn test_lookup_by_details() {
        assert_eq!(
            Format::lookup_by_details(8, Mode::UNORM, 3),
            Some(Format::lookup_by_vk_format(VkFormat::R8G8B8_UNORM)),
        );
        assert_eq!(
            Format::lookup_by_details(64, Mode::SFLOAT, 4),
            Some(Format::lookup_by_vk_format(VkFormat::R64G64B64A64_SFLOAT)),
        );
        assert_eq!(
            Format::lookup_by_details(64, Mode::UFLOAT, 4),
            None
        );
    }

    #[test]
    fn test_parts() {
        // Check that parts returns the right size slice for each type
        for format in FORMATS.iter() {
            assert_eq!(format.parts().len(), format.n_parts);
        }

        // Check some types
        assert_eq!(
            Format::lookup_by_vk_format(VkFormat::R8_UINT).parts(),
            &[Part { bits: 8, component: Component::R, mode: Mode::UINT }]
        );
        assert_eq!(
            Format::lookup_by_vk_format(VkFormat::B5G6R5_UNORM_PACK16).parts(),
            &[
                Part { bits: 5, component: Component::B, mode: Mode::UNORM },
                Part { bits: 6, component: Component::G, mode: Mode::UNORM },
                Part { bits: 5, component: Component::R, mode: Mode::UNORM },
            ]
        );
    }

    #[test]
    fn test_size() {
        assert_eq!(
            Format::lookup_by_vk_format(VkFormat::B8G8R8_UINT).size(),
            3
        );
        assert_eq!(
            Format::lookup_by_vk_format(VkFormat::R16G16_SINT).size(),
            4
        );
        assert_eq!(
            Format::lookup_by_vk_format(VkFormat::B5G6R5_UNORM_PACK16).size(),
            2
        );
    }

    #[test]
    fn test_sign_extend() {
        assert_eq!(sign_extend(0xff, 8), -1);
        assert_eq!(sign_extend(1, 8), 1);
    }

    fn assert_float_equal(a: f64, b: f64) {
        assert!((a - b).abs() < 0.01, "a={}, b={}", a, b);
    }

    #[test]
    fn test_load_packed_part() {
        // Test that there are no formats with unsupported packed modes
        for format in FORMATS.iter() {
            if let Some(_) = format.packed_size {
                for part in format.parts().iter() {
                    part.mode.load_packed_part(0, part.bits);
                }
            }
        }

        assert_float_equal(Mode::SRGB.load_packed_part(0x80, 8), 0.5);
        assert_float_equal(Mode::SNORM.load_packed_part(0x80, 8), -1.0);
        assert_float_equal(Mode::SNORM.load_packed_part(0x7f, 8), 1.0);
        assert_float_equal(Mode::UINT.load_packed_part(42, 8), 42.0);
        assert_float_equal(Mode::SINT.load_packed_part(0xff, 8), -1.0);
        assert_float_equal(Mode::UFLOAT.load_packed_part(0x1e0, 10), 1.0);
        assert_float_equal(Mode::UFLOAT.load_packed_part(0x3c0, 11), 1.0);
    }

    fn load_part(mode: Mode, bits: usize, fb_bytes: u64) -> f64 {
        let mut fb = Vec::new();
        let n_bytes = bits / 8;

        for i in 0..n_bytes {
            let byte_pos;

            #[cfg(target_endian = "little")]
            {
                byte_pos = i
            }
            #[cfg(not(target_endian = "little"))]
            {
                byte_pos = n_bytes - 1 - i
            }

            fb.push(((fb_bytes >> (byte_pos * 8)) & 0xff) as u8);
        }

        mode.load_part(bits, &fb)
    }

    #[test]
    fn test_load_part() {
        let dummy_array = [0u8; 8];

        // Test that there are no formats with unsupported unpacked modes
        for format in FORMATS.iter() {
            if let None = format.packed_size {
                for part in format.parts().iter() {
                    assert!(part.bits <= dummy_array.len() * 8);
                    part.mode.load_part(part.bits, &dummy_array);
                }
            }
        }

        assert_float_equal(load_part(Mode::UNORM, 8, 0x80), 0.5);
        assert_float_equal(load_part(Mode::UNORM, 16, 0x8000), 0.5);
        assert_float_equal(load_part(Mode::UNORM, 24, 0x800000), 0.5);
        assert_float_equal(load_part(Mode::UNORM, 32, 0x80000000), 0.5);
        assert_float_equal(load_part(Mode::UNORM, 64, 0x8000000000000000), 0.5);
        assert_float_equal(load_part(Mode::SNORM, 8, 0x81), -1.0);
        assert_float_equal(load_part(Mode::SNORM, 16, 0x8001), -1.0);
        assert_float_equal(load_part(Mode::SNORM, 32, 0x80000001), -1.0);
        assert_float_equal(
            load_part(Mode::SNORM, 64, 0x8000000000000001),
            -1.0
        );
        assert_float_equal(load_part(Mode::UINT, 8, 0x80), 128.0);
        assert_float_equal(load_part(Mode::UINT, 16, 0x8000), 32768.0);
        assert_float_equal(load_part(Mode::UINT, 32, 0x80000000), 2147483648.0);
        assert_float_equal(
            load_part(Mode::UINT, 64, 0x200000000),
            8589934592.0,
        );
        assert_float_equal(load_part(Mode::SINT, 8, 0x80), -128.0);
        assert_float_equal(load_part(Mode::SINT, 16, 0x8000), -32768.0);
        assert_float_equal(
            load_part(Mode::SINT, 32, 0x80000000),
            -2147483648.0
        );
        assert_float_equal(
            load_part(Mode::SINT, 64, u64::MAX),
            -1.0,
        );
        assert_float_equal(load_part(Mode::SFLOAT, 16, 0x3c00), 1.0);
        assert_float_equal(load_part(Mode::SFLOAT, 32, 0x3f800000), 1.0);
        assert_float_equal(
            load_part(Mode::SFLOAT, 64, 0xbfe0000000000000),
            -0.5
        );
    }

    #[test]
    fn test_load_pixel() {
        let dummy_source = [0u8; 32];

        // Test that the code handles every format
        for format in FORMATS.iter() {
            format.load_pixel(&dummy_source);
        }

        let source_data = [5.0f64, 4.0f64, -3.0f64, 0.5f64];
        let pixel: Vec<u8> = source_data
            .iter()
            .map(|v| v.to_ne_bytes())
            .flatten()
            .collect();
        let format = Format::lookup_by_vk_format(VkFormat::R64G64B64A64_SFLOAT);
        assert_eq!(format.load_pixel(&pixel), source_data);

        // Try a depth-stencil format. This should just return the
        // default rgb values.
        let format = Format::lookup_by_vk_format(VkFormat::D24_UNORM_S8_UINT);
        assert_eq!(format.load_pixel(&[0xff; 4]), [0.0, 0.0, 0.0, 1.0]);

        // Packed format
        let format = Format::lookup_by_vk_format(VkFormat::R5G6B5_UNORM_PACK16);
        let pixel = format.load_pixel(&0xae27u16.to_ne_bytes());
        let expected = [
            0b10101 as f64 / 0b11111 as f64,
            0b110001 as f64 / 0b111111 as f64,
            0b00111 as f64 / 0b11111 as f64,
            1.0,
        ];
        for (i, &pixel_comp) in pixel.iter().enumerate() {
            assert_float_equal(pixel_comp, expected[i]);
        }
    }

    #[test]
    fn test_alignment() {
        let format = Format::lookup_by_vk_format(VkFormat::R5G6B5_UNORM_PACK16);
        assert_eq!(format.alignment(), 2);

        let format = Format::lookup_by_vk_format(VkFormat::D24_UNORM_S8_UINT);
        assert_eq!(format.alignment(), 3);

        let format = Format::lookup_by_vk_format(VkFormat::R8G8B8_UNORM);
        assert_eq!(format.alignment(), 1);
    }
}

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

//! Contains utilities for accessing values stored in a buffer
//! according to the GLSL layout rules. These are decribed in detail
//! in the OpenGL specs here:
//! https://registry.khronos.org/OpenGL/specs/gl/glspec45.core.pdf#page=159

use crate::util;
use crate::tolerance::Tolerance;
use crate::half_float;
use std::mem;
use std::convert::TryInto;
use std::ffi::c_void;

/// Describes which layout standard is being used. The only difference
/// is that with [Std140] the offset between members of an array or
/// between elements of the minor axis of a matrix is rounded up to be
/// a multiple of 16 bytes.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum LayoutStd {
    Std140,
    Std430,
}

/// For matrix types, describes which axis is the major one. The
/// elements of the major axis are stored consecutively in memory. For
/// example, if the numbers indicate the order in memory of the
/// components of a matrix, the two orderings would look like this:
///
/// ```
/// Column major     Row major
/// [0 3 6]          [0 1 2]
/// [1 4 7]          [3 4 5]
/// [2 5 8]          [6 7 8]
/// ```
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum MajorAxis {
    Column,
    Row
}

/// Combines the [MajorAxis] and the [LayoutStd] to provide a complete
/// description of the layout options used for accessing the
/// components of a type.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Layout {
    pub std: LayoutStd,
    pub major: MajorAxis,
}

/// An enum representing all of the types that can be stored in a
/// slot. These correspond to the types in GLSL.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Type {
    Int = 0,
    UInt,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int64,
    UInt64,
    Float16,
    Float,
    Double,
    F16Vec2,
    F16Vec3,
    F16Vec4,
    Vec2,
    Vec3,
    Vec4,
    DVec2,
    DVec3,
    DVec4,
    IVec2,
    IVec3,
    IVec4,
    UVec2,
    UVec3,
    UVec4,
    I8Vec2,
    I8Vec3,
    I8Vec4,
    U8Vec2,
    U8Vec3,
    U8Vec4,
    I16Vec2,
    I16Vec3,
    I16Vec4,
    U16Vec2,
    U16Vec3,
    U16Vec4,
    I64Vec2,
    I64Vec3,
    I64Vec4,
    U64Vec2,
    U64Vec3,
    U64Vec4,
    Mat2,
    Mat2x3,
    Mat2x4,
    Mat3x2,
    Mat3,
    Mat3x4,
    Mat4x2,
    Mat4x3,
    Mat4,
    DMat2,
    DMat2x3,
    DMat2x4,
    DMat3x2,
    DMat3,
    DMat3x4,
    DMat4x2,
    DMat4x3,
    DMat4,
}

/// The type of a component of the types in [Type].
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum BaseType {
    Int,
    UInt,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int64,
    UInt64,
    Float16,
    Float,
    Double,
}

/// A type of comparison that can be used to compare values stored in
/// a slot with the chosen criteria. Use the [compare] method to
/// perform the comparison.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum Comparison {
    /// The comparison passes if the values are exactly equal.
    Equal,
    /// The comparison passes if the floating-point values are
    /// approximately equal using the given [Tolerance] settings. For
    /// integer types this is the same as [Equal].
    FuzzyEqual,
    /// The comparison passes if the values are different.
    NotEqual,
    /// The comparison passes if all of the values in the first slot
    /// are less than the values in the second slot.
    Less,
    /// The comparison passes if all of the values in the first slot
    /// are greater than or equal to the values in the second slot.
    GreaterEqual,
    /// The comparison passes if all of the values in the first slot
    /// are greater than the values in the second slot.
    Greater,
    /// The comparison passes if all of the values in the first slot
    /// are less than or equal to the values in the second slot.
    LessEqual,
}

#[derive(Debug)]
struct TypeInfo {
    base_type: BaseType,
    columns: usize,
    rows: usize,
}

static TYPE_INFOS: [TypeInfo; 62] = [
    TypeInfo { base_type: BaseType::Int, columns: 1, rows: 1 }, // Int
    TypeInfo { base_type: BaseType::UInt, columns: 1, rows: 1 }, // UInt
    TypeInfo { base_type: BaseType::Int8, columns: 1, rows: 1 }, // Int8
    TypeInfo { base_type: BaseType::UInt8, columns: 1, rows: 1 }, // UInt8
    TypeInfo { base_type: BaseType::Int16, columns: 1, rows: 1 }, // Int16
    TypeInfo { base_type: BaseType::UInt16, columns: 1, rows: 1 }, // UInt16
    TypeInfo { base_type: BaseType::Int64, columns: 1, rows: 1 }, // Int64
    TypeInfo { base_type: BaseType::UInt64, columns: 1, rows: 1 }, // UInt64
    TypeInfo { base_type: BaseType::Float16, columns: 1, rows: 1 }, // Float16
    TypeInfo { base_type: BaseType::Float, columns: 1, rows: 1 }, // Float
    TypeInfo { base_type: BaseType::Double, columns: 1, rows: 1 }, // Double
    TypeInfo { base_type: BaseType::Float16, columns: 1, rows: 2 }, // F16Vec2
    TypeInfo { base_type: BaseType::Float16, columns: 1, rows: 3 }, // F16Vec3
    TypeInfo { base_type: BaseType::Float16, columns: 1, rows: 4 }, // F16Vec4
    TypeInfo { base_type: BaseType::Float, columns: 1, rows: 2 }, // Vec2
    TypeInfo { base_type: BaseType::Float, columns: 1, rows: 3 }, // Vec3
    TypeInfo { base_type: BaseType::Float, columns: 1, rows: 4 }, // Vec4
    TypeInfo { base_type: BaseType::Double, columns: 1, rows: 2 }, // DVec2
    TypeInfo { base_type: BaseType::Double, columns: 1, rows: 3 }, // DVec3
    TypeInfo { base_type: BaseType::Double, columns: 1, rows: 4 }, // DVec4
    TypeInfo { base_type: BaseType::Int, columns: 1, rows: 2 }, // IVec2
    TypeInfo { base_type: BaseType::Int, columns: 1, rows: 3 }, // IVec3
    TypeInfo { base_type: BaseType::Int, columns: 1, rows: 4 }, // IVec4
    TypeInfo { base_type: BaseType::UInt, columns: 1, rows: 2 }, // UVec2
    TypeInfo { base_type: BaseType::UInt, columns: 1, rows: 3 }, // UVec3
    TypeInfo { base_type: BaseType::UInt, columns: 1, rows: 4 }, // UVec4
    TypeInfo { base_type: BaseType::Int8, columns: 1, rows: 2 }, // I8Vec2
    TypeInfo { base_type: BaseType::Int8, columns: 1, rows: 3 }, // I8Vec3
    TypeInfo { base_type: BaseType::Int8, columns: 1, rows: 4 }, // I8Vec4
    TypeInfo { base_type: BaseType::UInt8, columns: 1, rows: 2 }, // U8Vec2
    TypeInfo { base_type: BaseType::UInt8, columns: 1, rows: 3 }, // U8Vec3
    TypeInfo { base_type: BaseType::UInt8, columns: 1, rows: 4 }, // U8Vec4
    TypeInfo { base_type: BaseType::Int16, columns: 1, rows: 2 }, // I16Vec2
    TypeInfo { base_type: BaseType::Int16, columns: 1, rows: 3 }, // I16Vec3
    TypeInfo { base_type: BaseType::Int16, columns: 1, rows: 4 }, // I16Vec4
    TypeInfo { base_type: BaseType::UInt16, columns: 1, rows: 2 }, // U16Vec2
    TypeInfo { base_type: BaseType::UInt16, columns: 1, rows: 3 }, // U16Vec3
    TypeInfo { base_type: BaseType::UInt16, columns: 1, rows: 4 }, // U16Vec4
    TypeInfo { base_type: BaseType::Int64, columns: 1, rows: 2 }, // I64Vec2
    TypeInfo { base_type: BaseType::Int64, columns: 1, rows: 3 }, // I64Vec3
    TypeInfo { base_type: BaseType::Int64, columns: 1, rows: 4 }, // I64Vec4
    TypeInfo { base_type: BaseType::UInt64, columns: 1, rows: 2 }, // U64Vec2
    TypeInfo { base_type: BaseType::UInt64, columns: 1, rows: 3 }, // U64Vec3
    TypeInfo { base_type: BaseType::UInt64, columns: 1, rows: 4 }, // U64Vec4
    TypeInfo { base_type: BaseType::Float, columns: 2, rows: 2 }, // Mat2
    TypeInfo { base_type: BaseType::Float, columns: 2, rows: 3 }, // Mat2x3
    TypeInfo { base_type: BaseType::Float, columns: 2, rows: 4 }, // Mat2x4
    TypeInfo { base_type: BaseType::Float, columns: 3, rows: 2 }, // Mat3x2
    TypeInfo { base_type: BaseType::Float, columns: 3, rows: 3 }, // Mat3
    TypeInfo { base_type: BaseType::Float, columns: 3, rows: 4 }, // Mat3x4
    TypeInfo { base_type: BaseType::Float, columns: 4, rows: 2 }, // Mat4x2
    TypeInfo { base_type: BaseType::Float, columns: 4, rows: 3 }, // Mat4x3
    TypeInfo { base_type: BaseType::Float, columns: 4, rows: 4 }, // Mat4
    TypeInfo { base_type: BaseType::Double, columns: 2, rows: 2 }, // DMat2
    TypeInfo { base_type: BaseType::Double, columns: 2, rows: 3 }, // DMat2x3
    TypeInfo { base_type: BaseType::Double, columns: 2, rows: 4 }, // DMat2x4
    TypeInfo { base_type: BaseType::Double, columns: 3, rows: 2 }, // DMat3x2
    TypeInfo { base_type: BaseType::Double, columns: 3, rows: 3 }, // DMat3
    TypeInfo { base_type: BaseType::Double, columns: 3, rows: 4 }, // DMat3x4
    TypeInfo { base_type: BaseType::Double, columns: 4, rows: 2 }, // DMat4x2
    TypeInfo { base_type: BaseType::Double, columns: 4, rows: 3 }, // DMat4x3
    TypeInfo { base_type: BaseType::Double, columns: 4, rows: 4 }, // DMat4
];

// Mapping from GLSL type name to slot type. Sorted alphabetically so
// we can do a binary search.
static GLSL_TYPE_NAMES: [(&'static str, Type); 68] = [
    ("dmat2", Type::DMat2),
    ("dmat2x2", Type::DMat2),
    ("dmat2x3", Type::DMat2x3),
    ("dmat2x4", Type::DMat2x4),
    ("dmat3", Type::DMat3),
    ("dmat3x2", Type::DMat3x2),
    ("dmat3x3", Type::DMat3),
    ("dmat3x4", Type::DMat3x4),
    ("dmat4", Type::DMat4),
    ("dmat4x2", Type::DMat4x2),
    ("dmat4x3", Type::DMat4x3),
    ("dmat4x4", Type::DMat4),
    ("double", Type::Double),
    ("dvec2", Type::DVec2),
    ("dvec3", Type::DVec3),
    ("dvec4", Type::DVec4),
    ("f16vec2", Type::F16Vec2),
    ("f16vec3", Type::F16Vec3),
    ("f16vec4", Type::F16Vec4),
    ("float", Type::Float),
    ("float16_t", Type::Float16),
    ("i16vec2", Type::I16Vec2),
    ("i16vec3", Type::I16Vec3),
    ("i16vec4", Type::I16Vec4),
    ("i64vec2", Type::I64Vec2),
    ("i64vec3", Type::I64Vec3),
    ("i64vec4", Type::I64Vec4),
    ("i8vec2", Type::I8Vec2),
    ("i8vec3", Type::I8Vec3),
    ("i8vec4", Type::I8Vec4),
    ("int", Type::Int),
    ("int16_t", Type::Int16),
    ("int64_t", Type::Int64),
    ("int8_t", Type::Int8),
    ("ivec2", Type::IVec2),
    ("ivec3", Type::IVec3),
    ("ivec4", Type::IVec4),
    ("mat2", Type::Mat2),
    ("mat2x2", Type::Mat2),
    ("mat2x3", Type::Mat2x3),
    ("mat2x4", Type::Mat2x4),
    ("mat3", Type::Mat3),
    ("mat3x2", Type::Mat3x2),
    ("mat3x3", Type::Mat3),
    ("mat3x4", Type::Mat3x4),
    ("mat4", Type::Mat4),
    ("mat4x2", Type::Mat4x2),
    ("mat4x3", Type::Mat4x3),
    ("mat4x4", Type::Mat4),
    ("u16vec2", Type::U16Vec2),
    ("u16vec3", Type::U16Vec3),
    ("u16vec4", Type::U16Vec4),
    ("u64vec2", Type::U64Vec2),
    ("u64vec3", Type::U64Vec3),
    ("u64vec4", Type::U64Vec4),
    ("u8vec2", Type::U8Vec2),
    ("u8vec3", Type::U8Vec3),
    ("u8vec4", Type::U8Vec4),
    ("uint", Type::UInt),
    ("uint16_t", Type::UInt16),
    ("uint64_t", Type::UInt64),
    ("uint8_t", Type::UInt8),
    ("uvec2", Type::UVec2),
    ("uvec3", Type::UVec3),
    ("uvec4", Type::UVec4),
    ("vec2", Type::Vec2),
    ("vec3", Type::Vec3),
    ("vec4", Type::Vec4),
];

impl BaseType {
    /// Returns the size in bytes of a variable of this base type.
    pub fn size(self) -> usize {
        match self {
            BaseType::Int => mem::size_of::<i32>(),
            BaseType::UInt => mem::size_of::<u32>(),
            BaseType::Int8 => mem::size_of::<i8>(),
            BaseType::UInt8 => mem::size_of::<u8>(),
            BaseType::Int16 => mem::size_of::<i16>(),
            BaseType::UInt16 => mem::size_of::<u16>(),
            BaseType::Int64 => mem::size_of::<i64>(),
            BaseType::UInt64 => mem::size_of::<u64>(),
            BaseType::Float16 => mem::size_of::<u16>(),
            BaseType::Float => mem::size_of::<u32>(),
            BaseType::Double => mem::size_of::<u64>(),
        }
    }
}

impl Type {
    /// Returns the type of the components of this slot type.
    #[inline]
    pub fn base_type(self) -> BaseType {
        TYPE_INFOS[self as usize].base_type
    }

    /// Returns the number of rows in this slot type. For primitive types
    /// this will be 1, for vectors it will be the size of the vector
    /// and for matrix types it will be the number of rows.
    #[inline]
    pub fn rows(self) -> usize {
        TYPE_INFOS[self as usize].rows
    }

    /// Return the number of columns in this slot type. For non-matrix
    /// types this will be 1.
    #[inline]
    pub fn columns(self) -> usize {
        TYPE_INFOS[self as usize].columns
    }

    /// Returns whether the type is a matrix type
    #[inline]
    pub fn is_matrix(self) -> bool {
        self.columns() > 1
    }

    // Returns the effective major axis setting that should be used
    // when calculating offsets. The setting should only affect matrix
    // types.
    fn effective_major_axis(self, layout: Layout) -> MajorAxis {
        if self.is_matrix() {
            layout.major
        } else {
            MajorAxis::Column
        }
    }

    // Return a tuple containing the size of the major and minor axes
    // (in that order) given the type and layout
    fn major_minor(self, layout: Layout) -> (usize, usize) {
        match self.effective_major_axis(layout) {
            MajorAxis::Column => (self.columns(), self.rows()),
            MajorAxis::Row => (self.rows(), self.columns()),
        }
    }

    /// Returns the matrix stride of the slot type. This is the offset
    /// in bytes between consecutive components of the minor axis. For
    /// example, in a column-major layout, the matrix stride of a vec4
    /// will be 16, because to move to the next column you need to add
    /// an offset of 16 bytes to the address of the previous column.
    pub fn matrix_stride(self, layout: Layout) -> usize {
        let component_size = self.base_type().size();
        let (_major, minor) = self.major_minor(layout);

        let base_stride = if minor == 3 {
            component_size * 4
        } else {
            component_size * minor
        };

        match layout.std {
            LayoutStd::Std140 => {
                // According to std140 the size is rounded up to a vec4
                util::align(base_stride, 16)
            },
            LayoutStd::Std430 => base_stride,
        }
    }

    /// Returns the offset between members of the elements of an array
    /// of this type of slot. There may be padding between the members
    /// to fulfill the alignment criteria of the layout.
    pub fn array_stride(self, layout: Layout) -> usize {
        let matrix_stride = self.matrix_stride(layout);
        let (major, _minor) = self.major_minor(layout);

        matrix_stride * major
    }

    /// Returns the size of the slot type. Note that unlike
    /// [array_stride], this won’t include the padding to align the
    /// values in an array according to the GLSL layout rules. It will
    /// however always have a size that is a multiple of the size of
    /// the base type so it is suitable as an array stride for packed
    /// values stored internally that aren’t passed to Vulkan.
    pub fn size(self, layout: Layout) -> usize {
        let matrix_stride = self.matrix_stride(layout);
        let base_size = self.base_type().size();
        let (major, minor) = self.major_minor(layout);

        (major - 1) * matrix_stride + base_size * minor
    }

    /// Returns an iterator that iterates over the offsets of the
    /// components of this slot. This can be used to extract the
    /// values out of an array of bytes containing the slot. The
    /// offsets are returned in column-major order (ie, all of the
    /// offsets for a row are returned before moving to the next
    /// column), but the offsets are calculated to take into account
    /// the major axis setting in the layout.
    pub fn offsets(self, layout: Layout) -> OffsetsIter {
        OffsetsIter::new(self, layout)
    }

    /// Returns a [Type] given the GLSL type name, for example `dmat2`
    /// or `i64vec3`. `None` will be returned if the type name isn’t
    /// recognised.
    pub fn from_glsl_type(type_name: &str) -> Option<Type> {
        match GLSL_TYPE_NAMES.binary_search_by(
            |&(name, _)| name.cmp(type_name)
        ) {
            Ok(pos) => Some(GLSL_TYPE_NAMES[pos].1),
            Err(_) => None,
        }
    }
}

/// Iterator over the offsets into an array to extract the components
/// of a slot. Created by [Slot::offsets].
#[derive(Debug, Clone)]
pub struct OffsetsIter {
    slot_type: Type,
    row: usize,
    column: usize,
    offset: usize,
    column_offset: isize,
    row_offset: isize,
}

impl OffsetsIter {
    fn new(slot_type: Type, layout: Layout) -> OffsetsIter {
        let column_offset;
        let row_offset;
        let stride = slot_type.matrix_stride(layout);
        let base_size = slot_type.base_type().size();

        match slot_type.effective_major_axis(layout) {
            MajorAxis::Column => {
                column_offset =
                    (stride - base_size * slot_type.rows()) as isize;
                row_offset = base_size as isize;
            },
            MajorAxis::Row => {
                column_offset =
                    base_size as isize - (stride * slot_type.rows()) as isize;
                row_offset = stride as isize;
            },
        }

        OffsetsIter {
            slot_type,
            row: 0,
            column: 0,
            offset: 0,
            column_offset,
            row_offset,
        }
    }
}

impl Iterator for OffsetsIter {
    type Item = usize;

    fn next(&mut self) -> Option<usize> {
        if self.column >= self.slot_type.columns() {
            return None;
        }

        let result = self.offset;

        self.offset = ((self.offset as isize) + self.row_offset) as usize;
        self.row += 1;

        if self.row >= self.slot_type.rows() {
            self.row = 0;
            self.column += 1;
            self.offset =
                ((self.offset as isize) + self.column_offset) as usize;
        }

        Some(result)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let n_columns = self.slot_type.columns();

        let size = if self.column >= n_columns {
            0
        } else {
            (self.slot_type.rows() - self.row)
                + ((n_columns - 1 - self.column) * self.slot_type.rows())
        };

        (size, Some(size))
    }

    fn count(self) -> usize {
        self.size_hint().0
    }
}

impl Comparison {
    fn compare_without_fuzzy<T: PartialOrd>(self, a: T, b: T) -> bool {
        match self {
            Comparison::Equal | Comparison::FuzzyEqual => a.eq(&b),
            Comparison::NotEqual => a.ne(&b),
            Comparison::Less => a.lt(&b),
            Comparison::GreaterEqual => a.ge(&b),
            Comparison::Greater => a.gt(&b),
            Comparison::LessEqual => a.le(&b),
        }
    }

    fn compare_value(
        self,
        tolerance: &Tolerance,
        component: usize,
        base_type: BaseType,
        a: &[u8],
        b: &[u8]
    ) -> bool {
        match base_type {
            BaseType::Int => {
                let a = i32::from_ne_bytes(a.try_into().unwrap());
                let b = i32::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::UInt => {
                let a = u32::from_ne_bytes(a.try_into().unwrap());
                let b = u32::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::Int8 => {
                let a = i8::from_ne_bytes(a.try_into().unwrap());
                let b = i8::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::UInt8 => {
                let a = u8::from_ne_bytes(a.try_into().unwrap());
                let b = u8::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::Int16 => {
                let a = i16::from_ne_bytes(a.try_into().unwrap());
                let b = i16::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::UInt16 => {
                let a = u16::from_ne_bytes(a.try_into().unwrap());
                let b = u16::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::Int64 => {
                let a = i64::from_ne_bytes(a.try_into().unwrap());
                let b = i64::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::UInt64 => {
                let a = u64::from_ne_bytes(a.try_into().unwrap());
                let b = u64::from_ne_bytes(b.try_into().unwrap());
                self.compare_without_fuzzy(a, b)
            },
            BaseType::Float16 => {
                let a = u16::from_ne_bytes(a.try_into().unwrap());
                let a = half_float::to_f64(a);
                let b = u16::from_ne_bytes(b.try_into().unwrap());
                let b = half_float::to_f64(b);

                match self {
                    Comparison::FuzzyEqual => tolerance.equal(component, a, b),
                    Comparison::Equal
                        | Comparison::NotEqual
                        | Comparison::Less
                        | Comparison::GreaterEqual
                        | Comparison::Greater
                        | Comparison::LessEqual => {
                            self.compare_without_fuzzy(a, b)
                        },
                }
            },
            BaseType::Float => {
                let a = f32::from_ne_bytes(a.try_into().unwrap());
                let b = f32::from_ne_bytes(b.try_into().unwrap());

                match self {
                    Comparison::FuzzyEqual => {
                        tolerance.equal(component, a as f64, b as f64)
                    },
                    Comparison::Equal
                        | Comparison::NotEqual
                        | Comparison::Less
                        | Comparison::GreaterEqual
                        | Comparison::Greater
                        | Comparison::LessEqual => {
                            self.compare_without_fuzzy(a, b)
                        },
                }
            },
            BaseType::Double => {
                let a = f64::from_ne_bytes(a.try_into().unwrap());
                let b = f64::from_ne_bytes(b.try_into().unwrap());

                match self {
                    Comparison::FuzzyEqual => {
                        tolerance.equal(component, a, b)
                    },
                    Comparison::Equal
                        | Comparison::NotEqual
                        | Comparison::Less
                        | Comparison::GreaterEqual
                        | Comparison::Greater
                        | Comparison::LessEqual => {
                            self.compare_without_fuzzy(a, b)
                        },
                }
            },
        }
    }

    /// Compares the values in [a] with the values in [b] using the
    /// chosen criteria. The values are extracted from the two byte
    /// slices using the layout and type provided. The tolerance
    /// parameter is only used if the comparison is
    /// [Comparison::FuzzyEqual].
    pub fn compare(
        self,
        tolerance: &Tolerance,
        slot_type: Type,
        layout: Layout,
        a: &[u8],
        b: &[u8],
    ) -> bool {
        let base_type = slot_type.base_type();
        let base_type_size = base_type.size();
        let rows = slot_type.rows();

        for (i, offset) in slot_type.offsets(layout).enumerate() {
            let a = &a[offset..offset + base_type_size];
            let b = &b[offset..offset + base_type_size];

            if !self.compare_value(tolerance, i % rows, base_type, a, b) {
                return false;
            }
        }

        true
    }
}

#[no_mangle]
pub extern "C" fn vr_box_for_each_component(
    slot_type: Type,
    layout: &Layout,
    cb: extern "C" fn(
        base_type: BaseType,
        offset: usize,
        user_data: *mut c_void,
    ) -> u8,
    user_data: *mut c_void,
) {
    let base_type = slot_type.base_type();

    for offset in slot_type.offsets(*layout) {
        let ret = cb(base_type, offset, user_data);

        if ret == 0 {
            break;
        }
    }
}

#[no_mangle]
pub extern "C" fn vr_box_compare(
    comparison: Comparison,
    tolerance: &Tolerance,
    slot_type: Type,
    layout: &Layout,
    a: *const u8,
    b: *const u8,
) -> u8 {
    let size = slot_type.size(*layout);
    let a = unsafe { std::slice::from_raw_parts(a, size) };
    let b = unsafe { std::slice::from_raw_parts(b, size) };

    comparison.compare(tolerance, slot_type, *layout, a, b) as u8
}

#[no_mangle]
pub extern "C" fn vr_box_type_array_stride(
    slot_type: Type,
    layout: &Layout
) -> usize {
    slot_type.array_stride(*layout)
}

#[no_mangle]
pub extern "C" fn vr_box_type_size(
    slot_type: Type,
    layout: &Layout
) -> usize {
    slot_type.size(*layout)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_type_info() {
        // Test a few types

        // First
        assert!(matches!(Type::Int.base_type(), BaseType::Int));
        assert_eq!(Type::Int.columns(), 1);
        assert_eq!(Type::Int.rows(), 1);
        assert!(!Type::Int.is_matrix());

        // Last
        assert!(matches!(Type::DMat4.base_type(), BaseType::Double));
        assert_eq!(Type::DMat4.columns(), 4);
        assert_eq!(Type::DMat4.rows(), 4);
        assert!(Type::DMat4.is_matrix());

        // Vector
        assert!(matches!(Type::Vec4.base_type(), BaseType::Float));
        assert_eq!(Type::Vec4.columns(), 1);
        assert_eq!(Type::Vec4.rows(), 4);
        assert!(!Type::Vec4.is_matrix());

        // Non-square matrix
        assert!(matches!(Type::DMat4x3.base_type(), BaseType::Double));
        assert_eq!(Type::DMat4x3.columns(), 4);
        assert_eq!(Type::DMat4x3.rows(), 3);
        assert!(Type::DMat4x3.is_matrix());
    }

    #[test]
    fn test_base_type_size() {
        assert_eq!(BaseType::Int.size(), 4);
        assert_eq!(BaseType::UInt.size(), 4);
        assert_eq!(BaseType::Int8.size(), 1);
        assert_eq!(BaseType::UInt8.size(), 1);
        assert_eq!(BaseType::Int16.size(), 2);
        assert_eq!(BaseType::UInt16.size(), 2);
        assert_eq!(BaseType::Int64.size(), 8);
        assert_eq!(BaseType::UInt64.size(), 8);
        assert_eq!(BaseType::Float16.size(), 2);
        assert_eq!(BaseType::Float.size(), 4);
        assert_eq!(BaseType::Double.size(), 8);
    }

    #[test]
    fn test_matrix_stride() {
        assert_eq!(
            Type::Mat4.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            16,
        );
        // In Std140 the vecs along the minor axis are aligned to the
        // size of a vec4
        assert_eq!(
            Type::Mat4x2.matrix_stride(
                Layout { std: LayoutStd::Std140, major: MajorAxis::Column }
                ),
            16,
        );
        // In Std430 they are not
        assert_eq!(
            Type::Mat4x2.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            8,
        );
        // 3-component axis are always aligned to a vec4 regardless of
        // the layout standard
        assert_eq!(
            Type::Mat4x3.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            16,
        );

        // For the row-major the stride the axes are reversed
        assert_eq!(
            Type::Mat4x2.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Row }
                ),
            16,
        );
        assert_eq!(
            Type::Mat2x4.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Row }
                ),
            8,
        );

        // For non-matrix types the matrix stride is the array stride
        assert_eq!(
            Type::Vec3.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            16,
        );
        assert_eq!(
            Type::Float.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            4,
        );
        // Row-major layout does not affect vectors
        assert_eq!(
            Type::Vec2.matrix_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Row }
                ),
            8,
        );
    }

    #[test]
    fn test_array_stride() {
        assert_eq!(
            Type::UInt8.array_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            1,
        );
        assert_eq!(
            Type::I8Vec2.array_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            2,
        );
        assert_eq!(
            Type::I8Vec3.array_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            4,
        );
        assert_eq!(
            Type::Mat4x3.array_stride(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            64,
        );
    }

    #[test]
    fn test_size() {
        assert_eq!(
            Type::UInt8.size(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            1,
        );
        assert_eq!(
            Type::I8Vec2.size(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            2,
        );
        assert_eq!(
            Type::I8Vec3.size(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            3,
        );
        assert_eq!(
            Type::Mat4x3.size(
                Layout { std: LayoutStd::Std430, major: MajorAxis::Column }
                ),
            3 * 4 * 4 + 3 * 4,
        );
    }

    #[test]
    fn test_iterator() {
        let default_layout = Layout {
            std: LayoutStd::Std140,
            major: MajorAxis::Column,
        };

        // The components of a vec4 are packed tightly
        let vec4_offsets =
            Type::Vec4.offsets(default_layout).collect::<Vec<usize>>();
        assert_eq!(vec4_offsets, vec![0, 4, 8, 12]);

        // The major axis setting shouldn’t affect vectors
        let row_major_layout = Layout {
            std: LayoutStd::Std140,
            major: MajorAxis::Row,
        };
        let vec4_offsets =
            Type::Vec4.offsets(row_major_layout).collect::<Vec<usize>>();
        assert_eq!(vec4_offsets, vec![0, 4, 8, 12]);

        // Components of a mat4 are packed tightly
        let mat4_offsets =
            Type::Mat4.offsets(default_layout).collect::<Vec<usize>>();
        assert_eq!(
            mat4_offsets,
            vec![0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60],
        );

        // If there are 3 rows then they are padded out to align
        // each column to the size of a vec4
        let mat4x3_offsets =
            Type::Mat4x3.offsets(default_layout).collect::<Vec<usize>>();
        assert_eq!(
            mat4x3_offsets,
            vec![0, 4, 8, 16, 20, 24, 32, 36, 40, 48, 52, 56],
        );

        // If row-major order is being used then the offsets are still
        // reported in column-major order but the addresses that they
        // point to are in row-major order.
        // [0   4   8]
        // [16  20  24]
        // [32  36  40]
        // [48  52  56]
        let mat3x4_offsets =
            Type::Mat3x4.offsets(row_major_layout).collect::<Vec<usize>>();
        assert_eq!(
            mat3x4_offsets,
            vec![0, 16, 32, 48, 4, 20, 36, 52, 8, 24, 40, 56],
        );

        // Check the size_hint and count. The size hint is always
        // exact and should match the count.
        let mut iter = Type::Mat4.offsets(default_layout);

        for i in 0..16 {
            let expected_count = 16 - i;
            assert_eq!(
                iter.size_hint(),
                (expected_count, Some(expected_count))
            );
            assert_eq!(iter.clone().count(), expected_count);

            assert!(matches!(iter.next(), Some(_)));
        }

        assert_eq!(iter.size_hint(), (0, Some(0)));
        assert_eq!(iter.clone().count(), 0);
        assert!(matches!(iter.next(), None));
    }

    #[test]
    fn test_compare() {
        // Test every comparison mode
        assert!(Comparison::Equal.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(!Comparison::Equal.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[6],
            &[5],
        ));
        assert!(Comparison::NotEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[6],
            &[5],
        ));
        assert!(!Comparison::NotEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(Comparison::Less.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[4],
            &[5],
        ));
        assert!(!Comparison::Less.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(!Comparison::Less.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[6],
            &[5],
        ));
        assert!(Comparison::LessEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[4],
            &[5],
        ));
        assert!(Comparison::LessEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(!Comparison::LessEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[6],
            &[5],
        ));
        assert!(Comparison::Greater.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[4],
        ));
        assert!(!Comparison::Greater.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(!Comparison::Greater.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[6],
        ));
        assert!(Comparison::GreaterEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[4],
        ));
        assert!(Comparison::GreaterEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[5],
        ));
        assert!(!Comparison::GreaterEqual.compare(
            &Default::default(), // tolerance
            Type::UInt8,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5],
            &[6],
        ));
        assert!(Comparison::FuzzyEqual.compare(
            &Tolerance::new([1.0; 4], false),
            Type::Float,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &5.0f32.to_ne_bytes(),
            &5.9f32.to_ne_bytes(),
        ));
        assert!(!Comparison::FuzzyEqual.compare(
            &Tolerance::new([1.0; 4], false),
            Type::Float,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &5.0f32.to_ne_bytes(),
            &6.1f32.to_ne_bytes(),
        ));

        // Test all types

        macro_rules! test_compare_type_with_values {
            ($type_enum:expr, $low_value:expr, $high_value:expr) => {
                assert!(Comparison::Less.compare(
                    &Default::default(),
                    $type_enum,
                    Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
                    &$low_value.to_ne_bytes(),
                    &$high_value.to_ne_bytes(),
                ));
            };
        }

        macro_rules! test_compare_simple_type {
            ($type_num:expr, $rust_type:ty) => {
                test_compare_type_with_values!(
                    $type_num,
                    0 as $rust_type,
                    <$rust_type>::MAX
                );
            };
        }

        test_compare_simple_type!(Type::Int, i32);
        test_compare_simple_type!(Type::UInt, u32);
        test_compare_simple_type!(Type::Int8, i8);
        test_compare_simple_type!(Type::UInt8, u8);
        test_compare_simple_type!(Type::Int16, i16);
        test_compare_simple_type!(Type::UInt16, u16);
        test_compare_simple_type!(Type::Int64, i64);
        test_compare_simple_type!(Type::UInt64, u64);
        test_compare_simple_type!(Type::Float, f32);
        test_compare_simple_type!(Type::Double, f64);
        test_compare_type_with_values!(
            Type::Float16,
            half_float::from_f32(-100.0),
            half_float::from_f32(300.0)
        );

        macro_rules! test_compare_big_type {
            ($type_enum:expr, $bytes:expr) => {
                let layout = Layout {
                    std: LayoutStd::Std140,
                    major: MajorAxis::Column
                };
                let mut bytes = Vec::new();
                assert_eq!($type_enum.size(layout) % $bytes.len(), 0);
                for _ in 0..($type_enum.size(layout) / $bytes.len()) {
                    bytes.extend_from_slice($bytes);
                }
                assert!(Comparison::FuzzyEqual.compare(
                    &Default::default(),
                    $type_enum,
                    layout,
                    &bytes,
                    &bytes,
                ));
            };
        }

        test_compare_big_type!(
            Type::F16Vec2,
            &half_float::from_f32(1.0).to_ne_bytes()
        );
        test_compare_big_type!(
            Type::F16Vec3,
            &half_float::from_f32(1.0).to_ne_bytes()
        );
        test_compare_big_type!(
            Type::F16Vec4,
            &half_float::from_f32(1.0).to_ne_bytes()
        );

        test_compare_big_type!(Type::Vec2, &42.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Vec3, &24.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Vec4, &6.0f32.to_ne_bytes());
        test_compare_big_type!(Type::DVec2, &42.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DVec3, &24.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DVec4, &6.0f64.to_ne_bytes());
        test_compare_big_type!(Type::IVec2, &(-42i32).to_ne_bytes());
        test_compare_big_type!(Type::IVec3, &(-24i32).to_ne_bytes());
        test_compare_big_type!(Type::IVec4, &(-6i32).to_ne_bytes());
        test_compare_big_type!(Type::UVec2, &42u32.to_ne_bytes());
        test_compare_big_type!(Type::UVec3, &24u32.to_ne_bytes());
        test_compare_big_type!(Type::UVec4, &6u32.to_ne_bytes());
        test_compare_big_type!(Type::I8Vec2, &(-42i8).to_ne_bytes());
        test_compare_big_type!(Type::I8Vec3, &(-24i8).to_ne_bytes());
        test_compare_big_type!(Type::I8Vec4, &(-6i8).to_ne_bytes());
        test_compare_big_type!(Type::U8Vec2, &42u8.to_ne_bytes());
        test_compare_big_type!(Type::U8Vec3, &24u8.to_ne_bytes());
        test_compare_big_type!(Type::U8Vec4, &6u8.to_ne_bytes());
        test_compare_big_type!(Type::I16Vec2, &(-42i16).to_ne_bytes());
        test_compare_big_type!(Type::I16Vec3, &(-24i16).to_ne_bytes());
        test_compare_big_type!(Type::I16Vec4, &(-6i16).to_ne_bytes());
        test_compare_big_type!(Type::U16Vec2, &42u16.to_ne_bytes());
        test_compare_big_type!(Type::U16Vec3, &24u16.to_ne_bytes());
        test_compare_big_type!(Type::U16Vec4, &6u16.to_ne_bytes());
        test_compare_big_type!(Type::I64Vec2, &(-42i64).to_ne_bytes());
        test_compare_big_type!(Type::I64Vec3, &(-24i64).to_ne_bytes());
        test_compare_big_type!(Type::I64Vec4, &(-6i64).to_ne_bytes());
        test_compare_big_type!(Type::U64Vec2, &42u64.to_ne_bytes());
        test_compare_big_type!(Type::U64Vec3, &24u64.to_ne_bytes());
        test_compare_big_type!(Type::U64Vec4, &6u64.to_ne_bytes());
        test_compare_big_type!(Type::Mat2, &42.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat2x3, &42.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat2x4, &42.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat3, &24.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat3x2, &24.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat3x4, &24.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat4, &6.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat4x2, &6.0f32.to_ne_bytes());
        test_compare_big_type!(Type::Mat4x3, &6.0f32.to_ne_bytes());
        test_compare_big_type!(Type::DMat2, &42.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat2x3, &42.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat2x4, &42.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat3, &24.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat3x2, &24.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat3x4, &24.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat4, &6.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat4x2, &6.0f64.to_ne_bytes());
        test_compare_big_type!(Type::DMat4x3, &6.0f64.to_ne_bytes());

        // Check that it the comparison fails if a value other than
        // the first one is different
        assert!(!Comparison::Equal.compare(
            &Default::default(), // tolerance
            Type::U8Vec4,
            Layout { std: LayoutStd::Std140, major: MajorAxis::Column },
            &[5, 6, 7, 8],
            &[5, 6, 7, 9],
        ));
    }

    #[test]
    fn test_from_glsl_type() {
        assert_eq!(Type::from_glsl_type("vec3"), Some(Type::Vec3));
        assert_eq!(Type::from_glsl_type("dvec4"), Some(Type::DVec4));
        assert_eq!(Type::from_glsl_type("i64vec4"), Some(Type::I64Vec4));
        assert_eq!(Type::from_glsl_type("uint"), Some(Type::UInt));
        assert_eq!(Type::from_glsl_type("uint16_t"), Some(Type::UInt16));
        assert_eq!(Type::from_glsl_type("dvec5"), None);

        // Check that all types can be found with the binary search
        for &(type_name, slot_type) in GLSL_TYPE_NAMES.iter() {
            assert_eq!(Type::from_glsl_type(type_name), Some(slot_type));
        }
    }
}

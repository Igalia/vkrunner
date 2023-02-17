// vkrunner
//
// Copyright (C) 2019 Intel Corporation
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

use crate::vk;
use crate::vulkan_funcs;
use crate::vulkan_funcs::{NEXT_PTR_OFFSET, FIRST_FEATURE_OFFSET};
use std::mem;
use std::collections::{HashMap, HashSet};
use std::ffi::CStr;
use std::convert::TryInto;
use std::fmt;
use std::cell::UnsafeCell;

#[derive(Debug)]
struct Extension {
    // The name of the extension that provides this set of features
    // stored as a null-terminated byte sequence. It is stored this
    // way because that is how bindgen generates string literals from
    // headers.
    name_bytes: &'static [u8],
    // The size of the corresponding features struct
    struct_size: usize,
    // The enum for this struct
    struct_type: vk::VkStructureType,
    // List of feature names in this extension in the order they
    // appear in the features struct. The names are as written in the
    // struct definition.
    features: &'static [&'static str],
}

impl Extension {
    fn name(&self) -> &str {
        // The name comes from static data so it should always be
        // valid
        CStr::from_bytes_with_nul(self.name_bytes)
            .unwrap()
            .to_str()
            .unwrap()
    }
}

include!("features.rs");

// Lazily generated extensions data used for passing to Vulkan
struct LazyExtensions {
    // All of the null-terminated strings concatenated into a buffer
    strings: Vec<u8>,
    // Pointers to the start of each string
    pointers: Vec<* const u8>,
}

// Lazily generated structures data used for passing to Vulkan
struct LazyStructures {
    // A buffer used to return a linked chain of structs that can be
    // accessed from C.
    list: Vec<u8>,
}

// Lazily generated VkPhysicalDeviceFeatures struct to pass to Vulkan
struct LazyBaseFeatures {
    // Array big enough to store a VkPhysicalDeciveFeatures struct.
    // It’s easier to manipulate as an array instead of the actual
    // struct because we want to update the bools by index.
    bytes: [u8; mem::size_of::<vk::VkPhysicalDeviceFeatures>()],
}

#[derive(Debug)]
pub struct Requirements {
    // Minimum vulkan version
    version: u32,
    // Set of extension names required
    extensions: HashSet<String>,
    // A map indexed by extension number. The value is an array of
    // bools representing whether we need each feature in the
    // extension’s feature struct.
    features: HashMap<usize, Box<[bool]>>,
    // An array of bools corresponding to each feature in the base
    // VkPhysicalDeviceFeatures struct
    base_features: [bool; N_BASE_FEATURES],

    // The rest of the struct is lazily created from the above data
    // and shouldn’t be part of the PartialEq implementation.

    lazy_extensions: UnsafeCell<Option<LazyExtensions>>,
    lazy_structures: UnsafeCell<Option<LazyStructures>>,
    lazy_base_features: UnsafeCell<Option<LazyBaseFeatures>>,
}

/// Error returned by [Requirements::check]
#[derive(Debug)]
pub enum CheckError<'a> {
    /// The driver returned invalid data. The string explains the error.
    Invalid(String),
    /// A required base feature from VkPhysicalDeviceFeatures is missing.
    MissingBaseFeature(usize),
    /// A required extension is missing. The string slice is the name
    /// of the extension.
    MissingExtension(&'a str),
    /// A required feature is missing.
    MissingFeature { extension: usize, feature: usize },
    /// The API version reported by the driver is too low
    VersionTooLow { required_version: u32, actual_version: u32 },
}

impl<'a> fmt::Display for CheckError<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            CheckError::Invalid(s) => write!(f, "{}", s),
            &CheckError::MissingBaseFeature(feature_num) => {
                write!(
                    f,
                    "Missing required feature: {}",
                    BASE_FEATURES[feature_num],
                )
            },
            &CheckError::MissingExtension(s) => {
                write!(f, "Missing required extension: {}", s)
            },
            &CheckError::MissingFeature { extension, feature } => {
                write!(
                    f,
                    "Missing required feature “{}” from extension “{}”",
                    EXTENSIONS[extension].features[feature],
                    EXTENSIONS[extension].name(),
                )
            },
            &CheckError::VersionTooLow { required_version, actual_version } => {
                let (req_major, req_minor, req_patch) =
                    extract_version(required_version);
                let (actual_major, actual_minor, actual_patch) =
                    extract_version(actual_version);
                write!(
                    f,
                    "Vulkan API version {}.{}.{} required but the driver \
                     reported {}.{}.{}",
                    req_major, req_minor, req_patch,
                    actual_major, actual_minor, actual_patch,
                )
            }
        }
    }
}

/// Convert a decomposed Vulkan version into an integer. This is the
/// same as the `VK_MAKE_VERSION` macro in the Vulkan headers.
pub const fn make_version(major: u32, minor: u32, patch: u32) -> u32 {
    (major << 22) | (minor << 12) | patch
}

/// Decompose a Vulkan version into its component major, minor and
/// patch parts. This is the equivalent of the `VK_VERSION_MAJOR`,
/// `VK_VERSION_MINOR` and `VK_VERSION_PATCH` macros in the Vulkan
/// header.
pub const fn extract_version(version: u32) -> (u32, u32, u32) {
    (version >> 22, (version >> 12) & 0x3ff, version & 0xfff)
}

impl Requirements {
    pub fn new() -> Requirements {
        Requirements {
            version: make_version(1, 0, 0),
            extensions: HashSet::new(),
            features: HashMap::new(),
            base_features: [false; N_BASE_FEATURES],
            lazy_extensions: UnsafeCell::new(None),
            lazy_structures: UnsafeCell::new(None),
            lazy_base_features: UnsafeCell::new(None),
        }
    }

    /// Get the required Vulkan version that was previously set with
    /// [add_version](Requirements::add_version).
    pub fn version(&self) -> u32 {
        self.version
    }

    /// Set the minimum required Vulkan version.
    pub fn add_version(&mut self, major: u32, minor: u32, patch: u32) {
        self.version = make_version(major, minor, patch);
    }

    fn update_lazy_extensions(&self) -> &[* const u8] {
        // SAFETY: The only case where lazy_extensions will be
        // modified is if it is None and the only way for that to
        // happen is if something modifies the requirements through a
        // mutable reference. If that is the case then at that point
        // there were no other references to the requirements so
        // nothing can be holding a reference to the lazy data.
        let extensions = unsafe {
            match &mut *self.lazy_extensions.get() {
                Some(data) => return data.pointers.as_ref(),
                option @ None => {
                    option.insert(LazyExtensions {
                        strings: Vec::new(),
                        pointers: Vec::new(),
                    })
                },
            }
        };

        // Store a list of offsets into the c_extensions array for
        // each extension. We can’t directly store the pointers yet
        // because the Vec will probably be reallocated while we are
        // adding to it.
        let mut offsets = Vec::<usize>::new();

        for extension in self.extensions.iter() {
            offsets.push(extensions.strings.len());

            extensions.strings.extend_from_slice(extension.as_bytes());
            // Add the null terminator
            extensions.strings.push(0);
        }

        let base_ptr = extensions.strings.as_ptr();

        extensions.pointers.reserve(offsets.len());

        for offset in offsets {
            // SAFETY: These are all valid offsets into the
            // c_extensions Vec so they should all be in the same
            // allocation and no overflow is possible.
            extensions.pointers.push(unsafe { base_ptr.add(offset) });
        }

        extensions.pointers.as_ref()
    }

    /// Return a reference to an array of pointers to C-style strings
    /// that can be passed to `vkCreateDevice`.
    pub fn c_extensions(&self) -> &[* const u8] {
        self.update_lazy_extensions()
    }

    // Make a linked list of features structures that is suitable for
    // passing to VkPhysicalDeviceFeatures2 or vkCreateDevice. All of
    // the bools are set to 0. The vec with the structs is returned as
    // well as a list of offsets to the bools along with the extension
    // number.
    fn make_empty_structures(
        &self
    ) -> (Vec<u8>, Vec<(usize, usize)>)
    {
        let mut structures = Vec::new();

        // Keep an array of offsets and corresponding extension num in a
        // vec for each structure that will be returned. The offset is
        // also used to update the pNext pointers. We have to do this
        // after filling the vec because the vec will probably
        // reallocate while we are adding to it which would invalidate
        // the pointers.
        let mut offsets = Vec::<(usize, usize)>::new();

        for (&extension_num, features) in self.features.iter() {
            let extension = &EXTENSIONS[extension_num];

            // Make sure the struct size is big enough to hold all of
            // the bools
            assert!(
                extension.struct_size
                    >= (FIRST_FEATURE_OFFSET
                        + features.len() * mem::size_of::<vk::VkBool32>())
            );

            let offset = structures.len();
            structures.resize(offset + extension.struct_size, 0);

            // The structure type is the first field in the structure
            let type_end = offset + mem::size_of::<vk::VkStructureType>();
            structures[offset..type_end]
                .copy_from_slice(&extension.struct_type.to_ne_bytes());

            offsets.push((offset + FIRST_FEATURE_OFFSET, extension_num));
        }

        let mut last_offset = 0;

        for &(offset, _) in &offsets[1..] {
            let offset = offset - FIRST_FEATURE_OFFSET;

            // SAFETY: The offsets are all valid offsets into the
            // structures vec so they should all be in the same
            // allocation and no overflow should occur.
            let ptr = unsafe { structures.as_ptr().add(offset) };

            let ptr_start = last_offset + NEXT_PTR_OFFSET;
            let ptr_end = ptr_start + mem::size_of::<*const u8>();
            structures[ptr_start..ptr_end]
                .copy_from_slice(&(ptr as usize).to_ne_bytes());

            last_offset = offset;
        }

        (structures, offsets)
    }

    fn update_lazy_structures(&self) -> &[u8] {
        // SAFETY: The only case where lazy_structures will be
        // modified is if it is None and the only way for that to
        // happen is if something modifies the requirements through a
        // mutable reference. If that is the case then at that point
        // there were no other references to the requirements so
        // nothing can be holding a reference to the lazy data.
        let (structures, offsets) = unsafe {
            match &mut *self.lazy_structures.get() {
                Some(data) => return data.list.as_slice(),
                option @ None => {
                    let (list, offsets) = self.make_empty_structures();

                    (option.insert(LazyStructures { list }), offsets)
                },
            }
        };

        for (offset, extension_num) in offsets {
            let features = &self.features[&extension_num];

            for (feature_num, &feature) in features.iter().enumerate() {
                let feature_start =
                    offset
                    + mem::size_of::<vk::VkBool32>()
                    * feature_num;
                let feature_end =
                    feature_start + mem::size_of::<vk::VkBool32>();

                structures.list[feature_start..feature_end]
                    .copy_from_slice(&(feature as vk::VkBool32).to_ne_bytes());
            }
        }

        structures.list.as_slice()
    }

    /// Return a pointer to a linked list of feature structures that
    /// can be passed to `vkCreateDevice`, or `None` if no feature
    /// structs are required.
    pub fn c_structures(&self) -> Option<&[u8]> {
        if self.features.is_empty() {
            None
        } else {
            Some(self.update_lazy_structures())
        }
    }

    fn update_lazy_base_features(&self) -> &[u8] {
        // SAFETY: The only case where lazy_structures will be
        // modified is if it is None and the only way for that to
        // happen is if something modifies the requirements through a
        // mutable reference. If that is the case then at that point
        // there were no other references to the requirements so
        // nothing can be holding a reference to the lazy data.
        let base_features = unsafe {
            match &mut *self.lazy_base_features.get() {
                Some(data) => return &data.bytes,
                option @ None => {
                    option.insert(LazyBaseFeatures {
                        bytes: [
                            0;
                            mem::size_of::<vk::VkPhysicalDeviceFeatures>()
                        ],
                    })
                },
            }
        };

        for (feature_num, &feature) in self.base_features.iter().enumerate() {
            let feature_start = feature_num * mem::size_of::<vk::VkBool32>();
            let feature_end = feature_start + mem::size_of::<vk::VkBool32>();
            base_features.bytes[feature_start..feature_end]
                .copy_from_slice(&(feature as vk::VkBool32).to_ne_bytes());
        }

        &base_features.bytes
    }

    /// Return a pointer to a `VkPhysicalDeviceFeatures` struct that
    /// can be passed to `vkCreateDevice`.
    pub fn c_base_features(&self) -> &vk::VkPhysicalDeviceFeatures {
        unsafe {
            &*self.update_lazy_base_features().as_ptr().cast()
        }
    }

    fn add_extension_name(&mut self, name: &str) {
        // It would be nice to use get_or_insert_owned here if it
        // becomes stable. It’s probably better not to use
        // HashSet::replace directly because if it’s already in the
        // set then we’ll pointlessly copy the str slice and
        // immediately free it.
        if !self.extensions.contains(name) {
            self.extensions.insert(name.to_owned());
            // SAFETY: self is immutable so there should be no other
            // reference to the lazy data
            unsafe {
                *self.lazy_extensions.get() = None;
            }
        }
    }

    /// Adds a requirement to the list of requirements.
    ///
    /// The name can be either a feature as written in the
    /// corresponding features struct or the name of an extension. If
    /// it is a feature it needs to be either the name of a field in
    /// the `VkPhysicalDeviceFeatures` struct or a field in any of the
    /// features structs of the extensions that vkrunner knows about.
    /// In the latter case the name of the corresponding extension
    /// will be automatically added as a requirement.
    pub fn add(&mut self, name: &str) {
        if let Some((extension_num, feature_num)) = find_feature(name) {
            let extension = &EXTENSIONS[extension_num];

            self.add_extension_name(extension.name());

            let features = self
                .features
                .entry(extension_num)
                .or_insert_with(|| {
                    vec![false; extension.features.len()].into_boxed_slice()
                });

            if !features[feature_num] {
                // SAFETY: self is immutable so there should be no other
                // reference to the lazy data
                unsafe {
                    *self.lazy_structures.get() = None;
                }
                features[feature_num] = true;
            }
        } else if let Some(num) = find_base_feature(name) {
            if !self.base_features[num] {
                self.base_features[num] = true;
                // SAFETY: self is immutable so there should be no other
                // reference to the lazy data
                unsafe {
                    *self.lazy_structures.get() = None;
                }
            }
        } else {
            self.add_extension_name(name);
        }
    }

    fn check_base_features(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<(), CheckError> {
        let mut actual_features: [vk::VkBool32; N_BASE_FEATURES] =
            [0; N_BASE_FEATURES];

        unsafe {
            vkinst.vkGetPhysicalDeviceFeatures.unwrap()(
                device,
                actual_features.as_mut_ptr().cast()
            );
        }

        for (feature_num, &required) in self.base_features.iter().enumerate() {
            if required && actual_features[feature_num] == 0 {
                return Err(CheckError::MissingBaseFeature(feature_num));
            }
        }

        Ok(())
    }

    fn get_device_extensions(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<HashSet::<String>, CheckError> {
        let mut property_count = 0u32;

        let res = unsafe {
            vkinst.vkEnumerateDeviceExtensionProperties.unwrap()(
                device,
                std::ptr::null(), // layerName
                &mut property_count as *mut u32,
                std::ptr::null_mut(), // properties
            )
        };

        if res != vk::VK_SUCCESS {
            return Err(CheckError::Invalid(
                "vkEnumerateDeviceExtensionProperties failed".to_string()
            ));
        }

        let mut extensions = Vec::<vk::VkExtensionProperties>::with_capacity(
            property_count as usize
        );

        unsafe {
            let res = vkinst.vkEnumerateDeviceExtensionProperties.unwrap()(
                device,
                std::ptr::null(), // layerName
                &mut property_count as *mut u32,
                extensions.as_mut_ptr(),
            );

            if res != vk::VK_SUCCESS {
                return Err(CheckError::Invalid(
                    "vkEnumerateDeviceExtensionProperties failed".to_string()
                ));
            }

            // SAFETY: The FFI call to
            // vkEnumerateDeviceExtensionProperties should have filled
            // the extensions array with valid values so we can safely
            // set the length to the capacity we allocated earlier
            extensions.set_len(property_count as usize);
        };

        let mut extensions_set = HashSet::new();

        for extension in extensions.iter() {
            let name = &extension.extensionName;
            // Make sure it has a NULL terminator
            if let None = name.iter().find(|&&b| b == 0) {
                return Err(CheckError::Invalid(
                    "NULL terminator missing in string returned from \
                     vkEnumerateDeviceExtensionProperties".to_string()
                ));
            }
            // SAFETY: we just checked that the array has a null terminator
            let name = unsafe { CStr::from_ptr(name.as_ptr()) };
            let name = match name.to_str() {
                Err(_) => {
                    return Err(CheckError::Invalid(
                        "Invalid UTF-8 in string returned from \
                         vkEnumerateDeviceExtensionProperties".to_string()
                    ));
                },
                Ok(s) => s,
            };
            extensions_set.insert(name.to_owned());
        }

        Ok(extensions_set)
    }

    fn check_extensions(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<(), CheckError> {
        if self.extensions.is_empty() {
            return Ok(());
        }

        let actual_extensions = self.get_device_extensions(vkinst, device)?;

        for extension in self.extensions.iter() {
            if !actual_extensions.contains(extension) {
                return Err(CheckError::MissingExtension(extension));
            }
        }

        Ok(())
    }

    fn check_structures(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<(), CheckError> {
        if self.features.is_empty() {
            return Ok(());
        }

        // If vkGetPhysicalDeviceFeatures2KHR isn’t available then we
        // can probably assume that none of the extensions are
        // available.
        let get_features = match vkinst.vkGetPhysicalDeviceFeatures2KHR {
            None => {
                // Find the first feature and report that as missing
                for (&extension, features) in self.features.iter() {
                    for (feature, &enabled) in features.iter().enumerate() {
                        if enabled {
                            return Err(CheckError::MissingFeature {
                                extension,
                                feature,
                            });
                        }
                    }
                }
                unreachable!("Requirements::features should be empty if no \
                              features are required");
            },
            Some(func) => func,
        };

        let (mut actual_structures, offsets) = self.make_empty_structures();

        let mut features_query = vk::VkPhysicalDeviceFeatures2 {
            sType: vk::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            pNext: actual_structures.as_mut_ptr().cast(),
            features: Default::default(),
        };

        unsafe {
            get_features(
                device,
                &mut features_query as *mut vk::VkPhysicalDeviceFeatures2,
            );
        }

        for (offset, extension_num) in offsets {
            let features = &self.features[&extension_num];

            for (feature_num, &feature) in features.iter().enumerate() {
                if !feature {
                    continue;
                }

                let feature_start =
                    offset
                    + mem::size_of::<vk::VkBool32>()
                    * feature_num;
                let feature_end =
                    feature_start + mem::size_of::<vk::VkBool32>();
                let actual_value = vk::VkBool32::from_ne_bytes(
                    actual_structures[feature_start..feature_end]
                        .try_into()
                        .unwrap()
                );

                if actual_value == 0 {
                    return Err(CheckError::MissingFeature {
                        extension: extension_num,
                        feature: feature_num,
                    });
                }
            }
        }

        Ok(())
    }

    fn check_version(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<(), CheckError> {
        let mut props = vk::VkPhysicalDeviceProperties::default();

        unsafe {
            vkinst.vkGetPhysicalDeviceProperties.unwrap()(
                device,
                &mut props as *mut vk::VkPhysicalDeviceProperties,
            );
        }

        if props.apiVersion < self.version() {
            Err(CheckError::VersionTooLow {
                required_version: self.version(),
                actual_version: props.apiVersion,
            })
        } else {
            Ok(())
        }
    }

    pub fn check(
        &self,
        vkinst: &vulkan_funcs::Instance,
        device: vk::VkPhysicalDevice
    ) -> Result<(), CheckError> {
        self.check_base_features(vkinst, device)?;
        self.check_extensions(vkinst, device)?;
        self.check_structures(vkinst, device)?;
        self.check_version(vkinst, device)?;

        Ok(())
    }
}

// Looks for a feature with the given name. If found it returns the
// index of the extension it was found in and the index of the feature
// name.
fn find_feature(name: &str) -> Option<(usize, usize)> {
    for (extension_num, extension) in EXTENSIONS.iter().enumerate() {
        for (feature_num, &feature) in extension.features.iter().enumerate() {
            if feature == name {
                return Some((extension_num, feature_num));
            }
        }
    }

    None
}

fn find_base_feature(name: &str) -> Option<usize> {
    BASE_FEATURES.iter().position(|&f| f == name)
}

impl PartialEq for Requirements {
    fn eq(&self, other: &Requirements) -> bool {
        self.version == other.version
            && self.extensions == other.extensions
            && self.features == other.features
            && self.base_features == other.base_features
    }
}

impl Eq for Requirements {
}

// Manual implementation of clone to avoid copying the lazy state
impl Clone for Requirements {
    fn clone(&self) -> Requirements {
        Requirements {
            version: self.version,
            extensions: self.extensions.clone(),
            features: self.features.clone(),
            base_features: self.base_features.clone(),
            lazy_extensions: UnsafeCell::new(None),
            lazy_structures: UnsafeCell::new(None),
            lazy_base_features: UnsafeCell::new(None),
        }
    }

    fn clone_from(&mut self, source: &Requirements) {
        self.version = source.version;
        self.extensions.clone_from(&source.extensions);
        self.features.clone_from(&source.features);
        self.base_features.clone_from(&source.base_features);
        // SAFETY: self is immutable so there should be no other
        // reference to the lazy data
        unsafe {
            *self.lazy_extensions.get() = None;
            *self.lazy_structures.get() = None;
            *self.lazy_base_features.get() = None;
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::ffi::{c_char, c_void};
    use crate::fake_vulkan;

    fn get_struct_type(structure: &[u8]) -> vk::VkStructureType {
        let slice = &structure[0..mem::size_of::<vk::VkStructureType>()];
        let mut bytes = [0; mem::size_of::<vk::VkStructureType>()];
        bytes.copy_from_slice(slice);
        vk::VkStructureType::from_ne_bytes(bytes)
    }

    fn get_next_structure(structure: &[u8]) -> &[u8] {
        let slice = &structure[
            NEXT_PTR_OFFSET..NEXT_PTR_OFFSET + mem::size_of::<usize>()
        ];
        let mut bytes = [0; mem::size_of::<usize>()];
        bytes.copy_from_slice(slice);
        let pointer = usize::from_ne_bytes(bytes);
        let offset = pointer - structure.as_ptr() as usize;
        &structure[offset..]
    }

    fn find_bools_for_extension<'a, 'b>(
        mut structures: &'a [u8],
        extension: &'b Extension
    ) -> &'a [vk::VkBool32] {
        while !structures.is_empty() {
            let struct_type = get_struct_type(structures);

            if struct_type == extension.struct_type {
                unsafe {
                    let bools_ptr = structures
                        .as_ptr()
                        .add(FIRST_FEATURE_OFFSET);
                    return std::slice::from_raw_parts(
                        bools_ptr as *const vk::VkBool32,
                        extension.features.len()
                    );
                }
            }

            structures = get_next_structure(structures);
        }

        unreachable!("No structure found for extension “{}”", extension.name());
    }

    unsafe fn extension_in_c_extensions(
        reqs: &mut Requirements,
        ext: &str
    ) -> bool {
        for &p in reqs.c_extensions().iter() {
            if CStr::from_ptr(p as *const c_char).to_str().unwrap() == ext {
                return true;
            }
        }

        false
    }

    #[test]
    fn test_all_features() {
        let mut reqs = Requirements::new();

        for extension in EXTENSIONS.iter() {
            for feature in extension.features.iter() {
                reqs.add(feature);
            }
        }

        for feature in BASE_FEATURES.iter() {
            reqs.add(feature);
        }

        for (extension_num, extension) in EXTENSIONS.iter().enumerate() {
            // All of the extensions should be in the set
            assert!(reqs.extensions.contains(extension.name()));
            // All of the features of every extension should be true
            assert!(reqs.features[&extension_num].iter().all(|&b| b));
        }

        // All of the base features should be enabled
        assert!(reqs.base_features.iter().all(|&b| b));

        let base_features = reqs.c_base_features();

        assert_eq!(base_features.robustBufferAccess, 1);
        assert_eq!(base_features.fullDrawIndexUint32, 1);
        assert_eq!(base_features.imageCubeArray, 1);
        assert_eq!(base_features.independentBlend, 1);
        assert_eq!(base_features.geometryShader, 1);
        assert_eq!(base_features.tessellationShader, 1);
        assert_eq!(base_features.sampleRateShading, 1);
        assert_eq!(base_features.dualSrcBlend, 1);
        assert_eq!(base_features.logicOp, 1);
        assert_eq!(base_features.multiDrawIndirect, 1);
        assert_eq!(base_features.drawIndirectFirstInstance, 1);
        assert_eq!(base_features.depthClamp, 1);
        assert_eq!(base_features.depthBiasClamp, 1);
        assert_eq!(base_features.fillModeNonSolid, 1);
        assert_eq!(base_features.depthBounds, 1);
        assert_eq!(base_features.wideLines, 1);
        assert_eq!(base_features.largePoints, 1);
        assert_eq!(base_features.alphaToOne, 1);
        assert_eq!(base_features.multiViewport, 1);
        assert_eq!(base_features.samplerAnisotropy, 1);
        assert_eq!(base_features.textureCompressionETC2, 1);
        assert_eq!(base_features.textureCompressionASTC_LDR, 1);
        assert_eq!(base_features.textureCompressionBC, 1);
        assert_eq!(base_features.occlusionQueryPrecise, 1);
        assert_eq!(base_features.pipelineStatisticsQuery, 1);
        assert_eq!(base_features.vertexPipelineStoresAndAtomics, 1);
        assert_eq!(base_features.fragmentStoresAndAtomics, 1);
        assert_eq!(base_features.shaderTessellationAndGeometryPointSize, 1);
        assert_eq!(base_features.shaderImageGatherExtended, 1);
        assert_eq!(base_features.shaderStorageImageExtendedFormats, 1);
        assert_eq!(base_features.shaderStorageImageMultisample, 1);
        assert_eq!(base_features.shaderStorageImageReadWithoutFormat, 1);
        assert_eq!(base_features.shaderStorageImageWriteWithoutFormat, 1);
        assert_eq!(base_features.shaderUniformBufferArrayDynamicIndexing, 1);
        assert_eq!(base_features.shaderSampledImageArrayDynamicIndexing, 1);
        assert_eq!(base_features.shaderStorageBufferArrayDynamicIndexing, 1);
        assert_eq!(base_features.shaderStorageImageArrayDynamicIndexing, 1);
        assert_eq!(base_features.shaderClipDistance, 1);
        assert_eq!(base_features.shaderCullDistance, 1);
        assert_eq!(base_features.shaderFloat64, 1);
        assert_eq!(base_features.shaderInt64, 1);
        assert_eq!(base_features.shaderInt16, 1);
        assert_eq!(base_features.shaderResourceResidency, 1);
        assert_eq!(base_features.shaderResourceMinLod, 1);
        assert_eq!(base_features.sparseBinding, 1);
        assert_eq!(base_features.sparseResidencyBuffer, 1);
        assert_eq!(base_features.sparseResidencyImage2D, 1);
        assert_eq!(base_features.sparseResidencyImage3D, 1);
        assert_eq!(base_features.sparseResidency2Samples, 1);
        assert_eq!(base_features.sparseResidency4Samples, 1);
        assert_eq!(base_features.sparseResidency8Samples, 1);
        assert_eq!(base_features.sparseResidency16Samples, 1);
        assert_eq!(base_features.sparseResidencyAliased, 1);
        assert_eq!(base_features.variableMultisampleRate, 1);
        assert_eq!(base_features.inheritedQueries, 1);

        // All of the values should be set in the C structs
        for extension in EXTENSIONS.iter() {
            let structs = reqs.c_structures().unwrap();

            assert!(
                find_bools_for_extension(structs, extension)
                    .iter()
                    .all(|&b| b == 1)
            );

            assert!(unsafe { &*reqs.lazy_structures.get() }.is_some());
        }

        // All of the extensions should be in the c_extensions
        for extension in EXTENSIONS.iter() {
            assert!(unsafe {
                extension_in_c_extensions(&mut reqs, extension.name())
            });
            assert!(unsafe { &*reqs.lazy_extensions.get() }.is_some());
        }

        // Sanity check that a made-up extension isn’t in c_extensions
        assert!(!unsafe {
            extension_in_c_extensions(&mut reqs, "not_a_real_ext")
        });
    }

    #[test]
    fn test_version() {
        let mut reqs = Requirements::new();
        reqs.add_version(2, 1, 5);
        assert_eq!(reqs.version(), 0x801005);
    }

    #[test]
    fn test_empty() {
        let reqs = Requirements::new();

        assert!(unsafe { &*reqs.lazy_extensions.get() }.is_none());
        assert!(unsafe { &*reqs.lazy_structures.get() }.is_none());

        let base_features_ptr = reqs.c_base_features()
            as *const vk::VkPhysicalDeviceFeatures
            as *const vk::VkBool32;

        unsafe {
            let base_features = std::slice::from_raw_parts(
                base_features_ptr,
                N_BASE_FEATURES
            );

            assert!(base_features.iter().all(|&b| b == 0));
        }
    }

    #[test]
    fn test_eq() {
        let mut reqs_a = Requirements::new();
        let mut reqs_b = Requirements::new();

        assert_eq!(reqs_a, reqs_b);

        reqs_a.add("advancedBlendCoherentOperations");
        assert_ne!(reqs_a, reqs_b);

        reqs_b.add("advancedBlendCoherentOperations");
        assert_eq!(reqs_a, reqs_b);

        // Getting the C data shouldn’t affect the equality
        reqs_a.c_structures();
        reqs_a.c_base_features();
        reqs_a.c_extensions();

        assert_eq!(reqs_a, reqs_b);

        // The order of adding shouldn’t matter
        reqs_a.add("fake_extension");
        reqs_a.add("another_fake_extension");

        reqs_b.add("another_fake_extension");
        reqs_b.add("fake_extension");

        assert_eq!(reqs_a, reqs_b);

        reqs_a.add("wideLines");
        assert_ne!(reqs_a, reqs_b);

        reqs_b.add("wideLines");
        assert_eq!(reqs_a, reqs_b);

        reqs_a.add_version(3, 1, 2);
        assert_ne!(reqs_a, reqs_b);

        reqs_b.add_version(3, 1, 2);
        assert_eq!(reqs_a, reqs_b);
    }

    #[test]
    fn test_clone() {
        let mut reqs = Requirements::new();

        reqs.add("wideLines");
        reqs.add("fake_extension");
        reqs.add("advancedBlendCoherentOperations");
        assert_eq!(reqs.clone(), reqs);

        assert!(unsafe { &*reqs.lazy_structures.get() }.is_none());
        assert_eq!(reqs.c_extensions().len(), 2);
        assert_eq!(reqs.c_base_features().wideLines, 1);

        let empty = Requirements::new();

        reqs.clone_from(&empty);

        assert!(unsafe { &*reqs.lazy_structures.get() }.is_none());
        assert_eq!(reqs.c_extensions().len(), 0);
        assert_eq!(reqs.c_base_features().wideLines, 0);

        assert_eq!(reqs, empty);
    }

    struct FakeVulkanData {
        fake_vulkan: Box<fake_vulkan::FakeVulkan>,
        _vklib: vulkan_funcs::Library,
        vkinst: vulkan_funcs::Instance,
        instance: vk::VkInstance,
        device: vk::VkPhysicalDevice,
    }

    impl FakeVulkanData {
        fn new() -> FakeVulkanData {
            let mut fake_vulkan = fake_vulkan::FakeVulkan::new();

            fake_vulkan.physical_devices.push(Default::default());

            fake_vulkan.set_override();
            let vklib = vulkan_funcs::Library::new().unwrap();

            let mut instance = std::ptr::null_mut();

            unsafe {
                let res = vklib.vkCreateInstance.unwrap()(
                    std::ptr::null(), // pCreateInfo
                    std::ptr::null(), // pAllocator
                    std::ptr::addr_of_mut!(instance),
                );
                assert_eq!(res, vk::VK_SUCCESS);
            }

            extern "C" fn get_instance_proc(
                func_name: *const c_char,
                user_data: *const c_void,
            ) -> *const c_void {
                unsafe {
                    let fake_vulkan =
                        &*user_data.cast::<fake_vulkan::FakeVulkan>();
                    std::mem::transmute(
                        fake_vulkan.get_function(func_name.cast())
                    )
                }
            }

            let vkinst = unsafe {
                vulkan_funcs::Instance::new(
                    get_instance_proc,
                    fake_vulkan.as_ref()
                        as *const fake_vulkan::FakeVulkan
                        as *const c_void,
                )
            };

            let mut device = std::ptr::null_mut();

            unsafe {
                let mut count = 1;
                let res = vkinst.vkEnumeratePhysicalDevices.unwrap()(
                    instance,
                    std::ptr::addr_of_mut!(count),
                    std::ptr::addr_of_mut!(device),
                );

                assert_eq!(res, vk::VK_SUCCESS);
            }

            FakeVulkanData {
                fake_vulkan,
                _vklib: vklib,
                vkinst,
                instance,
                device,
            }
        }
    }

    impl Drop for FakeVulkanData {
        fn drop(&mut self) {
            if !std::thread::panicking() {
                unsafe {
                    self.vkinst.vkDestroyInstance.unwrap()(
                        self.instance,
                        std::ptr::null(), // allocator
                    );
                }
            }
        }
    }

    fn check_base_features<'a>(
        reqs: &'a Requirements,
        features: &vk::VkPhysicalDeviceFeatures
    ) -> Result<(), CheckError<'a>> {
        let mut data = FakeVulkanData::new();

        data.fake_vulkan.physical_devices[0].features = features.clone();

        reqs.check(&data.vkinst, data.device)
    }

    #[test]
    fn test_check_base_features() {
        let mut features = Default::default();

        assert!(matches!(
            check_base_features(&Requirements::new(), &features),
            Ok(()),
        ));

        features.geometryShader = vk::VK_TRUE;

        assert!(matches!(
            check_base_features(&Requirements::new(), &features),
            Ok(()),
        ));

        let mut reqs = Requirements::new();
        reqs.add("geometryShader");
        assert!(matches!(
            check_base_features(&reqs, &features),
            Ok(()),
        ));

        reqs.add("depthBounds");
        match check_base_features(&reqs, &features) {
            Ok(()) => unreachable!("Requirements::check was supposed to fail"),
            Err(e) => {
                assert!(matches!(e, CheckError::MissingBaseFeature(_)));
                assert_eq!(
                    e.to_string(),
                    "Missing required feature: depthBounds"
                );
            },
        }
    }

    #[test]
    fn test_check_extensions() {
        let mut data = FakeVulkanData::new();
        let mut reqs = Requirements::new();

        assert!(matches!(
            reqs.check(&data.vkinst, data.device),
            Ok(()),
        ));

        reqs.add("fake_extension");

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected extensions check to fail"),
            Err(e) => {
                assert!(matches!(
                    e,
                    CheckError::MissingExtension("fake_extension"),
                ));
                assert_eq!(
                    e.to_string(),
                    "Missing required extension: fake_extension"
                );
            },
        };

        data.fake_vulkan.physical_devices[0].add_extension("fake_extension");

        assert!(matches!(
            reqs.check(&data.vkinst, data.device),
            Ok(()),
        ));

        // Add an extension via a feature
        reqs.add("multiviewGeometryShader");

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected extensions check to fail"),
            Err(e) => {
                assert!(matches!(
                    e,
                    CheckError::MissingExtension("VK_KHR_multiview")
                ));
                assert_eq!(
                    e.to_string(),
                    "Missing required extension: VK_KHR_multiview",
                );
            },
        };

        data.fake_vulkan.physical_devices[0].add_extension("VK_KHR_multiview");

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected extensions check to fail"),
            Err(e) => {
                assert!(matches!(
                    e,
                    CheckError::MissingFeature { .. },
                ));
                assert_eq!(
                    e.to_string(),
                    "Missing required feature “multiviewGeometryShader” from \
                     extension “VK_KHR_multiview”",
                );
            },
        };

        // Make an unterminated UTF-8 character
        let extension_name = &mut data
            .fake_vulkan
            .physical_devices[0]
            .extensions[0]
            .extensionName;
        extension_name[0] = -1i8 as c_char;
        extension_name[1] = 0;

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected extensions check to fail"),
            Err(e) => {
                assert_eq!(
                    e.to_string(),
                    "Invalid UTF-8 in string returned from \
                     vkEnumerateDeviceExtensionProperties"
                );
                assert!(matches!(e, CheckError::Invalid(_)));
            },
        };

        // No null-terminator in the extension
        extension_name.fill(32);

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected extensions check to fail"),
            Err(e) => {
                assert_eq!(
                    e.to_string(),
                    "NULL terminator missing in string returned from \
                     vkEnumerateDeviceExtensionProperties"
                );
                assert!(matches!(e, CheckError::Invalid(_)));
            },
        };
    }

    #[test]
    fn test_check_structures() {
        let mut data = FakeVulkanData::new();
        let mut reqs = Requirements::new();

        reqs.add("multiview");
        data.fake_vulkan.physical_devices[0].add_extension("VK_KHR_multiview");

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected features check to fail"),
            Err(e) => {
                assert_eq!(
                    e.to_string(),
                    "Missing required feature “multiview” \
                     from extension “VK_KHR_multiview”",
                );
                assert!(matches!(
                    e,
                    CheckError::MissingFeature { .. },
                ));
            },
        };

        data.fake_vulkan.physical_devices[0].multiview.multiview = vk::VK_TRUE;

        assert!(matches!(
            reqs.check(&data.vkinst, data.device),
            Ok(()),
        ));

        reqs.add("shaderBufferInt64Atomics");
        data.fake_vulkan.physical_devices[0].add_extension(
            "VK_KHR_shader_atomic_int64"
        );

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected features check to fail"),
            Err(e) => {
                assert_eq!(
                    e.to_string(),
                    "Missing required feature “shaderBufferInt64Atomics” \
                     from extension “VK_KHR_shader_atomic_int64”",
                );
                assert!(matches!(
                    e,
                    CheckError::MissingFeature { .. },
                ));
            },
        };

        data.fake_vulkan
            .physical_devices[0]
            .shader_atomic
            .shaderBufferInt64Atomics =
            vk::VK_TRUE;

        assert!(matches!(
            reqs.check(&data.vkinst, data.device),
            Ok(()),
        ));
    }

    #[test]
    fn test_check_version() {
        let mut data = FakeVulkanData::new();
        let mut reqs = Requirements::new();

        reqs.add_version(1, 2, 0);
        data.fake_vulkan.physical_devices[0].properties.apiVersion =
            make_version(1, 1, 0);

        match reqs.check(&data.vkinst, data.device) {
            Ok(()) => unreachable!("expected version check to fail"),
            Err(e) => {
                // The check will report that the version is 1.0.0
                // because vkEnumerateInstanceVersion is not available
                assert_eq!(
                    e.to_string(),
                    "Vulkan API version 1.2.0 required but the driver \
                     reported 1.1.0",
                );
                assert!(matches!(
                    e,
                    CheckError::VersionTooLow { .. },
                ));
            },
        };

        // Set a valid version
        data.fake_vulkan.physical_devices[0].properties.apiVersion =
            make_version(1, 3, 0);

        assert!(matches!(
            reqs.check(&data.vkinst, data.device),
            Ok(()),
        ));
    }
}

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

use crate::vk;
use crate::shader_stage;
use crate::logger::Logger;
use crate::context::Context;
use crate::script::{Script, Shader};
use crate::temp_file;
use crate::requirements::extract_version;
use std::fmt;
use std::mem;
use std::ptr;
use std::io::{self, Write, Read, BufWriter};
use std::fs::File;
use std::path::Path;
use std::env;
use std::process::{Stdio, Command, Output};

/// An error that can be returned by [build_stage].
#[derive(Debug)]
pub enum Error {
    /// There were no shaders for this stage in the script
    MissingStageShaders(shader_stage::Stage),
    /// vkCreateShaderModule returned an error
    CreateShaderModuleFailed,
    /// Error creating a temporary file
    TempFile(temp_file::Error),
    /// Other I/O error
    IoError(io::Error),
    /// A compiler or assembler command returned a non-zero status
    CommandFailed,
    /// The generated shader binary didn’t have the right SPIR-V magic
    /// number or wasn’t a multiple of 32-bit integers.
    InvalidShaderBinary,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::MissingStageShaders(stage) => write!(
                f,
                "No shaders for stage {:?}",
                stage,
            ),
            Error::CreateShaderModuleFailed => write!(
                f,
                "vkCreateShaderModule failed"
            ),
            Error::TempFile(e) => e.fmt(f),
            Error::IoError(e) => e.fmt(f),
            Error::CommandFailed => write!(
                f,
                "A subprocess failed with a non-zero exit status",
            ),
            Error::InvalidShaderBinary => write!(
                f,
                "The compiler or assembler generated an invalid SPIR-V binary",
            ),
        }
    }
}

impl From<temp_file::Error> for Error {
    fn from(e: temp_file::Error) -> Error {
        Error::TempFile(e)
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Error {
        Error::IoError(e)
    }
}

fn stage_name(stage: shader_stage::Stage) -> &'static str {
    match stage {
        shader_stage::Stage::Vertex => "vert",
        shader_stage::Stage::TessCtrl => "tesc",
        shader_stage::Stage::TessEval => "tese",
        shader_stage::Stage::Geometry => "geom",
        shader_stage::Stage::Fragment => "frag",
        shader_stage::Stage::Compute => "comp",
    }
}

fn handle_command_output(
    logger: &mut Logger,
    output: Output,
) -> Result<(), Error> {
    logger.write_all(output.stdout.as_slice())?;
    logger.write_all(output.stderr.as_slice())?;

    if output.status.success() {
        Ok(())
    } else {
        Err(Error::CommandFailed)
    }
}

fn show_disassembly_from_file(
    logger: &mut Logger,
    filename: &Path,
) -> Result<(), Error> {
    let exe = match env::var_os("PIGLIT_SPIRV_DIS_BINARY") {
        Some(exe) => exe,
        None => "spirv-dis".into(),
    };

    handle_command_output(
        logger,
        Command::new(exe)
            .arg(filename)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()?
    )
}

fn create_shader_from_binary(
    context: &Context,
    data: &[u32],
) -> Result<vk::VkShaderModule, Error> {
    let shader_module_create_info = vk::VkShaderModuleCreateInfo {
        sType: vk::VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        pNext: ptr::null(),
        flags: 0,
        codeSize: data.len() * mem::size_of::<u32>(),
        pCode: data.as_ptr(),
    };

    let mut module: vk::VkShaderModule = ptr::null_mut();

    let res = unsafe {
        context.device().vkCreateShaderModule.unwrap()(
            context.vk_device(),
            ptr::addr_of!(shader_module_create_info),
            ptr::null(), // allocator
            ptr::addr_of_mut!(module),
        )
    };

    if res == vk::VK_SUCCESS {
        Ok(module)
    } else {
        Err(Error::CreateShaderModuleFailed)
    }
}

fn create_shader_from_binary_file(
    context: &Context,
    file: &mut File,
) -> Result<vk::VkShaderModule, Error> {
    let mut data = Vec::<u32>::new();
    let mut buf = vec![0u8; 512];
    let mut buf_length = 0usize;
    let mut big_endian = false;
    let mut bytes = [0u8; mem::size_of::<u32>()];

    loop {
        let got = file.read(&mut buf[buf_length..])?;

        if got == 0 {
            break;
        }

        buf_length += got;

        for i in 0..buf_length / mem::size_of::<u32>() {
            let buf_start = i * mem::size_of::<u32>();
            bytes.copy_from_slice(&buf[
                buf_start..buf_start + mem::size_of::<u32>()
            ]);

            if data.len() == 0 {
                if bytes == [0x07, 0x23, 0x02, 0x03] {
                    big_endian = true;
                } else if bytes == [0x03, 0x02, 0x23, 0x07] {
                    big_endian = false;
                } else {
                    return Err(Error::InvalidShaderBinary);
                }
            }

            data.push(if big_endian {
                u32::from_be_bytes(bytes)
            } else {
                u32::from_le_bytes(bytes)
            });
        }

        let remainder = buf_length % mem::size_of::<u32>();

        buf.copy_within(buf_length - remainder.., 0);
        buf_length = remainder;
    }

    if buf_length != 0 {
        Err(Error::InvalidShaderBinary)
    } else {
        create_shader_from_binary(context, &data)
    }
}

fn create_temp_file_for_source(
    source: &str
) -> Result<temp_file::TempFile, Error> {
    let mut temp_file = temp_file::TempFile::new()?;
    temp_file.file().unwrap().write_all(source.as_bytes())?;
    temp_file.file().unwrap().flush()?;
    temp_file.close();

    Ok(temp_file)
}

fn version_string(version: u32) -> String {
    let (version_major, version_minor, _) = extract_version(version);
    format!("vulkan{}.{}", version_major, version_minor)
}

fn compile_glsl(
    logger: &mut Logger,
    context: &Context,
    script: &Script,
    stage: shader_stage::Stage,
    show_disassembly: bool,
) -> Result<vk::VkShaderModule, Error> {
    let mut shader_files = Vec::new();

    for shader in script.shaders(stage) {
        let source = match &shader {
            Shader::Glsl(s) => s,
            _ => unreachable!("Unexpected shader type"),
        };
        shader_files.push(create_temp_file_for_source(&source)?);
    }

    let version_str = version_string(script.requirements().version());

    let mut module_file = temp_file::TempFile::new()?;

    let exe = match env::var_os("PIGLIT_GLSLANG_VALIDATOR_BINARY") {
        Some(exe) => exe,
        None => "glslangValidator".into(),
    };

    handle_command_output(
        logger,
        Command::new(exe)
            .args([
                "-V",
                "--target-env", &version_str,
                "-S", stage_name(stage),
            ])
            .arg("-o").arg(module_file.filename())
            .args(shader_files.iter().map(|file| file.filename()))
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()?
    )?;

    drop(shader_files);

    if show_disassembly {
        show_disassembly_from_file(logger, module_file.filename())?;
    }

    create_shader_from_binary_file(context, module_file.file().unwrap())
}

fn assemble_spirv(
    logger: &mut Logger,
    context: &Context,
    script: &Script,
    source: &str,
    show_disassembly: bool,
) -> Result<vk::VkShaderModule, Error> {
    let version_str = version_string(script.requirements().version());

    let mut module_file = temp_file::TempFile::new()?;

    let source_file = create_temp_file_for_source(source)?;

    let exe = match env::var_os("PIGLIT_SPIRV_AS_BINARY") {
        Some(exe) => exe,
        None => "spirv-as".into(),
    };

    handle_command_output(
        logger,
        Command::new(exe)
            .args(["--target-env", &version_str])
            .arg("-o").arg(module_file.filename())
            .arg(source_file.filename())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()?
    )?;

    drop(source_file);

    if show_disassembly {
        show_disassembly_from_file(logger, module_file.filename())?;
    }

    create_shader_from_binary_file(context, module_file.file().unwrap())
}

fn load_binary_stage(
    logger: &mut Logger,
    context: &Context,
    data: &[u32],
    show_disassembly: bool,
) -> Result<vk::VkShaderModule, Error> {
    if show_disassembly {
        let mut temp_file = temp_file::TempFile::new()?;
        let mut writer = BufWriter::new(temp_file.file().unwrap());

        for value in data {
            writer.write_all(&value.to_ne_bytes())?;
        }

        writer.flush()?;

        drop(writer);

        temp_file.close();

        show_disassembly_from_file(logger, temp_file.filename())?;
    }

    create_shader_from_binary(context, data)
}

pub fn build_stage(
    logger: &mut Logger,
    context: &Context,
    script: &Script,
    stage: shader_stage::Stage,
    show_disassembly: bool,
) -> Result<vk::VkShaderModule, Error> {
    let shaders = script.shaders(stage);

    match shaders.get(0) {
        None => Err(Error::MissingStageShaders(stage)),
        Some(Shader::Glsl(_)) => compile_glsl(
            logger,
            context,
            script,
            stage,
            show_disassembly,
        ),
        Some(Shader::Spirv(source)) => {
            // The script parser should have ensured that there’s
            // only one shader
            assert_eq!(shaders.len(), 1);
            assemble_spirv(
                logger,
                context,
                script,
                source,
                show_disassembly
            )
        },
        Some(Shader::Binary(data)) => {
            // The script parser should have ensured that there’s
            // only one shader
            assert_eq!(shaders.len(), 1);
            load_binary_stage(
                logger,
                context,
                data,
                show_disassembly
            )
        },
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_vulkan::{FakeVulkan, HandleType};
    use crate::context::Context;
    use crate::requirements::Requirements;
    use crate::source::Source;
    use crate::config::Config;
    use std::ffi::{c_char, c_void, CStr};

    struct CompileOutput {
        log: Vec<String>,
        result: Result<Vec<u32>, Error>,
    }

    extern "C" fn log_cb(message: *const c_char, user_data: *mut c_void) {
        unsafe {
            let message = CStr::from_ptr(message).to_str().unwrap().to_owned();
            let log = &mut *(user_data as *mut Vec<String>);
            log.push(message);
        }
    }

    fn compile_script(
        source: &str,
        stage: shader_stage::Stage,
        show_disassembly: bool,
    ) -> CompileOutput {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());

        compile_script_with_fake_vulkan(
            &mut fake_vulkan,
            source,
            stage,
            show_disassembly
        )
    }

    fn compile_script_with_fake_vulkan(
        fake_vulkan: &mut FakeVulkan,
        source: &str,
        stage: shader_stage::Stage,
        show_disassembly: bool,
    ) -> CompileOutput {
        fake_vulkan.set_override();
        let context = Context::new(&Requirements::new(), None).unwrap();

        let source = Source::from_string(source.to_owned());
        let script = Script::load(&Config::new(), &source).unwrap();
        let mut log = Vec::new();
        let mut logger = Logger::new(
            Some(log_cb),
            ptr::addr_of_mut!(log).cast(),
        );

        let result = build_stage(
            &mut logger,
            &context,
            &script,
            stage,
            show_disassembly,
        ).map(|module| {
            let code = match &fake_vulkan.get_handle(module).data {
                HandleType::ShaderModule { code } => code.clone(),
                _ => unreachable!("Unexpected Vulkan handle type"),
            };
            unsafe {
                context.device().vkDestroyShaderModule.unwrap()(
                    context.vk_device(),
                    module,
                    ptr::null(), // allocator
                );
            }
            code
        });

        CompileOutput { log, result }
    }

    #[test]
    fn glsl_le() {
        let CompileOutput { log, result } = compile_script(
            "[fragment shader]\n\
             03 02 23 07\n\
             fe ca fe ca\n",
            shader_stage::Stage::Fragment,
            false, // show_disassembly
        );

        assert_eq!(
            &log,
            &[
                "vulkan_spirv",
                "target_env: vulkan1.0",
                "stage: frag",
            ],
        );

        assert_eq!(result.unwrap(), &[0x07230203, 0xcafecafe]);
    }

    #[test]
    fn glsl_be() {
        let CompileOutput { log, result } = compile_script(
            "[vertex shader]\n\
             07 23 02 03\n\
             ca fe ca fe\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );

        assert_eq!(
            &log,
            &[
                "vulkan_spirv",
                "target_env: vulkan1.0",
                "stage: vert",
            ],
        );

        assert_eq!(result.unwrap(), &[0x07230203, 0xcafecafe]);
    }

    #[test]
    fn spirv() {
        let CompileOutput { log, result } = compile_script(
            "[vertex shader spirv]\n\
             07 23 02 03\n\
             f0 0d fe ed\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );

        assert_eq!(&log, &["target_env: vulkan1.0"]);

        assert_eq!(result.unwrap(), &[0x07230203, 0xf00dfeed]);
    }

    #[test]
    fn binary() {
        let CompileOutput { log, result } = compile_script(
            "[vertex shader binary]\n\
             07230203\n\
             f00dfeed\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );

        assert!(log.is_empty());

        assert_eq!(result.unwrap(), &[0x07230203, 0xf00dfeed]);
    }

    fn test_stage(
        stage: shader_stage::Stage,
        section_name: &str,
        compiler_argument: &str
    ) {
        let CompileOutput { log, result } = compile_script(
            &format!(
                "[{}]\n\
                 07 23 02 03\n",
                section_name,
            ),
            stage,
            false, // show_disassembly
        );

        assert_eq!(
            &log,
            &[
                "vulkan_spirv",
                "target_env: vulkan1.0",
                &format!("stage: {}", compiler_argument)
            ]
        );

        assert!(result.is_ok());
    }

    #[test]
    fn all_stages() {
        test_stage(
            shader_stage::Stage::Vertex,
            "vertex shader",
            "vert"
        );
        test_stage(
            shader_stage::Stage::TessCtrl,
            "tessellation control shader",
            "tesc"
        );
        test_stage(
            shader_stage::Stage::TessEval,
            "tessellation evaluation shader",
            "tese"
        );
        test_stage(
            shader_stage::Stage::Geometry,
            "geometry shader",
            "geom"
        );
        test_stage(
            shader_stage::Stage::Fragment,
            "fragment shader",
            "frag"
        );
        test_stage(
            shader_stage::Stage::Compute,
            "compute shader",
            "comp"
        );
    }

    #[test]
    fn no_shaders() {
        let CompileOutput { result, .. } = compile_script(
            "[vertex shader]\n\
             07 23 02 03\n",
            shader_stage::Stage::Fragment,
            false, // show_disassembly
        );
        assert_eq!(
            &result.unwrap_err().to_string(),
            "No shaders for stage Fragment",
        );
    }

    #[test]
    fn create_shader_module_error() {
        let mut fake_vulkan = FakeVulkan::new();
        fake_vulkan.physical_devices.push(Default::default());
        fake_vulkan.queue_result(
            "vkCreateShaderModule".to_string(),
            vk::VK_ERROR_UNKNOWN
        );

        let CompileOutput { result, .. } = compile_script_with_fake_vulkan(
            &mut fake_vulkan,
            "[vertex shader]\n\
             07 23 02 03\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );
        assert_eq!(
            &result.unwrap_err().to_string(),
            "vkCreateShaderModule failed",
        );
    }

    #[test]
    fn invalid_magic() {
        let CompileOutput { result, .. } = compile_script(
            "[vertex shader]\n\
             12 34 56 78\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );
        assert_eq!(
            &result.unwrap_err().to_string(),
            "The compiler or assembler generated an invalid SPIR-V binary",
        );
    }

    #[test]
    fn not_multiple_of_u32() {
        let CompileOutput { result, .. } = compile_script(
            "[vertex shader]\n\
             07 23 02 03 9a\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );
        assert_eq!(
            &result.unwrap_err().to_string(),
            "The compiler or assembler generated an invalid SPIR-V binary",
        );
    }

    #[test]
    fn compiler_failed() {
        let CompileOutput { result, .. } = compile_script(
            "[vertex shader]\n\
             invalid hex values\n",
            shader_stage::Stage::Vertex,
            false, // show_disassembly
        );
        assert_eq!(
            &result.unwrap_err().to_string(),
            "A subprocess failed with a non-zero exit status",
        );
    }

    fn test_show_disassembly(section_suffix: &str) {
        let CompileOutput { log, result } = compile_script(
            &format!(
                "[fragment shader{}]\n\
                 03 02 23 07\n\
                 fe ca fe ca\n",
                section_suffix,
            ),
            shader_stage::Stage::Fragment,
            true, // show_disassembly
        );

        assert!(log.iter().find(|&line| line == "disassembly").is_some());

        assert!(result.is_ok());
    }

    #[test]
    fn show_glsl_disassembly() {
        test_show_disassembly("");
    }

    #[test]
    fn show_spirv_disassembly() {
        test_show_disassembly(" spirv");
    }

    #[test]
    fn show_binary_disassembly() {
        test_show_disassembly(" binary");
    }
}

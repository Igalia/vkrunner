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

use std::ffi::{OsString, OsStr};
use std::collections::HashMap;
use std::fmt;
use std::process::ExitCode;
use std::ptr;
use std::rc::Rc;
use std::cell::RefCell;
use std::ffi::c_void;
use std::io::{self, BufWriter};
use std::fs::File;
extern crate vkrunner;
use vkrunner::{Config, Executor, Source, inspect, result};

#[derive(Debug)]
struct Opt {
    short: Option<char>,
    long: &'static str,
    help: &'static str,
    argument_name: Option<&'static str>,
    argument_type: ArgumentType,
}

#[derive(Debug)]
enum OptError {
    UnknownOption(String),
    MissingArgument(&'static Opt),
    InvalidUtf8(&'static Opt),
    InvalidInteger(String),
}

#[derive(Debug)]
enum Error {
    OptError(OptError),
    InvalidTokenReplacement(String),
    ShowHelp,
    TestFailed,
    IoError(io::Error),
    BufferNotFound(u32),
    NoBuffers,
    ZeroDeviceId,
}

#[derive(Debug)]
enum ArgumentType {
    Flag,
    Filename,
    StringArray,
    Integer,
}

#[derive(Debug)]
enum ArgumentValue {
    Flag,
    Filename(OsString),
    StringArray(Vec<String>),
    Integer(u32),
}

type OptValues = HashMap<&'static str, ArgumentValue>;

#[derive(Debug)]
struct Options {
    values: OptValues,
    scripts: Vec<OsString>,
}

#[derive(Debug)]
struct TokenReplacement<'a> {
    token: &'a str,
    replacement: &'a str,
}

impl fmt::Display for OptError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            OptError::UnknownOption(s) => write!(f, "Unknown option: {}", s),
            OptError::MissingArgument(o) => {
                write!(f, "Option --{} requires an argument", o.long)
            },
            OptError::InvalidUtf8(o) => {
                write!(f, "Invalid UTF-8 in argument to --{}", o.long)
            },
            OptError::InvalidInteger(s) => {
                write!(f, "Invalid integer: {}", s)
            },
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::OptError(e) => e.fmt(f),
            Error::InvalidTokenReplacement(s) => {
                write!(f, "Invalid token replacement: {}", s)
            },
            Error::ShowHelp => format_help(f),
            Error::TestFailed => {
                write!(f, "{}", format_result(result::Result::Fail))
            },
            Error::IoError(e) => e.fmt(f),
            Error::BufferNotFound(binding) => {
                write!(f, "No buffer with binding {} was found", binding)
            },
            Error::NoBuffers => {
                write!(
                    f,
                    "Buffer dump requested but the script has no buffers"
                )
            },
            Error::ZeroDeviceId => {
                write!(
                    f,
                    "Device IDs start from 1 but 0 was specified",
                )
            },
        }
    }
}

impl From<OptError> for Error {
    fn from(e: OptError) -> Error {
        Error::OptError(e)
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Error {
        Error::IoError(e)
    }
}

static HELP_OPTION: &'static str = "help";
static IMAGE_OPTION: &'static str = "image";
static BUFFER_OPTION: &'static str = "buffer";
static BINDING_OPTION: &'static str = "binding";
static DISASM_OPTION: &'static str = "disasm";
static REPLACE_OPTION: &'static str = "replace";
static QUIET_OPTION: &'static str = "quiet";
static DEVICE_ID_OPTION: &'static str = "device-id";

static OPTIONS: [Opt; 8] = [
    Opt {
        short: Some('h'),
        long: HELP_OPTION,
        help: "Show this help message",
        argument_name: None,
        argument_type: ArgumentType::Flag,
    },
    Opt {
        short: Some('i'),
        long: IMAGE_OPTION,
        help: "Write the final rendering to IMG as a PPM image",
        argument_name: Some("IMG"),
        argument_type: ArgumentType::Filename,
    },
    Opt {
        short: Some('b'),
        long: BUFFER_OPTION,
        help: "Dump contents of a UBO or SSBO to BUF",
        argument_name: Some("BUF"),
        argument_type: ArgumentType::Filename,
    },
    Opt {
        short: Some('B'),
        long: BINDING_OPTION,
        help: "Select which buffer to dump using the -b option. Defaults to \
               first buffer",
        argument_name: Some("BINDING"),
        argument_type: ArgumentType::Integer,
    },
    Opt {
        short: Some('d'),
        long: DISASM_OPTION,
        help: "Show the SPIR-V disassembly",
        argument_name: None,
        argument_type: ArgumentType::Flag,
    },
    Opt {
        short: Some('D'),
        long: REPLACE_OPTION,
        help: "Replace occurences of TOK with REPL in the scripts",
        argument_name: Some("TOK=REPL"),
        argument_type: ArgumentType::StringArray,
    },
    Opt {
        short: Some('q'),
        long: QUIET_OPTION,
        help: "Don’t print any non-error information to stdout",
        argument_name: None,
        argument_type: ArgumentType::Flag,
    },
    Opt {
        short: None,
        long: DEVICE_ID_OPTION,
        help: "Select the Vulkan device",
        argument_name: Some("DEVID"),
        argument_type: ArgumentType::Integer,
    },
];

fn format_help(f: &mut fmt::Formatter) -> fmt::Result {
    writeln!(
        f,
        "usage: vkrunner [OPTION]… SCRIPT…\n\
         Runs the shader test script SCRIPT\n\
         \n\
         Options:"
    )?;

    let longest_long = OPTIONS
        .iter()
        .map(|o| o.long.chars().count())
        .max()
        .unwrap();
    let longest_arg = OPTIONS
        .iter()
        .map(|o| o.argument_name.map(|n| n.chars().count()).unwrap_or(0))
        .max()
        .unwrap();

    for (i, option) in OPTIONS.iter().enumerate() {
        if i > 0 {
            writeln!(f)?;
        }

        write!(f, " ")?;

        match option.short {
            Some(c) => write!(f, "-{},", c)?,
            None => write!(f, "   ")?,
        }

        write!(
            f,
            "--{:long_width$} {:arg_width$} {}",
            option.long,
            option.argument_name.unwrap_or(""),
            option.help,
            long_width = longest_long,
            arg_width = longest_arg,
        )?;
    }

    Ok(())
}

fn process_argument<I>(
    values: &mut OptValues,
    args: &mut I,
    opt: &'static Opt,
) -> Result<(), OptError>
    where I: Iterator<Item = OsString>
{
    match opt.argument_type {
        ArgumentType::Flag => {
            values.insert(opt.long, ArgumentValue::Flag);
        },
        ArgumentType::Filename => match args.next() {
            Some(filename) => {
                values.insert(
                    opt.long,
                    ArgumentValue::Filename(filename),
                );
            },
            None => return Err(OptError::MissingArgument(opt)),
        },
        ArgumentType::StringArray => match args.next() {
            Some(arg) => match arg.to_str() {
                Some(s) => {
                    values.entry(opt.long)
                        .and_modify(|value| match value {
                            ArgumentValue::StringArray(values) =>
                                values.push(s.to_string()),
                            _ => unreachable!(),
                        })
                        .or_insert_with(|| ArgumentValue::StringArray(
                            vec![s.to_string()]
                        ));
                },
                None => return Err(OptError::InvalidUtf8(opt)),
            },
            None => return Err(OptError::MissingArgument(opt)),
        },
        ArgumentType::Integer => match args.next() {
            Some(arg) => match arg.to_str() {
                Some(s) => match s.parse::<u32>() {
                    Ok(value) => {
                        values.insert(
                            opt.long,
                            ArgumentValue::Integer(value),
                        );
                    },
                    Err(_) => {
                        return Err(OptError::InvalidInteger(
                            s.to_owned()
                        ));
                    },
                },
                None => return Err(OptError::InvalidUtf8(opt)),
            },
            None => return Err(OptError::MissingArgument(opt)),
        },
    }

    Ok(())
}

fn process_long_arg<I>(
    values: &mut OptValues,
    args: &mut I,
    arg: &str
) -> Result<(), OptError>
    where I: Iterator<Item = OsString>
{
    for opt in OPTIONS.iter() {
        if opt.long.eq(arg) {
            return process_argument(values, args, opt);
        }
    }

    Err(OptError::UnknownOption(format!("--{}", arg)))
}

fn process_short_args<I>(
    values: &mut OptValues,
    args: &mut I,
    arg: &str
) -> Result<(), OptError>
    where I: Iterator<Item = OsString>
{
    if arg.len() == 0 {
        return Err(OptError::UnknownOption("-".to_string()));
    }

    'arg_loop: for ch in arg.chars() {
        for opt in OPTIONS.iter() {
            if let Some(opt_ch) = opt.short {
                if opt_ch == ch {
                    process_argument(values, args, opt)?;
                    continue 'arg_loop;
                }
            }
        }

        return Err(OptError::UnknownOption(format!("{}", ch)));
    }

    Ok(())
}

fn parse_options<I>(
    mut args: I
) -> Result<Options, OptError>
    where I: Iterator<Item = OsString>
{
    let mut values = HashMap::new();
    let mut scripts = Vec::new();

    // Skip the first arg
    if args.next().is_none() {
        return Ok(Options { values, scripts });
    }

    while let Some(arg) = args.next() {
        match arg.to_str() {
            Some(arg_str) => {
                if arg_str == "--" {
                    scripts.extend(args);
                    break;
                } else if arg_str.starts_with("--") {
                    process_long_arg(&mut values, &mut args, &arg_str[2..])?;
                } else if arg_str.starts_with("-") {
                    process_short_args(&mut values, &mut args, &arg_str[1..])?;
                } else {
                    scripts.push(arg);
                }
            },
            None => scripts.push(arg),
        }
    }

    Ok(Options { values, scripts })
}

impl<'a> InspectData<'a> {
    fn new(options: &'a Options) -> InspectData {
        InspectData {
            image_filename: match options.values.get(IMAGE_OPTION) {
                Some(ArgumentValue::Filename(filename)) => {
                    Some(filename)
                },
                _ => None,
            },
            buffer_filename: match options.values.get(BUFFER_OPTION) {
                Some(ArgumentValue::Filename(filename)) => {
                    Some(filename)
                },
                _ => None,
            },
            buffer_binding: match options.values.get(BINDING_OPTION) {
                Some(&ArgumentValue::Integer(binding)) => {
                    Some(binding)
                },
                _ => None,
            },
            failed: false,
        }
    }
}

fn get_token_replacements<'a>(
    values: &'a OptValues
) -> Result<Vec<TokenReplacement<'a>>, Error> {
    let mut res = Vec::new();

    if let Some(ArgumentValue::StringArray(replacements)) =
        values.get(REPLACE_OPTION)
    {
        for replacement in replacements {
            match replacement.split_once('=') {
                None => {
                    return Err(Error::InvalidTokenReplacement(
                        replacement.to_owned()
                    ));
                },
                Some((token, replacement)) => res.push(TokenReplacement {
                    token,
                    replacement,
                }),
            }
        }
    }

    Ok(res)
}

struct InspectData<'a> {
    image_filename: Option<&'a OsStr>,
    buffer_filename: Option<&'a OsStr>,
    buffer_binding: Option<u32>,
    failed: bool,
}

fn write_ppm(
    image: &inspect::Image,
    filename: &OsStr,
) -> Result<(), Error> {
    let mut file = BufWriter::new(File::create(filename)?);

    use std::io::Write;

    write!(
        &mut file,
        "P6\n\
         {} {}\n\
         255\n",
        image.width,
        image.height,
    )?;

    let format_size = image.format.size();

    for y in 0..image.height {
        let mut line = unsafe {
            std::slice::from_raw_parts(
                (image.data as *const u8).add(y as usize * image.stride),
                image.width as usize * format_size,
            )
        };

        for _ in 0..image.width {
            let pixel = image.format.load_pixel(&line[0..format_size]);
            let mut bytes = [0u8; 3];

            for (i, component) in pixel[0..3].iter().enumerate() {
                bytes[i] = (component.clamp(0.0, 1.0) * 255.0).round() as u8;
            }

            file.write_all(&bytes)?;

            line = &line[format_size..];
        }
    }

    Ok(())
}

fn write_buffer(
    data: &inspect::Data,
    filename: &OsStr,
    binding: Option<u32>,
) -> Result<(), Error> {
    let buffers = unsafe {
        std::slice::from_raw_parts(data.buffers, data.n_buffers)
    };

    let buffer = match binding {
        None => match buffers.get(0) {
            Some(buffer) => buffer,
            None => return Err(Error::NoBuffers),
        },
        Some(binding) => match buffers.iter().find(
            |b| b.binding as u32 == binding
        ) {
            Some(buffer) => buffer,
            None => return Err(Error::BufferNotFound(binding)),
        },
    };

    let mut file = File::create(filename)?;

    let data = unsafe {
        std::slice::from_raw_parts(
            buffer.data as *const u8,
            buffer.size,
        )
    };

    use std::io::Write;

    file.write_all(data)?;

    Ok(())
}

extern "C" fn inspect_cb(data: &inspect::Data, user_data: *mut c_void) {
    let inspect_data = unsafe { &mut *(user_data as *mut InspectData) };

    if let Some(filename) = inspect_data.image_filename {
        match write_ppm(&data.color_buffer, filename) {
            Err(e) => {
                eprintln!("{}", e);
                inspect_data.failed = true;
            },
            Ok(()) => (),
        }
    }

    if let Some(filename) = inspect_data.buffer_filename {
        match write_buffer(&data, filename, inspect_data.buffer_binding) {
            Err(e) => {
                eprintln!("{}", e);
                inspect_data.failed = true;
            },
            Ok(()) => (),
        }
    }
}

fn set_up_config(
    config: &Rc<RefCell<Config>>,
    options: &Options,
    inspect_data: &mut InspectData,
) -> Result<(), Error> {
    let mut config = config.borrow_mut();

    config.set_inspect_cb(Some(inspect_cb));
    config.set_user_data(ptr::addr_of_mut!(*inspect_data).cast());

    if let Some(&ArgumentValue::Integer(device_id))
        = options.values.get(DEVICE_ID_OPTION)
    {
        match device_id.checked_sub(1) {
            None => return Err(Error::ZeroDeviceId),
            Some(device_id) => config.set_device_id(Some(device_id as usize)),
        }
    }

    if let Some(ArgumentValue::Flag) = options.values.get(DISASM_OPTION) {
        config.set_show_disassembly(true);
    }

    Ok(())
}

fn format_result(result: result::Result) -> String {
    format!("PIGLIT: {{\"result\": \"{}\" }}", result.name())
}

fn add_token_replacements(
    token_replacements: &[TokenReplacement<'_>],
    source: &mut Source,
) {
    for token_replacement in token_replacements.iter() {
        source.add_token_replacement(
            token_replacement.token.to_owned(),
            token_replacement.replacement.to_owned(),
        );
    }
}

fn run() -> Result<(), Error> {
    let options = parse_options(std::env::args_os())?;

    if options.values.contains_key(HELP_OPTION) || options.scripts.is_empty() {
        return Err(Error::ShowHelp);
    }

    let config = Rc::new(RefCell::new(Config::new()));

    let mut inspect_data = InspectData::new(&options);

    set_up_config(&config, &options, &mut inspect_data)?;

    let token_replacements = get_token_replacements(&options.values)?;

    let mut executor = Executor::new(Rc::clone(&config));

    let mut overall_result = result::Result::Skip;

    for script_filename in options.scripts.iter() {
        if options.scripts.len() > 1
            && !options.values.contains_key(QUIET_OPTION)
        {
            println!("{}", script_filename.to_string_lossy());
        }

        let mut source = Source::from_file(script_filename.into());

        add_token_replacements(&token_replacements, &mut source);

        let result = executor.execute(&source);

        overall_result = overall_result.merge(result);
    }

    if inspect_data.failed {
        overall_result = overall_result.merge(result::Result::Fail);
    }

    match overall_result {
        result::Result::Fail => Err(Error::TestFailed),
        result::Result::Pass if options.values.contains_key(QUIET_OPTION) => {
            Ok(())
        },
        _ => {
            println!("{}", format_result(overall_result));
            Ok(())
        },
    }
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{}", e);
            ExitCode::FAILURE
        },
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn all_arg_types() {
        let args: Vec<OsString> = vec![
            "vkrunner".into(),
            "-dB".into(), "12".into(),
            "--image".into(), "screenshot.png".into(),
            "--replace".into(), "bad=aĉa".into(),
            "--replace".into(), "good=bona".into(),
            "script.shader_test".into(),
        ];

        let options = parse_options(args.into_iter()).unwrap();

        assert!(matches!(&options.values[DISASM_OPTION], ArgumentValue::Flag));

        let ArgumentValue::Integer(binding) = &options.values[BINDING_OPTION]
        else { unreachable!(); };
        assert_eq!(*binding, 12);

        let ArgumentValue::Filename(image) = &options.values[IMAGE_OPTION]
        else { unreachable!(); };
        assert_eq!(image.to_str().unwrap(), "screenshot.png");

        let ArgumentValue::StringArray(values) = &options.values[REPLACE_OPTION]
        else { unreachable!(); };
        assert_eq!(
            values,
            &vec!["bad=aĉa".to_string(), "good=bona".to_string()]
        );

        assert_eq!(options.scripts.len(), 1);
        assert_eq!(options.scripts[0].to_str().unwrap(), "script.shader_test");
    }

    #[test]
    fn multi_arguments() {
        let args = vec![
            "vkrunner".into(),
            "-ib".into(), "image.png".into(), "buffer.raw".into(),
        ];

        let options = parse_options(args.into_iter()).unwrap();

        let ArgumentValue::Filename(image) = &options.values[IMAGE_OPTION]
        else { unreachable!(); };
        assert_eq!(image.to_str().unwrap(), "image.png");

        let ArgumentValue::Filename(image) = &options.values[BUFFER_OPTION]
        else { unreachable!(); };
        assert_eq!(image.to_str().unwrap(), "buffer.raw");
    }

    #[test]
    fn unknown_option() {
        let args = vec!["vkrunner".into(), "--bad-option".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Unknown option: --bad-option");

        let args = vec!["vkrunner".into(), "-d?".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Unknown option: ?");

        let args = vec!["vkrunner".into(), "-".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Unknown option: -");
    }

    #[test]
    fn trailing_arguments() {
        let args = vec![
            "vkrunner".into(),
            "-i".into(), "image.png".into(),
            "--".into(),
            "-i.shader_test".into(),
        ];

        let options = parse_options(args.into_iter()).unwrap();

        assert_eq!(options.scripts.len(), 1);
        assert_eq!(options.scripts[0].to_str().unwrap(), "-i.shader_test");
    }

    #[test]
    fn missing_argument() {
        let args = vec!["vkrunner".into(), "--buffer".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Option --buffer requires an argument");

        let args = vec!["vkrunner".into(), "-D".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Option --replace requires an argument");

        let args = vec!["vkrunner".into(), "-B".into()].into_iter();
        let error = parse_options(args).unwrap_err();
        assert_eq!(&error.to_string(), "Option --binding requires an argument");
    }

    #[test]
    #[cfg(unix)]
    fn invalid_utf8() {
        use std::os::unix::ffi::OsStrExt;
        use std::ffi::OsStr;

        let args = vec![
            "vkrunner".into(),
            "-D".into(),
            OsStr::from_bytes(b"buffer-\x80.raw").into(),
        ].into_iter();

        let error = parse_options(args).unwrap_err();
        assert_eq!(
            &error.to_string(),
            "Invalid UTF-8 in argument to --replace",
        );

        let args = vec![
            "vkrunner".into(),
            "-B".into(),
            OsStr::from_bytes(b"TWELVE-\x80").into(),
        ].into_iter();

        let error = parse_options(args).unwrap_err();
        assert_eq!(
            &error.to_string(),
            "Invalid UTF-8 in argument to --binding",
        );
    }

    #[test]
    fn invalid_integer() {
        use std::os::unix::ffi::OsStrExt;
        use std::ffi::OsStr;

        let args = vec![
            "vkrunner".into(),
            "-B".into(),
            OsStr::from_bytes(b"twelve").into(),
        ].into_iter();

        let error = parse_options(args).unwrap_err();
        assert_eq!(
            &error.to_string(),
            "Invalid integer: twelve",
        );
    }
}

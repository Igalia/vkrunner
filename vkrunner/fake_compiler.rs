// vkrunner
//
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

//! This program is just for the unit tests. It makes a fake compiler
//! so we can get code coverage in compiler.rs without depending a
//! real GLSL compiler.

use std::io::{Write, BufWriter, BufReader, BufRead};
use std::fs::File;

fn copy_inputs(inputs: &[String], output: &str) {
    let mut output = BufWriter::new(File::create(output).unwrap());

    for input in inputs {
        let input = File::open(input).unwrap();

        for line in BufReader::new(input).lines() {
            for byte in line.unwrap().split_whitespace() {
                let byte_array = [u8::from_str_radix(byte, 16).unwrap()];
                output.write_all(&byte_array).unwrap();
            }
        }
    }
}

fn main() {
    let mut args = std::env::args();
    let mut inputs = Vec::new();
    let mut output = None;

    // Skip program name
    args.next().unwrap();

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--quiet" => println!("quiet"),
            "-V" => println!("vulkan_spirv"),
            "--target-env" => println!("target_env: {}", args.next().unwrap()),
            "-S" => println!("stage: {}", args.next().unwrap()),
            "-o" => output = Some(args.next().unwrap()),
            other_arg if other_arg.starts_with("-") => {
                unreachable!("unexpected arg: {}", other_arg);
            },
            input_file => inputs.push(input_file.to_owned()),
        }
    }

    match output {
        None => {
            // Pretend to output the disassembly
            println!("disassembly");
        },
        Some(output) => copy_inputs(&inputs, &output),
    }
}

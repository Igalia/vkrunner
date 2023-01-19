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

use crate::source;
use std::io;
use std::io::BufRead;
use std::fmt;
use std::fs;

// Encapsulates the two possible buf readers (either from a file or
// from a string) that the stream will use. I don’t think we can put
// this behind a boxed trait object because we can’t tell Rust that
// the box won’t outlive the Stream object.
#[derive(Debug)]
enum Reader<'a> {
    File(io::BufReader<fs::File>),
    String(io::BufReader<&'a [u8]>),
}

/// A struct used to read lines from a [Source]. This will handle
/// concatening lines that end with the line continuation character
/// (`\`) and the token replacements.
#[derive(Debug)]
pub struct Stream<'a> {
    source: &'a source::Source,
    reader: Reader<'a>,

    line_num: usize,
    next_line_num: usize,
    reached_eof: bool,
}

/// An error that can be returned from [Stream::read_line] or [Stream::new].
#[derive(Debug)]
pub enum StreamError {
    IoError(io::Error),
    /// A token replacement causes an infinite loop of replacements to
    /// occur.
    TokenReplacementLoop,
}

impl fmt::Display for StreamError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            StreamError::IoError(e) => e.fmt(f),
            StreamError::TokenReplacementLoop => {
                write!(f, "The token replacements cause an infinite loop")
            },
        }
    }
}

impl std::convert::From<io::Error> for StreamError {
    fn from(value: io::Error) -> StreamError {
        StreamError::IoError(value)
    }
}

impl<'a> Stream<'a> {
    /// Constructs a new [Stream] that will read lines from the given
    /// [Source]. The construction can fail if the stream is a file
    /// source and opening the file fails. In that case the returned
    /// [StreamError] will contain a [std::io::Error].
    pub fn new(source: &source::Source) -> Result<Stream, StreamError> {
        let reader = match source.data() {
            source::Data::File { filename } => {
                let file = fs::File::open(filename)?;
                Reader::File(io::BufReader::new(file))
            },
            source::Data::String { source } => {
                Reader::String(io::BufReader::new(source.as_bytes()))
            },
        };

        Ok(Stream {
            source,
            reader,
            line_num: 0,
            next_line_num: 1,
            reached_eof: false,
        })
    }

    /// Read a line from the stream and append it to the given String.
    /// This will handle the line continuation characters (`\`) and
    /// the token replacements set on the [Source].
    ///
    /// The length of the data appended to the string is returned. If
    /// the end of the source is reached then it will return 0.
    pub fn read_line(
        &mut self,
        line: &mut String
    ) -> Result<usize, StreamError> {
        let start_length = line.len();

        self.line_num = self.next_line_num;

        while !self.reached_eof {
            let length = match &mut self.reader {
                Reader::File(r) => r.read_line(line)?,
                Reader::String(r) => r.read_line(line)?,
            };

            if length == 0 {
                self.reached_eof = true;
                break;
            }

            self.next_line_num += 1;

            if length >= 2 {
                if line.ends_with("\\\n") {
                    line.truncate(line.len() - 2);
                    continue;
                } else if length >= 3 && line.ends_with("\\\r\n") {
                    line.truncate(line.len() - 3);
                    continue;
                }
            }

            break;
        }

        self.process_token_replacements(line, start_length)?;

        Ok(line.len() - start_length)
    }

    /// Returns the line number in the source data of the start of the
    /// last line that was returned by [read_line]. For example, if
    /// the source file is like this:
    ///
    /// ```
    /// line one \
    /// continuation of line one.
    /// line two \
    /// continuation of line two.
    /// ```
    ///
    /// then the second time [read_line] is called it will append
    /// “line two continuation of line two” and [line_num] will report
    /// 3 because the line starts on the third line of the source
    /// data.
    pub fn line_num(&self) -> usize {
        self.line_num
    }

    fn process_token_replacements(
        &self,
        line: &mut String,
        start_pos: usize
    ) -> Result<(), StreamError> {
        let mut count = 0usize;

        // Loop through each valid position in the line string. We
        // can’t safely use an iterator because we’re going to modify
        // the string as we iterate
        let mut pos = start_pos;

        while pos < line.len() {
            'token_loop: loop {
                for token_replacement in self.source.token_replacements() {
                    if line[pos..].starts_with(&token_replacement.token) {
                        count += 1;

                        // If we’ve replaced at least 1000 tokens then
                        // something has probably gone wrong and this
                        // is never going to finish.
                        if count >= 1000 {
                            return Err(StreamError::TokenReplacementLoop);
                        }

                        line.replace_range(
                            pos..pos + token_replacement.token.len(),
                            &token_replacement.replacement,
                        );

                        // Start looking for tokens from the start of
                        // the list in case the replacement contains
                        // one of the earlier tokens
                        continue 'token_loop;
                    }
                }

                break 'token_loop;
            }

            // Move to the next character. It would be nice if we
            // could do this by just looking at the first byte of the
            // UTF-8 sequence but there doesn’t seem to be a handy
            // Rust API for that.
            pos += line[pos..].chars().next().unwrap().len_utf8();
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_line_continuation() {
        let source = source::Source::from_string(
            "line one \\\n\
             more line one\n\
             line three \\\n\
             is really long \\\n\
             \\\n\
             and even has blank lines".to_string()
        );
        let mut stream = Stream::new(&source).unwrap();

        let mut line = String::new();
        assert_eq!(stream.read_line(&mut line).unwrap(), 23);
        assert_eq!(line, "line one more line one\n");
        assert_eq!(stream.line_num(), 1);

        assert_eq!(stream.read_line(&mut line).unwrap(), 50);
        assert_eq!(
            &line[23..],
            "line three is really long and even has blank lines",
        );
        assert_eq!(stream.line_num(), 3);

        // The line can also be continued if it has Windows-style line endings
        let source = source::Source::from_string(
            "line one \\\r\n\
             more line one\n\
             line three \r what".to_string()
        );
        let mut stream = Stream::new(&source).unwrap();
        let mut line = String::new();

        assert_eq!(stream.read_line(&mut line).unwrap(), 23);
        assert_eq!(line, "line one more line one\n");
        assert_eq!(stream.line_num(), 1);

        line.clear();
        assert_eq!(stream.read_line(&mut line).unwrap(), 17);
        assert_eq!(line, "line three \r what");
        assert_eq!(stream.line_num(), 3);

        // Backslashes in the middle of the string should just be left alone
        let source = source::Source::from_string(
            "I am happy \\o//".to_string()
        );
        let mut stream = Stream::new(&source).unwrap();
        let mut line = String::new();
        assert_eq!(stream.read_line(&mut line).unwrap(), 15);
        assert_eq!(line, "I am happy \\o//");
    }

    #[test]
    fn test_missing_file() {
        let source = source::Source::from_file(
            "this-file-does-not-exist".to_string()
        );
        let e = Stream::new(&source).unwrap_err();
        match e {
            StreamError::IoError(e) => {
                assert_eq!(e.kind(), io::ErrorKind::NotFound);
            },
            _ => unreachable!("expected StreamError::IoError, got: {}", e),
        }
    }

    fn run_test_file_source(filename: String) {
        fs::write(&filename, "my source code").unwrap();

        let source = source::Source::from_file(filename);
        let mut stream = Stream::new(&source).unwrap();
        let mut line = String::new();
        assert_eq!(stream.read_line(&mut line).unwrap(), 14);
        assert_eq!(line, "my source code");

        // EOF should return 0
        assert_eq!(stream.read_line(&mut line).unwrap(), 0);
        // It should also work a second time
        assert_eq!(stream.read_line(&mut line).unwrap(), 0);
    }

    #[test]
    fn test_file_source() {
        let mut filename = std::env::temp_dir();
        filename.push("vkrunner-test-file-source");
        let filename_str = filename.to_str().unwrap().to_owned();

        // Catch the unwind to try to remove the file that we created
        // if the test fails
        let r = std::panic::catch_unwind(
            move || run_test_file_source(filename_str)
        );

        if let Err(e) = fs::remove_file(filename) {
            assert_eq!(e.kind(), io::ErrorKind::NotFound);
        }

        if let Err(e) = r {
            std::panic::resume_unwind(e);
        }
    }

    macro_rules! replace_tokens {
        ($source:expr, $expected:expr, $($token:expr, $replacement:expr),*) => {
            let mut source = source::Source::from_string($source.to_owned());

            $(
                {
                    source.add_token_replacement(
                        $token.to_owned(),
                        $replacement.to_owned()
                    );
                }
            )*;

            let mut stream = Stream::new(&source).unwrap();
            let mut line = String::new();

            assert_eq!(stream.read_line(&mut line).unwrap(), $expected.len());
            assert_eq!(line, $expected);
        };
    }

    #[test]
    fn test_token_replacements() {
        // Simple replacement
        replace_tokens!(
            "one two",
            "1 2",
            "one", "1",
            "two", "2"
        );
        // Line continuation within a replacement
        replace_tokens!(
            "tok\\\n\
             ens are neat",
            "replacements are neat",
            "token",
            "replacement"
        );
        // Chain of replacements
        replace_tokens!(
            "I like this",
            "I like tomatoes",
            "this", "thatthing",
            "that", "t",
            "thing", "omatoes"
        );

        let mut source = source::Source::from_string(
            "Infinite recursion!".to_string()
        );
        source.add_token_replacement(
            "recursion".to_string(),
            "deeper".to_string(),
        );
        source.add_token_replacement(
            "deeper".to_string(),
            "keep-going".to_string(),
        );
        source.add_token_replacement(
            "keep-going".to_string(),
            "recursion".to_string(),
        );
        let mut stream = Stream::new(&source).unwrap();
        let mut line = String::new();
        let e = stream.read_line(&mut line).unwrap_err();
        assert!(matches!(e, StreamError::TokenReplacementLoop));
        assert_eq!(
            e.to_string(),
            "The token replacements cause an infinite loop"
        );

        // Try an empty token
        let mut source = source::Source::from_string(
            "Infinite recursion!".to_string()
        );
        source.add_token_replacement(
            "".to_string(),
            "ever longer".to_string(),
        );
        let mut stream = Stream::new(&source).unwrap();
        let mut line = String::new();
        let e = stream.read_line(&mut line).unwrap_err();
        assert!(matches!(e, StreamError::TokenReplacementLoop));
    }
}

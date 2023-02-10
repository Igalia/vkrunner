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

use std::ffi::{c_char, c_void};
use std::fmt;
use std::io;
use std::str;

/// An object that log messages can be written to. Normally this will
/// just write the messages to standard out, but an application can
/// configure a C callback to receive the messages by tweaking the
/// [Config](crate::config::Config). The struct implements `Write` so
/// it can be used with macros like [write!](std::write).
#[derive(Debug)]
pub struct Logger {
    callback: Option<WriteCallback>,
    user_data: *mut c_void,

    // The data is collected into this buffer until we have a complete
    // line to send to the callback.
    buf: Vec<u8>,

    // True if the any data was added from a u8 slice so it might not
    // be valid UTF-8.
    maybe_invalid_utf8: bool,
}

/// A callback to use to write the data to instead of writing to
/// stdout. This will be called one line at a time. Each line will be
/// terminated the C null terminator but no newlines. It will always
/// be valid UTF-8.
pub type WriteCallback = extern "C" fn(
    message: *const c_char,
    user_data: *mut c_void,
);

impl Logger {
    /// Construct a new logger that will write to the given callback.
    /// If the callback is `None` then the log will go to the stdout
    /// instead. `user_data` will be passed to the callback. You can
    /// pass [`ptr::null_mut()`](std::ptr::null_mut) if there is no
    /// callback.
    pub fn new(
        callback: Option<WriteCallback>,
        user_data: *mut c_void
    ) -> Logger {
        Logger {
            callback,
            user_data,
            maybe_invalid_utf8: false,

            buf: Vec::new(),
        }
    }

    fn send_range(&mut self, start: usize, end: usize) {
        if self.maybe_invalid_utf8 {
            let mut pos = start;

            loop {
                match str::from_utf8(&self.buf[pos..end]) {
                    Ok(_) => break,
                    Err(e) => {
                        // Replace the offending byte with a question
                        // mark. This should result in valid UTF-8
                        // without having to move the bytes around.
                        self.buf[pos + e.valid_up_to()] = b'?';
                        pos += e.valid_up_to() + 1;
                    },
                }
            }
        }

        match self.callback {
            Some(callback) => {
                // Add the C null terminator. This method should be
                // called knowing that the zero will be set at `end`
                // so it should have ensured the buffer is large
                // enough.
                self.buf[end] = 0;

                callback(
                    self.buf[start..end + 1].as_ptr().cast(),
                    self.user_data
                );
            },
            None => {
                // SAFETY: We just ensured that the range is valid
                // UTF-8 above.
                let s = unsafe {
                    str::from_utf8_unchecked(&self.buf[start..end])
                };
                println!("{}", s);
            },
        }
    }

    fn flush_lines(&mut self) {
        let mut pos = 0;

        while let Some(line_len) = self.buf[pos..]
            .into_iter()
            .position(|&c| c == b'\n')
        {
            self.send_range(pos, pos + line_len);
            pos += line_len + 1;
        }

        // Remove the lines that we successfully processed
        self.buf.drain(0..pos);

        if self.buf.is_empty() {
            self.maybe_invalid_utf8 = false;
        }
    }
}

impl io::Write for Logger {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if !buf.is_empty() {
            self.maybe_invalid_utf8 = true;
            self.buf.extend_from_slice(buf);
            self.flush_lines();
        }

        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        if !self.buf.is_empty() {
            let old_len = self.buf.len();

            // Make sure there is enough space for the null terminator
            // to be added
            self.buf.push(0);

            self.send_range(0, old_len);
            self.buf.clear();
        }

        Ok(())
    }
}

impl fmt::Write for Logger {
    fn write_str(&mut self, s: &str) -> Result<(), fmt::Error> {
        self.buf.extend_from_slice(s.as_bytes());
        self.flush_lines();
        Ok(())
    }
}

#[macro_use]
mod macros {
    macro_rules! log {
        ($dst:expr, $($arg:tt)*) => {
            let _ = fmt::Write::write_fmt($dst, format_args!($($arg)*));
            let _ = fmt::Write::write_char($dst, '\n');
        };
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::ffi::CStr;

    struct TestLogger {
        logger: Logger,
        items: Vec<String>,
    }

    extern "C" fn log_item_cb(message: *const c_char, user_data: *mut c_void) {
        let logger = unsafe {
            &mut *(user_data as *mut TestLogger)
        };

        let message = unsafe { CStr::from_ptr(message.cast()) };
        logger.items.push(message.to_str().unwrap().to_string());
    }

    fn test_logger() -> Box<TestLogger> {
        // Need to box the logger so we can pass a pointer to it in
        // the logging callback
        let mut logger = Box::new(TestLogger {
            logger: Logger::new(None, std::ptr::null_mut()),
            items: Vec::new(),
        });

        logger.logger = Logger::new(
            Some(log_item_cb),
            logger.as_mut() as *mut TestLogger as *mut c_void,
        );

        logger
    }

    #[test]
    fn multiple_lines() {
        let mut logger = test_logger();

        log!(
            &mut logger.logger,
            "This is a line\n\
             This is another line.\n\
             This is followed by a number: {}",
            42,
        );

        assert_eq!(logger.items.len(), 3);
        assert_eq!(logger.items[0], "This is a line");
        assert_eq!(logger.items[1], "This is another line.");
        assert_eq!(logger.items[2], "This is followed by a number: 42");
    }

    #[test]
    fn split_line() {
        let mut logger = test_logger();

        use std::fmt::Write;

        write!(&mut logger.logger, "Part of first line ").unwrap();
        assert_eq!(logger.items.len(), 0);
        write!(
            &mut logger.logger,
            "next part of first line\nSecond line\n"
        ).unwrap();
        assert_eq!(logger.items.len(), 2);
        assert_eq!(
            logger.items[0],
            "Part of first line next part of first line",
        );
        assert_eq!(logger.items[1], "Second line");
    }

    #[test]
    fn bad_utf8() {
        let mut logger = test_logger();

        use std::io::Write;

        logger.logger.write(
            b"\xc4u ne mankas bajtoj \xc4\x89i tie \xe2\n"
        ).unwrap();
        assert_eq!(logger.items.len(), 1);
        assert_eq!(logger.items[0], "?u ne mankas bajtoj ĉi tie ?");
    }

    #[test]
    fn flush() {
        let mut logger = test_logger();

        use std::fmt::Write;

        write!(&mut logger.logger, "One line\nUnterminated line").unwrap();
        assert_eq!(logger.items.len(), 1);
        assert_eq!(logger.items[0], "One line");

        io::Write::flush(&mut logger.logger).unwrap();

        assert_eq!(logger.items.len(), 2);
        assert_eq!(logger.items[1], "Unterminated line");

        assert!(logger.logger.buf.is_empty());

        io::Write::flush(&mut logger.logger).unwrap();

        assert_eq!(logger.items.len(), 2);
    }
}

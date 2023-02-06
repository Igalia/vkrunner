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

use std::path::{Path, PathBuf};
use std::fs::File;
use std::io;
use std::fmt;
use std::fmt::Write;

/// An error that can be returned from [TempFile::new]
#[derive(Debug)]
pub enum Error {
    /// All of the possible unique names were tried and all of the
    /// files exist. This should probably not really be possible.
    NoAvailableFilename,
    /// An I/O error other than `AlreadyExists` and `Interrupted`
    /// occurred while opening the file.
    IoError(io::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        match self {
            Error::NoAvailableFilename => {
                write!(f, "No available temporary filenames")
            },
            Error::IoError(e) => e.fmt(f),
        }
    }
}

/// Class to handle creating a tempory file and deleting it when
/// dropped.
///
/// The file will be created in the directory returned by
/// [std::env::temp_dir]. It will have a generated name beginning with
/// `vkrunner-` followed by an integer. The file will be opened in
/// write-only mode and the corresponding [File] object can be
/// accessed with [TempFile::file]. The file will be automatically
/// closed and deleted when the `TempFile` is dropped.
#[derive(Debug)]
pub struct TempFile {
    filename: PathBuf,
    file: Option<File>,
}

fn find_available_file(filename: &mut PathBuf) -> Result<File, Error> {
    let mut filename_part = String::new();

    for i in 0..=u64::MAX {
        filename_part.clear();
        write!(&mut filename_part, "vkrunner-{}", i).unwrap();

        filename.push(&filename_part);

        match File::options()
            .write(true)
            .read(true)
            .create_new(true)
            .open(&filename)
        {
            Ok(file) => return Ok(file),
            Err(e) => {
                if e.kind() != io::ErrorKind::AlreadyExists
                    && e.kind() != io::ErrorKind::Interrupted
                {
                    return Err(Error::IoError(e));
                }
                // Otherwise just try again with a new name
            },
        }

        filename.pop();
    }

    Err(Error::NoAvailableFilename)
}

impl TempFile {
    /// Creates a new temporary file or returns an error.
    pub fn new() -> Result<TempFile, Error> {
        let mut filename = std::env::temp_dir();
        let file = find_available_file(&mut filename)?;

        Ok(TempFile {
            filename,
            file: Some(file),
        })
    }

    /// Gets the [File] object. This can be `None` if
    /// [TempFile::close] has been called.
    pub fn file(&mut self) -> Option<&mut File> {
        self.file.as_mut()
    }

    /// Gets the filename of the temporary file.
    pub fn filename(&self) -> &Path {
        &self.filename
    }

    /// Closes the file. The file will still exist and won’t be
    /// deleted until the `TempFile` is dropped.
    pub fn close(&mut self) {
        self.file = None
    }
}

impl Drop for TempFile {
    fn drop(&mut self) {
        // Make sure the file is closed before trying to delete it
        self.file = None;
        // We can’t do anything if the remove fails so just ignore the error
        let _ = std::fs::remove_file(&self.filename);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::io::Seek;
    use std::io::{Write, Read};

    #[test]
    fn two_files() {
        let mut file_a = TempFile::new().unwrap();

        assert!(file_a.file().is_some());
        assert!(
            file_a
                .filename()
                .display()
                .to_string()
                .find("vkrunner-")
                .is_some()
        );
        assert!(file_a.filename().is_file());

        file_a.close();
        assert!(file_a.file().is_none());
        assert!(file_a.filename().is_file());

        let mut file_b = TempFile::new().unwrap();

        assert!(file_b.file().is_some());
        assert!(file_a.filename() != file_b.filename());
        assert!(file_b.filename().is_file());

        let file_a_filename = file_a.filename().to_owned();
        drop(file_a);
        assert!(!file_a_filename.is_file());

        assert!(file_b.filename.is_file());

        let file_b_filename = file_b.filename().to_owned();
        drop(file_b);
        assert!(!file_b_filename.is_file());
    }

    #[test]
    fn read() {
        let mut temp_file = TempFile::new().unwrap();

        temp_file.file().unwrap().write(b"hello").unwrap();

        temp_file.file().unwrap().rewind().unwrap();

        let mut contents = String::new();

        temp_file.file().unwrap().read_to_string(&mut contents).unwrap();

        assert_eq!(&contents, "hello");

        temp_file.close();

        let contents = std::fs::read_to_string(temp_file.filename()).unwrap();

        assert_eq!(contents, "hello");
    }
}

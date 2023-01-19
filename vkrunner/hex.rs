// Copyright (c) The Piglit project 2007
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

use crate::half_float;
use std::num::{ParseFloatError, ParseIntError};
use std::fmt;
use std::convert::From;

// Based on functions from piglit-util.c

#[derive(Debug)]
pub enum ParseError {
    Float(ParseFloatError),
    Int(ParseIntError),
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ParseError::Float(e) => e.fmt(f),
            ParseError::Int(e) => e.fmt(f),
        }
    }
}

impl From<ParseFloatError> for ParseError {
    fn from(e: ParseFloatError) -> ParseError {
        ParseError::Float(e)
    }
}

impl From<ParseIntError> for ParseError {
    fn from(e: ParseIntError) -> ParseError {
        ParseError::Int(e)
    }
}

// Similar to haystack.starts_with(needle) but the ASCII characters of
// haystack are converted to lower case before comparing. The needle
// value should already be in lowercase for this to work.
fn starts_with_ignore_case(haystack: &str, needle: &str) -> bool {
    let mut haystack_chars = haystack.chars();

    for needle_c in needle.chars() {
        match haystack_chars.next() {
            None => return false,
            Some(haystack_c) => if haystack_c.to_ascii_lowercase() != needle_c {
                return false;
            },
        }
    }

    true
}

// Checks whether the string slice starts with one of the special
// words allowed in a float string. If so it returns the length of the
// word, otherwise it returns None.
fn starts_with_float_special_word(s: &str) -> Option<usize> {
    for word in ["infinity", "inf", "nan"] {
        if starts_with_ignore_case(s, word) {
            return Some(word.len());
        }
    }

   None
}

// Checks whether the string starts with a valid Number part of a
// float. If so it returns the byte length, otherwise it returns None.
fn count_number(s: &str) -> Option<usize> {
    // Optional units
    let before_digits = count_digits(s);

    let num_end = &s[before_digits..];

    // Optional decimal
    let after_digits;
    let n_points;

    if let Some('.') = num_end.chars().next() {
        n_points = 1;
        let digits = &num_end[1..];
        after_digits = count_digits(digits);
    } else {
        n_points = 0;
        after_digits = 0;
    }

    // Either the units or the decimal must be present
    if before_digits > 0 || after_digits > 0 {
        Some(before_digits + n_points + after_digits)
    } else {
        None
    }
}

// Returns how many ASCII digits are at the start of the string
fn count_digits(s: &str) -> usize {
    s.chars().take_while(char::is_ascii_digit).count()
}

// Checks whether the strings starts with a valid Exp part of a float.
// If so it returns the byte length, otherwise it returns None.
fn count_exp(s: &str) -> Option<usize> {
    if let Some('E' | 'e') = s.chars().next() {
        let mut count = 1;

        if let Some('+' | '-') = s[count..].chars().next() {
            count += 1;
        }

        let digits = count_digits(&s[count..]);

        if digits > 0 {
            Some(count + digits)
        } else {
            None
        }
    } else {
        None
    }
}

// It looks like the rust float parsing functions don’t have an
// equivalent of the `endptr` argument to `strtod`. This function
// tries to work around that by extracting the float part and the rest
// of the string and returning the two string slices as a tuple. It also
// skips leading spaces.
fn split_parts(mut s: &str) -> (&str, &str) {
    // skip only ASCII spaces and tabs
    while !s.is_empty() &&
        (s.as_bytes()[0] == b' ' || s.as_bytes()[0] == b'\t')
    {
        s = &s[1..];
    }

    let mut split_point = 0;

    if s.starts_with("0x") {
        split_point =
            2
            + s[2..]
            .chars()
            .take_while(char::is_ascii_hexdigit)
            .count();
    } else {
        // Optional sign
        if let Some('-' | '+') = s[split_point..].chars().next() {
            split_point += 1;
        }

        if let Some(len) = starts_with_float_special_word(&s[split_point..]) {
            split_point += len;
        } else if let Some(len) = count_number(&s[split_point..]) {
            split_point += len;

            if let Some(len) = count_exp(&s[split_point..]) {
                split_point += len;
            }
        }
    }

    (&s[0..split_point], &s[split_point..])
}

// Wrapper for str.parse<f32> which allows using an exact hex bit
// pattern to generate a float value and ignores trailing data.
//
// If the parsing works, it will return a tuple with the floating
// point value and a string slice pointing to the rest of the string.
pub fn parse_f32(s: &str) -> Result<(f32, &str), ParseError> {
    let (s, tail) = split_parts(s);

    if s.starts_with("0x") {
        Ok((f32::from_bits(u32::from_str_radix(&s[2..], 16)?), tail))
    } else {
        Ok((s.parse::<f32>()?, tail))
    }
}

// Wrapper for str.parse<f64> which allows using an exact hex bit
// pattern to generate a float value and ignores trailing data.
//
// If the parsing works, it will return a tuple with the floating
// point value and a string slice pointing to the rest of the string.
pub fn parse_f64(s: &str) -> Result<(f64, &str), ParseError> {
    let (s, tail) = split_parts(s);

    if s.starts_with("0x") {
        Ok((f64::from_bits(u64::from_str_radix(&s[2..], 16)?), tail))
    } else {
        Ok((s.parse::<f64>()?, tail))
    }
}

// Wrapper for calling half_float::from_f32 on the result of parsing to the
// string to a float but that also allows specifying the value exactly
// as a hexadecimal number.
//
// If the parsing works, it will return a tuple with the half float
// value and a string slice pointing to the rest of the string.
pub fn parse_half_float(s: &str) -> Result<(u16, &str), ParseError> {
    let (s, tail) = split_parts(s);

    if s.starts_with("0x") {
        Ok((u16::from_str_radix(&s[2..], 16)?, tail))
    } else {
        Ok((half_float::from_f32(s.parse::<f32>()?), tail))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_split_parts() {
        // Skip spaces and tabs
        assert_eq!(split_parts("    0"), ("0", ""));
        assert_eq!(split_parts("  \t  0"), ("0", ""));
        // Don’t skip other whitespace characters
        assert_eq!(split_parts("  \n  0"), ("", "\n  0"));

        // Hex digits
        assert_eq!(split_parts("0xCafeCafeTEA"), ("0xCafeCafe", "TEA"));

        // Signs
        assert_eq!(split_parts("+42 cupcakes"), ("+42", " cupcakes"));
        assert_eq!(split_parts("-42 gerbils"), ("-42", " gerbils"));

        // Special words
        assert_eq!(split_parts("+infinity forever"), ("+infinity", " forever"));
        assert_eq!(split_parts("INf forever"), ("INf", " forever"));
        assert_eq!(split_parts("   -NaN fornever"), ("-NaN", " fornever"));
        assert_eq!(split_parts("infin"), ("inf", "in"));
        assert_eq!(split_parts("NaN12"), ("NaN", "12"));

        // Normal numbers
        assert_eq!(split_parts("12.2"), ("12.2", ""));
        assert_eq!(split_parts("-12.2"), ("-12.2", ""));
        assert_eq!(split_parts("12.2e6"), ("12.2e6", ""));
        assert_eq!(split_parts("12.2E6"), ("12.2E6", ""));
        assert_eq!(split_parts("12.E6"), ("12.E6", ""));
        assert_eq!(split_parts("12.E-6"), ("12.E-6", ""));
        assert_eq!(split_parts("12.E+6"), ("12.E+6", ""));
        assert_eq!(split_parts(".0"), (".0", ""));
        assert_eq!(split_parts("5."), ("5.", ""));
        assert_eq!(split_parts(".5e6"), (".5e6", ""));
        assert_eq!(split_parts("5.e6"), ("5.e6", ""));

        // At least one side of the decimal point must be specified
        assert_eq!(split_parts("+."), ("+", "."));

        // The exponent must have some digits
        assert_eq!(split_parts("12e"), ("12", "e"));
        assert_eq!(split_parts("12e-"), ("12", "e-"));

        // Can’t have the exponent on its own
        assert_eq!(split_parts("e12"), ("", "e12"));
    }

    fn float_matches<T: Into<f64>>(
        res: Result<(T, &str), ParseError>,
        expected_value: T,
        expected_tail: &str
    ) {
        let (value, tail) = res.unwrap();
        let value = value.into();
        let expected_value = expected_value.into();

        assert!((value - expected_value).abs() < 0.0001,
                "value={}, expected_value={}",
                value,
                expected_value);
        assert_eq!(expected_tail, tail);
    }

    #[test]
    fn test_parse_f32() {
        float_matches(parse_f32("1.0"), 1.0, "");
        float_matches(parse_f32("  \t 1.0"), 1.0, "");
        float_matches(parse_f32("-1.0"), -1.0, "");
        float_matches(parse_f32("+1.0"), 1.0, "");
        float_matches(parse_f32("42 monkies"), 42.0, " monkies");

        float_matches(parse_f32("0x0"), 0.0, "");
        assert_eq!(parse_f32("0x7f800000").unwrap().0, f32::INFINITY);

        assert_eq!(parse_f32("-inf").unwrap(), (-f32::INFINITY, ""));
        assert_eq!(parse_f32("infinity 12").unwrap(), (f32::INFINITY, " 12"));
        assert!(parse_f32("NaN").unwrap().0.is_nan());

        assert!(matches!(
            parse_f32(""),
            Err(ParseError::Float(ParseFloatError { .. }))
        ));

        assert!(matches!(
            parse_f32("0xaaaaaaaaa"),
            Err(ParseError::Int(ParseIntError { .. }))
        ));
    }

    #[test]
    fn test_parse_f64() {
        float_matches(parse_f64("1.0"), 1.0, "");
        float_matches(parse_f64("42 monkies"), 42.0, " monkies");

        float_matches(parse_f64("0x0"), 0.0, "");
        assert_eq!(parse_f64("0xfff0000000000000").unwrap().0, -f64::INFINITY);

        assert_eq!(parse_f64("-inf").unwrap(), (-f64::INFINITY, ""));
        assert_eq!(parse_f64("infinity 12").unwrap(), (f64::INFINITY, " 12"));
        assert!(parse_f64("NaN").unwrap().0.is_nan());

        assert!(matches!(
            parse_f64(""),
            Err(ParseError::Float(ParseFloatError { .. }))
        ));

        assert!(matches!(
            parse_f64("0xaaaaaaaaaaaaaaaaa"),
            Err(ParseError::Int(ParseIntError { .. }))
        ));
    }

    #[test]
    fn test_parse_half_float() {
        assert_eq!(parse_half_float("1.0").unwrap(), (0x3c00, ""));

        assert_eq!(parse_half_float("0x7bff").unwrap(), (0x7bff, ""));
        assert_eq!(parse_half_float("0x7c00").unwrap(), (0x7c00, ""));

        assert_eq!(
            parse_half_float("infinity 12").unwrap(),
            (0x7c00, " 12")
        );

        assert!(matches!(
            parse_half_float(""),
            Err(ParseError::Float(ParseFloatError { .. }))
        ));

        assert!(matches!(
            parse_half_float("0xffff1"),
            Err(ParseError::Int(ParseIntError { .. }))
        ));
    }
}

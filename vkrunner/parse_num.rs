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

use std::num::ParseIntError;
use std::fmt;

#[derive(Debug, PartialEq)]
pub enum ParseError {
    // The negative sign was used in an unsigned number type
    NegativeError,
    // A number that would be valid in an unsigned type overflows the
    // signed type
    SignedOverflow,
    // Any other error returned by the from_str_radix function
    Other(ParseIntError),
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ParseError::NegativeError => write!(f, "Number can’t be negated"),
            ParseError::SignedOverflow => {
                write!(f, "Number out of range for type")
            },
            ParseError::Other(e) => e.fmt(f),
        }
    }
}

impl From<ParseIntError> for ParseError {
    fn from(e: ParseIntError) -> ParseError {
        ParseError::Other(e)
    }
}

struct NumAnalysis<'a> {
    negative: bool,
    radix: u32,
    num_part: &'a str,
    tail: &'a str,
}

fn is_octal_prefix(prefix: &str) -> bool {
    prefix.starts_with("0")
        && prefix.len() > 1
        && prefix.as_bytes()[1].is_ascii_digit()
}

fn analyse_num(mut s: &str) -> NumAnalysis {
    // skip only ASCII spaces and tabs
    while !s.is_empty() &&
        (s.as_bytes()[0] == b' ' || s.as_bytes()[0] == b'\t')
    {
        s = &s[1..];
    }

    // Optional sign
    let (prefix_start, negative) = match s.chars().next() {
        Some('-') => (1, true),
        Some('+') => (1, false),
        _ => (0, false),
    };

    let prefix = &s[prefix_start..];

    let (radix, num_start) = if prefix.starts_with("0x") {
        (16, 2)
    } else if is_octal_prefix(prefix) {
        (8, 1)
    } else {
        (10, 0)
    };

    let num_start = &prefix[num_start..];

    let split_point = num_start
        .chars()
        .take_while(|c| c.is_ascii_hexdigit())
        .count();

    NumAnalysis {
        negative,
        radix,
        num_part: &num_start[0..split_point],
        tail: &num_start[split_point..],
    }
}

// Macro to create a function to parse an unsigned int type. These are
// needed because there doesn’t seem to be an equivalent to strtoul in
// rust where the radix can be zero. The regular parse function also
// doesn’t return a tail pointer.
macro_rules! parse_unsigned {
    ($func:ident, $t:ident) => {
        pub fn $func(s: &str) -> Result<($t, &str), ParseError> {
            let analysis = analyse_num(s);

            let num = $t::from_str_radix(analysis.num_part, analysis.radix)?;

            if analysis.negative {
                Err(ParseError::NegativeError)
            } else {
                Ok((num, analysis.tail))
            }
        }
    }
}

// Macro to create a function to parse a signed int type. These are
// needed because there doesn’t seem to be an equivalent to strtol in
// rust where the radix can be zero. The regular parse function also
// doesn’t return a tail pointer.
macro_rules! parse_signed {
    ($func:ident, $st:ident, $ut:ident) => {
        pub fn $func(s: &str) -> Result<($st, &str), ParseError> {
            let analysis = analyse_num(s);

            let num = $ut::from_str_radix(analysis.num_part, analysis.radix)?;

            if analysis.negative {
                if num > $st::MAX as $ut + 1 {
                    Err(ParseError::SignedOverflow)
                } else {
                    // We have an unsigned value that we need to
                    // negate. We can’t just cast it to signed and
                    // then negate it because the MIN value is not
                    // representable as a positive value in the signed
                    // type. Instead we do the trick of !x+1 to
                    // negate while staying in the unsigned type.
                    Ok(((!num).wrapping_add(1) as $st, analysis.tail))
                }
            } else {
                if num > $st::MAX as $ut {
                    Err(ParseError::SignedOverflow)
                } else {
                    Ok((num as $st, analysis.tail))
                }
            }
        }
    }
}

parse_unsigned!(parse_u64, u64);
parse_unsigned!(parse_u32, u32);
parse_unsigned!(parse_u16, u16);
parse_unsigned!(parse_u8, u8);
parse_signed!(parse_i64, i64, u64);
parse_signed!(parse_i32, i32, u32);
parse_signed!(parse_i16, i16, u16);
parse_signed!(parse_i8, i8, u8);

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_unsigned() {
        assert_eq!(
            parse_u64(&u64::MAX.to_string()).unwrap(),
            (u64::MAX, ""),
        );
        assert_eq!(
            parse_u32(&u32::MAX.to_string()).unwrap(),
            (u32::MAX, ""),
        );
        assert_eq!(
            parse_u16(&u16::MAX.to_string()).unwrap(),
            (u16::MAX, ""),
        );
        assert_eq!(
            parse_u8(&u8::MAX.to_string()).unwrap(),
            (u8::MAX, ""),
        );

        assert!(matches!(parse_u8("-1"), Err(ParseError::NegativeError)));
        assert!(matches!(parse_u8("-0"), Err(ParseError::NegativeError)));

        assert!(matches!(
            parse_u8("256"),
            Err(ParseError::Other(ParseIntError { .. })),
        ));

        assert_eq!(
            parse_i8("  0 12"),
            Ok((0, " 12")),
        );

        assert_eq!(
            parse_u8("   0x42  after"),
            Ok((66, "  after")),
        );
        assert_eq!(
            parse_u32("   0xaBCdef01  after"),
            Ok((0xabcdef01, "  after")),
        );
        assert_eq!(
            parse_u32("   0xffgoat"),
            Ok((255, "goat")),
        );
        assert_eq!(
            parse_u32(" \t  0100  after"),
            Ok((64, "  after")),
        );
    }

    #[test]
    fn test_signed() {
        assert_eq!(
            parse_i64(&i64::MAX.to_string()).unwrap(),
            (i64::MAX, ""),
        );
        assert_eq!(
            parse_i32(&i32::MAX.to_string()).unwrap(),
            (i32::MAX, ""),
        );
        assert_eq!(
            parse_i16(&i16::MAX.to_string()).unwrap(),
            (i16::MAX, ""),
        );
        assert_eq!(
            parse_i8(&i8::MAX.to_string()).unwrap(),
            (i8::MAX, ""),
        );
        assert_eq!(
            parse_i64(&i64::MIN.to_string()).unwrap(),
            (i64::MIN, ""),
        );
        assert_eq!(
            parse_i32(&i32::MIN.to_string()).unwrap(),
            (i32::MIN, ""),
        );
        assert_eq!(
            parse_i16(&i16::MIN.to_string()).unwrap(),
            (i16::MIN, ""),
        );
        assert_eq!(
            parse_i8(&i8::MIN.to_string()).unwrap(),
            (i8::MIN, ""),
        );

        assert_eq!(
            parse_i8("-0"),
            Ok((0, "")),
        );

        assert_eq!(
            parse_i8("  -0 0"),
            Ok((0, " 0")),
        );

        assert!(matches!(
            parse_i8("128"),
            Err(ParseError::SignedOverflow),
        ));
        assert!(matches!(
            parse_i8("-129"),
            Err(ParseError::SignedOverflow),
        ));

        assert_eq!(
            parse_i8("   0x42  after"),
            Ok((66, "  after")),
        );
        assert_eq!(
            parse_i32(" \t  0100  after"),
            Ok((64, "  after")),
        );
        assert_eq!(
            parse_i8("   -0x42  after"),
            Ok((-66, "  after")),
        );
        assert_eq!(
            parse_i32(" \t  -0100  after"),
            Ok((-64, "  after")),
        );
    }
}

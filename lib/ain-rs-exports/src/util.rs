use std::str;

use ain_evm::Result;
use ain_macros::ffi_fallible;

use crate::{ffi, prelude::*};

/// Validates a slice of bytes is valid UTF-8 and converts the bytes to a rust string slice.
#[ffi_fallible]
pub fn rs_try_from_utf8(string: &'static [u8]) -> Result<String> {
    let string = str::from_utf8(string).map_err(|_| "Error interpreting bytes, invalid UTF-8")?;
    Ok(string.to_string())
}

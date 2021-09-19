use std::{error, fmt};

use jsonrpc;
use serde_json;

/// The error type for errors produced in this library.
#[derive(Debug)]
pub enum Error {
    JsonRpc(jsonrpc::error::Error),
    Json(serde_json::error::Error),
}

impl From<jsonrpc::error::Error> for Error {
    fn from(e: jsonrpc::error::Error) -> Error {
        Error::JsonRpc(e)
    }
}

impl From<serde_json::error::Error> for Error {
    fn from(e: serde_json::error::Error) -> Error {
        Error::Json(e)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::JsonRpc(ref e) => match e {
                jsonrpc::error::Error::Rpc(e) => writeln!(f, r"{}", e.message),
                _ => write!(f, "JSON-RPC error: {:#?}", e),
            },
            Error::Json(ref e) => write!(f, "JSON error: {}", e),
        }
    }
}

impl error::Error for Error {
    fn description(&self) -> &str {
        "defi-cli error"
    }

    fn cause(&self) -> Option<&dyn error::Error> {
        match *self {
            Error::JsonRpc(ref e) => Some(e),
            Error::Json(ref e) => Some(e),
        }
    }
}

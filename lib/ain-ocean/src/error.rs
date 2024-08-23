use anyhow::format_err;
use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;
use serde_json::json;
// use thiserror::Error;
use snafu::{Location, Snafu};

#[derive(Debug)]
pub enum IndexAction {
    Index,
    Invalidate,
}

impl std::fmt::Display for IndexAction {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            IndexAction::Index => write!(f, "index"),
            IndexAction::Invalidate => write!(f, "invalidate"),
        }
    }
}

#[derive(Snafu, Debug)]
pub enum NotFoundKind {
    Proposal,
    Masternode,
    Scheme,
    Oracle,
    Token,
    PoolPair,
    RawTx,
}

#[derive(Debug, Snafu)]
pub enum Error {
    // #[snafu(display("{username} may not log in until they pay USD {amount:E}"))]
    AddressParseError {
        source: bitcoin::address::error::ParseError,
        location: Location,
    },
    BincodeError {
        source: bincode::Error,
        location: Location,
    },
    BitcoinAddressError {
        source: bitcoin::address::Error,
        location: Location,
    },
    BitcoinConsensusEncodeError {
        source: bitcoin::consensus::encode::Error,
        location: Location,
    },
    BitcoinHexToArrayError {
        source: bitcoin::hex::HexToArrayError,
        location: Location,
    },
    DecimalError {
        source: rust_decimal::Error,
        location: Location,
    },
    // #[snafu(context(false))]
    // #[snafu(transparent)]
    DBError {
        source: ain_db::DBError,
        location: Location,
    },
    FromHexError {
        source: hex::FromHexError,
        location: Location,
    },
    IOError {
        source: std::io::Error,
        location: Location,
    },
    JsonrpseeError {
        source: jsonrpsee::core::Error,
        location: Location,
    },
    ParseIntError {
        source: std::num::ParseIntError,
        location: Location,
    },
    ParseFloatError {
        source: std::num::ParseFloatError,
        location: Location,
    },
    RpcError {
        source: defichain_rpc::Error,
        location: Location,
    },
    SerdeJsonError {
        source: serde_json::Error,
        location: Location,
    },
    TryFromIntError {
        source: std::num::TryFromIntError,
        location: Location,
    },
    NotFound,
    NotFoundIndex,
    DecimalConversionError,
    OverflowError,
    UnderflowError,
    SecondaryIndex,
    UntradeableTokenError,
    ValidationError,
    BadRequest {
        message: String
    },
    // #[error("Unable to find {0:}")]
    // NotFound(NotFoundKind),
    // #[error(
    //     "attempting to sync: {0:?} but type: {1:?} with id: {2:?} cannot be found in the index"
    // )]
    // NotFoundIndex(IndexAction, String, String),
    Other {
        message: String,
    },
}

impl From<bincode::Error> for Error {
    fn from(error: bincode::Error) -> Self {
        Self::BincodeError { source: error, location: snafu::location!() }
    }
}

impl From<bitcoin::address::Error> for Error {
    fn from(error: bitcoin::address::Error) -> Self {
        Self::BitcoinAddressError { source: error, location: snafu::location!() }
    }
}

impl From<bitcoin::consensus::encode::Error> for Error {
    fn from(error: bitcoin::consensus::encode::Error) -> Self {
        Self::BitcoinConsensusEncodeError { source: error, location: snafu::location!() }
    }
}

impl From<bitcoin::hex::HexToArrayError> for Error {
    fn from(error: bitcoin::hex::HexToArrayError) -> Self {
        Self::BitcoinHexToArrayError { source: error, location: snafu::location!() }
    }
}

impl From<rust_decimal::Error> for Error {
    fn from(error: rust_decimal::Error ) -> Self {
        Self::DecimalError { source: error, location: snafu::location!() }
    }
}

impl From<ain_db::DBError> for Error {
    fn from(error: ain_db::DBError) -> Self {
        Self::DBError { source: error, location: snafu::location!() }
    }
}

impl From<hex::FromHexError> for Error {
    fn from(error: hex::FromHexError) -> Self {
        Self::FromHexError { source: error, location: snafu::location!() }
    }
}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Self::IOError { source: error, location: snafu::location!() }
    }
}

impl From<jsonrpsee::core::Error> for Error {
    fn from(error: jsonrpsee::core::Error) -> Self {
        Self::JsonrpseeError { source: error, location: snafu::location!() }
    }
}

impl From<std::num::ParseIntError> for Error {
    fn from(error: std::num::ParseIntError) -> Self {
        Self::ParseIntError { source: error, location: snafu::location!() }
    }
}

impl From<std::num::ParseFloatError> for Error {
    fn from(error: std::num::ParseFloatError) -> Self {
        Self::ParseFloatError { source: error, location: snafu::location!() }
    }
}

impl From<defichain_rpc::Error> for Error {
    fn from(error: defichain_rpc::Error) -> Self {
        Self::RpcError { source: error, location: snafu::location!() }
    }
}

impl From<serde_json::Error> for Error {
    fn from(error: serde_json::Error) -> Self {
        Self::SerdeJsonError { source: error, location: snafu::location!() }
    }
}

impl From<std::num::TryFromIntError> for Error {
    fn from(error: std::num::TryFromIntError) -> Self {
        Self::TryFromIntError { source: error, location: snafu::location!() }
    }
}

impl From<Box<dyn std::error::Error>> for Error {
    fn from(err: Box<dyn std::error::Error>) -> Self {
        Self::Other { message: err.to_string(), location: snafu::location!() }
    }
}

impl From<&str> for Error {
    fn from(s: &str) -> Self {
        Self::Other { message: s.to_string(), location: snafu::location!() }
    }
}

#[derive(Serialize)]
pub enum ErrorKind {
    NotFound,
    BadRequest,
    Unknown,
}

#[derive(Serialize)]
struct ApiErrorData {
    code: u16,
    r#type: ErrorKind,
    at: u128,
    message: String,
    url: String,
}
#[derive(Serialize)]
pub struct ApiError {
    error: ApiErrorData,
    #[serde(skip)]
    status: StatusCode,
}

impl ApiError {
    pub fn new(status: StatusCode, message: String, url: String) -> Self {
        let current_time = std::time::SystemTime::now();
        let at = current_time
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .as_millis();

        let r#type = match status {
            StatusCode::NOT_FOUND => ErrorKind::NotFound,
            StatusCode::BAD_REQUEST => ErrorKind::BadRequest,
            _ => ErrorKind::Unknown,
        };

        Self {
            error: ApiErrorData {
                r#type,
                code: status.as_u16(),
                message,
                url,
                at,
            },
            status,
        }
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let status = self.status;
        let body = Json(json!({
            "error": self.error
        }));
        (status, body).into_response()
    }
}

impl Error {
    pub fn into_code_and_message(self) -> (StatusCode, String) {
        let (code, reason) = match &self {
            // Error::RpcError(defichain_rpc::Error::JsonRpc(jsonrpc_async::error::Error::Rpc(e))) => {
            //     (
            //         StatusCode::NOT_FOUND,
            //         match e {
            //             e if e.message.contains("Cannot find existing loan scheme") => {
            //                 format!("{}", Error::NotFound(NotFoundKind::Scheme))
            //             }
            //             _ => e.message.to_string(),
            //         },
            //     )
            // }
            Error::NotFound => (StatusCode::NOT_FOUND, format!("{self}")),
            Error::BadRequest { msg } => (StatusCode::BAD_REQUEST, msg.clone()),
            _ => (StatusCode::INTERNAL_SERVER_ERROR, self.to_string()),
        };
        (code, reason)
    }
}

// impl From<Box<dyn std::error::Error>> for Error {
//     fn from(err: Box<dyn std::error::Error>) -> Error {
//         Error::Other(format_err!("{err}"))
//     }
// }

// impl From<&str> for Error {
//     fn from(s: &str) -> Self {
//         Error::Other(format_err!("{s}"))
//     }
// }

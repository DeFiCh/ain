use std::{
    backtrace::Backtrace,
    fmt,
    num::{ParseFloatError, ParseIntError},
};

use ain_db::DBError;
use anyhow::format_err;
use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use bitcoin::hex::HexToArrayError;
use serde::Serialize;
use serde_json::json;
use thiserror::Error;

#[derive(Debug)]
pub enum IndexAction {
    Index,
    Invalidate,
}

impl fmt::Display for IndexAction {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            IndexAction::Index => write!(f, "index"),
            IndexAction::Invalidate => write!(f, "invalidate"),
        }
    }
}

#[derive(Error, Debug)]
pub enum NotFoundKind {
    #[error("proposal")]
    Proposal,
    #[error("masternode")]
    Masternode,
    #[error("scheme")]
    Scheme,
    #[error("oracle")]
    Oracle,
    #[error("token")]
    Token,
    #[error("poolpair")]
    PoolPair,
    #[error("rawtx")]
    RawTx,
}

#[derive(Error, Debug)]
pub enum Error {
    #[error("Ocean: Bincode error: {0:?}")]
    BincodeError(#[from] bincode::Error),
    #[error("Ocean: HexToArrayError error: {0:?}")]
    HexToArrayError(#[from] HexToArrayError),
    #[error("Ocean: ParseIntError error: {0:?}")]
    ParseIntError(#[from] ParseIntError),
    #[error("Ocean: ParseFloatError error: {0:?}")]
    ParseFloatError(#[from] ParseFloatError),
    #[error("Ocean: DBError error: {0:?}")]
    DBError(#[from] DBError),
    #[error("Ocean: IO error: {0:?}")]
    IOError(#[from] std::io::Error),
    #[error("Ocean: FromHexError error: {0:?}")]
    FromHexError(#[from] hex::FromHexError),
    #[error("Ocean: Consensus encode error: {0:?}")]
    ConsensusEncodeError(#[from] bitcoin::consensus::encode::Error),
    #[error("Ocean: jsonrpsee error: {0:?}")]
    JsonrpseeError(#[from] jsonrpsee::core::Error),
    #[error("Ocean: serde_json error: {0:?}")]
    SerdeJSONError(#[from] serde_json::Error),
    #[error("Ocean: RPC error: {0:?}")]
    RpcError(#[from] defichain_rpc::Error),
    #[error("Unable to find {0:}")]
    NotFound(NotFoundKind),
    #[error(
        "attempting to sync: {0:?} but type: {1:?} with id: {2:?} cannot be found in the index"
    )]
    NotFoundIndex(IndexAction, String, String),
    #[error("Ocean: Decimal error: {0:?}")]
    DecimalError(#[from] rust_decimal::Error),
    #[error("Decimal conversion error")]
    DecimalConversionError,
    #[error("Ocean: Overflow error")]
    OverflowError,
    #[error("Ocean: Underflow error")]
    UnderflowError(Backtrace),
    #[error("Error fetching primary value")]
    SecondaryIndex,
    #[error("Token {0:?} is invalid as it is not tradeable")]
    UntradeableTokenError(String),
    #[error("Ocean: BitcoinAddressError: {0:?}")]
    BitcoinAddressError(#[from] bitcoin::address::Error),
    #[error("Ocean: TryFromIntError: {0:?}")]
    TryFromIntError(#[from] std::num::TryFromIntError),
    #[error("{0:}")]
    AddressParseError(#[from] bitcoin::address::error::ParseError),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
    #[error("Validation error: {0}")]
    ValidationError(String),
    #[error("{0}")]
    BadRequest(String),
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
            Error::RpcError(defichain_rpc::Error::JsonRpc(jsonrpc_async::error::Error::Rpc(e))) => {
                (
                    StatusCode::NOT_FOUND,
                    match e {
                        e if e.message.contains("Cannot find existing loan scheme") => {
                            format!("{}", Error::NotFound(NotFoundKind::Scheme))
                        }
                        _ => e.message.to_string(),
                    },
                )
            }
            Error::NotFound(_) => (StatusCode::NOT_FOUND, format!("{self}")),
            Error::BadRequest(msg) => (StatusCode::BAD_REQUEST, msg.clone()),
            _ => (StatusCode::INTERNAL_SERVER_ERROR, self.to_string()),
        };
        (code, reason)
    }
}

impl From<Box<dyn std::error::Error>> for Error {
    fn from(err: Box<dyn std::error::Error>) -> Error {
        Error::Other(format_err!("{err}"))
    }
}

impl From<&str> for Error {
    fn from(s: &str) -> Self {
        Error::Other(format_err!("{s}"))
    }
}

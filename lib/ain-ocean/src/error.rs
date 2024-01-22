use std::num::ParseIntError;

use ain_db::DBError;
use anyhow::format_err;
use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use bitcoin::hex::HexToArrayError;
use serde::Serialize;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum OceanError {
    #[error("Ocean: HexToArrayError error: {0:?}")]
    HexToArrayError(#[from] HexToArrayError),
    #[error("Ocean: ParseIntError error: {0:?}")]
    ParseIntError(#[from] ParseIntError),
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
    RpcError(#[from] bitcoincore_rpc::Error),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

#[derive(Serialize)]
pub enum ErrorType {
    NotFound,
    Unknown,
}

#[derive(Serialize)]
struct ApiErrorData {
    code: u16,
    r#type: ErrorType,
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
            StatusCode::NOT_FOUND => ErrorType::NotFound,
            _ => ErrorType::Unknown,
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
        let reason = Json(self);
        (status, reason).into_response()
    }
}

impl OceanError {
    pub fn into_code_and_message(self) -> (StatusCode, String) {
        let (code, reason) = match self {
            OceanError::RpcError(bitcoincore_rpc::Error::JsonRpc(jsonrpc::error::Error::Rpc(
                e,
            ))) => {
                println!("e : {:?}", e);

                (StatusCode::NOT_FOUND, format!(""))
            }
            _ => (StatusCode::INTERNAL_SERVER_ERROR, self.to_string()),
        };
        println!("reason : {:?}", reason);
        (code, reason)
    }
}

impl From<Box<dyn std::error::Error>> for OceanError {
    fn from(err: Box<dyn std::error::Error>) -> OceanError {
        OceanError::Other(format_err!("{err}"))
    }
}

impl From<&str> for OceanError {
    fn from(s: &str) -> Self {
        OceanError::Other(format_err!("{s}"))
    }
}

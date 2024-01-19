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
use tarpc::client::RpcError;
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
enum ErrorType {
    NotFound,
    Unknown,
}

#[derive(Serialize)]
pub struct ApiError {
    r#type: ErrorType,
    code: u16,
    at: u64,
    message: String,
    url: String,
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let reason = Json(self);
        (self.code, reason).into_response()
    }
}

impl OceanError {
    fn into_code_and_message(self) -> (StatusCode, String) {
        let code: StatusCode = match self {
            // OceanError::SomeError => StatusCode::SomeCode,
            _ => StatusCode::INTERNAL_SERVER_ERROR,
        };

        println!("into response : {}", self);

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

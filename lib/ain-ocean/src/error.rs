use std::num::ParseIntError;

use ain_db::DBError;
use anyhow::format_err;
use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
};
use bitcoin::hex::HexToArrayError;
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
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

impl IntoResponse for OceanError {
    fn into_response(self) -> Response {
        let code: StatusCode = match self {
            // OceanError::SomeError => StatusCode::SomeCode,
            _ => StatusCode::INTERNAL_SERVER_ERROR,
        };
        let reason = self.to_string();
        (code, reason).into_response()
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

use std::num::ParseIntError;

use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
};
use bitcoin::hex::HexToArrayError;
use thiserror::Error;
pub type OceanResult<T> = Result<T, OceanError>;
use anyhow::format_err;

#[derive(Error, Debug)]
pub enum OceanError {
    #[error("Ocean: HexToArrayError error: {0:?}")]
    HexToArrayError(#[from] HexToArrayError),
    #[error("Ocean: ParseIntError error: {0:?}")]
    ParseIntError(#[from] ParseIntError),
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

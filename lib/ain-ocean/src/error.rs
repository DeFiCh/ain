use axum::{http::StatusCode, response::{IntoResponse, Response}};
use thiserror::Error;

pub type OceanResult<T> = Result<T, OceanError>;

#[derive(Error, Debug)]
pub enum OceanError {
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

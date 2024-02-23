use axum::{
    async_trait,
    extract::{path::ErrorKind, rejection::PathRejection, FromRequestParts},
    http::{request::Parts, StatusCode},
};
use serde::{de::DeserializeOwned, Serialize};

use crate::error::ApiError;

// We define our own `Path` extractor that customizes the error from `axum::extract::Path`
#[derive(Debug)]
pub struct Path<T>(pub T);

#[async_trait]
impl<S, T> FromRequestParts<S> for Path<T>
where
    // these trait bounds are copied from `impl FromRequest for axum::extract::path::Path`
    T: DeserializeOwned + Send,
    S: Send + Sync,
{
    type Rejection = ApiError;

    async fn from_request_parts(parts: &mut Parts, state: &S) -> Result<Self, Self::Rejection> {
        match axum::extract::Path::<T>::from_request_parts(parts, state).await {
            Ok(value) => Ok(Self(value.0)),
            Err(rejection) => {
                let error = match rejection {
                    PathRejection::FailedToDeserializePathParams(inner) => {
                        let kind = inner.into_kind();
                        let error = match &kind {
                            ErrorKind::WrongNumberOfParameters { .. } => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                kind.to_string(),
                                parts.uri.to_string(),
                            ),

                            ErrorKind::ParseErrorAtKey { key, .. } => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                format!("key: {key}, {kind}"),
                                parts.uri.to_string(),
                            ),

                            ErrorKind::ParseErrorAtIndex { index, .. } => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                format!("index: {index}, {kind}"),
                                parts.uri.to_string(),
                            ),

                            ErrorKind::ParseError { .. } => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                kind.to_string(),
                                parts.uri.to_string(),
                            ),

                            ErrorKind::InvalidUtf8InPathParam { key } => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                format!("key: {key}, {kind}"),
                                parts.uri.to_string(),
                            ),

                            ErrorKind::UnsupportedType { .. } => {
                                // this error is caused by the programmer using an unsupported type
                                // (such as nested maps) so respond with `500` instead
                                ApiError::new(
                                    StatusCode::INTERNAL_SERVER_ERROR,
                                    kind.to_string(),
                                    parts.uri.to_string(),
                                )
                            }

                            ErrorKind::Message(msg) => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                msg.clone(),
                                parts.uri.to_string(),
                            ),

                            _ => ApiError::new(
                                StatusCode::BAD_REQUEST,
                                format!("Unhandled deserialization error: {kind}"),
                                parts.uri.to_string(),
                            ),
                        };

                        error
                    }
                    PathRejection::MissingPathParams(error) => ApiError::new(
                        StatusCode::INTERNAL_SERVER_ERROR,
                        error.to_string(),
                        parts.uri.to_string(),
                    ),
                    _ => ApiError::new(
                        StatusCode::INTERNAL_SERVER_ERROR,
                        format!("Unhandled path rejection: {rejection}"),
                        parts.uri.to_string(),
                    ),
                };

                Err(error)
            }
        }
    }
}

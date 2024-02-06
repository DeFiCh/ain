use axum::{
    async_trait,
    extract::FromRequestParts,
    http::{request::Parts, StatusCode},
};
use serde::{de::DeserializeOwned, Deserialize};
use serde_with::{serde_as, DisplayFromStr};

use crate::error::ApiError;

pub fn default_pagination_size() -> usize {
    30
}

#[serde_as]
#[derive(Deserialize, Default, Debug)]
pub struct PaginationQuery {
    #[serde_as(as = "DisplayFromStr")]
    #[serde(default = "default_pagination_size")]
    pub size: usize,
    pub next: Option<String>,
}

pub struct Query<T>(pub T);

#[async_trait]
impl<S, T> FromRequestParts<S> for Query<T>
where
    T: Default + DeserializeOwned,
    S: Send + Sync,
{
    type Rejection = ApiError;

    async fn from_request_parts(parts: &mut Parts, _state: &S) -> Result<Self, Self::Rejection> {
        let query = parts.uri.query().unwrap_or_default();

        match serde_urlencoded::from_str(query) {
            Ok(v) => Ok(Query(v)),
            Err(e) => Err(ApiError::new(
                StatusCode::BAD_REQUEST,
                format!("Invalid query parameter value for {query}. {e}"),
                parts.uri.to_string(),
            )),
        }
    }
}

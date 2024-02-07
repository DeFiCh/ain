use axum::{
    async_trait,
    extract::FromRequestParts,
    http::{request::Parts, StatusCode},
};
use serde::{
    de::{DeserializeOwned, Deserializer},
    Deserialize,
};
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
    #[serde(deserialize_with = "undefined_to_none")]
    pub next: Option<String>,
}

fn undefined_to_none<'de, D>(d: D) -> Result<Option<String>, D::Error>
where
    D: Deserializer<'de>,
{
    let v: Option<String> = Deserialize::deserialize(d)?;
    match v {
        Some(v) => if v.as_str() == "undefined" {
            Ok(None)
        } else {
            Ok(Some(v))
        },
        None => Ok(None)
    }
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

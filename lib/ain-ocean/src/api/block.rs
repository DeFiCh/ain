use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Path, routing::get, Extension, Router};
use bitcoin::BlockHash;
use serde::{Deserialize, Deserializer};

use super::response::{ApiPagedResponse, Response};
use crate::{
    api_query::{PaginationQuery, Query},
    error::ApiError,
    model::Block,
    repository::RepositoryOps,
    Result, Services,
};

pub enum HashOrHeight {
    Height(u32),
    Id(BlockHash),
}

impl<'de> Deserialize<'de> for HashOrHeight {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        if let Ok(height) = s.parse::<u32>() {
            Ok(HashOrHeight::Height(height))
        } else if let Ok(id) = s.parse::<BlockHash>() {
            Ok(HashOrHeight::Id(id))
        } else {
            Err(serde::de::Error::custom("Error parsing HashOrHeight"))
        }
    }
}

#[ocean_endpoint]
async fn list_blocks(
    Query(query): Query<PaginationQuery>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<ApiPagedResponse<Block>> {
    let blocks = services
        .block
        .by_height
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = services
                .block
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(blocks, query.size, |block| {
        block.clone().hash
    }))
}

#[ocean_endpoint]
async fn get_block(
    Path(id): Path<HashOrHeight>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<Response<Option<Block>>> {
    let block = if let Some(id) = match id {
        HashOrHeight::Height(n) => services.block.by_height.get(&n)?,
        HashOrHeight::Id(id) => Some(id),
    } {
        services.block.by_id.get(&id)?
    } else {
        None
    };

    Ok(Response::new(block))
}

async fn get_transactions(
    Path(hash): Path<BlockHash>,
    Extension(services): Extension<Arc<Services>>,
) -> String {
    format!("Transactions for block with hash {}", hash)
}

// Get highest indexed block
#[ocean_endpoint]
async fn get_highest(Extension(services): Extension<Arc<Services>>) -> Result<Response<Option<Block>>> {
    let block = services.block.by_height.get_highest()?;

    Ok(Response::new(block))
}

pub fn router(services: Arc<Services>) -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/highest", get(get_highest))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
        .layer(Extension(services))
}

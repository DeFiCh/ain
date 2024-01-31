use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Path, routing::get, Router};
use bitcoin::BlockHash;
use defichain_rpc::Client;
use serde::{Deserialize, Deserializer};

use super::response::ApiPagedResponse;
use crate::{
    api_query::{PaginationQuery, Query},
    error::ApiError,
    model::Block,
    repository::RepositoryOps,
    Result, SERVICES,
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
async fn list_blocks(Query(query): Query<PaginationQuery>) -> Result<ApiPagedResponse<Block>> {
    let blocks = SERVICES
        .block
        .by_height
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = SERVICES
                .block
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(blocks, query.size, |block| {
        block.clone().id
    }))
}

#[ocean_endpoint]
async fn get_block(Path(id): Path<HashOrHeight>) -> Result<Option<Block>> {
    let block = if let Some(id) = match id {
        HashOrHeight::Height(n) => SERVICES.block.by_height.get(&n)?,
        HashOrHeight::Id(id) => Some(id),
    } {
        SERVICES.block.by_id.get(&id)?
    } else {
        None
    };

    Ok(block)
}

async fn get_transactions(Path(hash): Path<BlockHash>) -> String {
    format!("Transactions for block with hash {}", hash)
}

// Get highest indexed block
#[ocean_endpoint]
async fn get_highest() -> Result<Option<Block>> {
    let block = SERVICES.block.by_height.get_highest()?;

    Ok(block)
}

pub fn router(state: Arc<Client>) -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/highest", get(get_highest))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
}

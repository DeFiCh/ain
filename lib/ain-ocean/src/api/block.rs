use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use bitcoin::BlockHash;
use defichain_rpc::{Client, RpcApi};

use super::response::{ApiPagedResponse, Response};
use crate::{
    api_query::PaginationQuery, error::ApiError, model::Block, repository::RepositoryOps,
    storage::ocean_store, Result, SERVICES,
};

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
async fn get_block(Path(id): Path<BlockHash>) -> Result<Response<Option<Block>>> {
    let block = SERVICES.block.by_id.get(&id)?;

    Ok(Response::new(block))
}

async fn get_transactions(Path(hash): Path<BlockHash>) -> String {
    format!("Transactions for block with hash {}", hash)
}

// Get highest indexed block
#[ocean_endpoint]
async fn get_highest() -> Result<Response<Option<Block>>> {
    let block = SERVICES.block.by_height.get_highest()?;

    Ok(Response::new(block))
}

pub fn router(state: Arc<Client>) -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/highest", get(get_highest))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
}

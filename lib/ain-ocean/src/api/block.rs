use std::sync::Arc;

use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use bitcoin::BlockHash;
use jsonrpsee_http_client::{ClientT, HttpClient};

use crate::{
    api_paged_response::ApiPagedResponse, api_query::PaginationQuery, model::Block,
    repository::RepositoryOps, Result, SERVICES,
};

async fn list_blocks(
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<Block>>> {
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

    Ok(Json(ApiPagedResponse::of(blocks, query.size, |block| {
        block.clone().id
    })))
}

async fn get_block(Path(id): Path<BlockHash>) -> Result<Json<Option<Block>>> {
    let block = SERVICES.block.by_id.get(&id)?;

    Ok(Json(block))
}

async fn get_transactions(Path(hash): Path<BlockHash>) -> String {
    format!("Transactions for block with hash {}", hash)
}

pub fn router(state: Arc<HttpClient>) -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
}

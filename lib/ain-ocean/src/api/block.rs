use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use bitcoin::BlockHash;

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    error::OceanResult,
    SERVICES,
    repository::RepositoryOps,
    model::Block,
};

async fn list_blocks(
    Query(query): Query<PaginationQuery>,
) -> OceanResult<Json<ApiPagedResponse<Block>>> {
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
        .collect::<OceanResult<Vec<_>>>()?;

    Ok(Json(ApiPagedResponse::of(
        blocks,
        query.size,
        |block| block.clone().id,
    )))
}

async fn get_block(Path(id): Path<BlockHash>) -> OceanResult<Json<Option<Block>>> {
    let block = SERVICES
        .block
        .by_id
        .get(&id)?;

    Ok(Json(block))
}

async fn get_transactions(Path(hash): Path<BlockHash>) -> String {
    format!("Transactions for block with hash {}", hash)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
}

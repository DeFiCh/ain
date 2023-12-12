use axum::{
    debug_handler,
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    error::OceanResult,
};

#[derive(Deserialize)]
struct BlockId {
    id: String,
}

#[derive(Deserialize)]
struct BlockHash {
    hash: String,
}

#[debug_handler]
async fn list_blocks(Query(query): Query<PaginationQuery>) -> OceanResult<Json<ApiPagedResponse<Block>>> {
    // TODO(): query from lvldb.. or maybe pull from index
    let blocks = vec![
        Block { id: "0".into() },
        Block { id: "1".into() },
        Block { id: "2".into() },
    ];

    Ok(Json(ApiPagedResponse::of(blocks, query.size, |block| {
        block.clone().id
    })))
}

#[debug_handler]
async fn get_block(Path(BlockId { id }): Path<BlockId>) -> OceanResult<Json<Block>> {
    Ok(Json(Block {
        id,
    }))
}

async fn get_transactions(Path(BlockHash { hash }): Path<BlockHash>) -> String {
    format!("Transactions for block with hash {}", hash)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
}

#[derive(Clone, Debug, Serialize)]
#[serde(default)]
pub struct Block {
    id: String,
    // TODO(): type mapping
    // hash: H256,
    // previous_hash: H256,

    // height: u64,
    // version: u64,
    // time: u64, // ---------------| block time in seconds since epoch
    // median_time: u64, // --------| meidan time of the past 11 block timestamps

    // transaction_count: u64,

    // difficulty: u64,

    // masternode: H256,
    // minter: H256,
    // minter_block_count: u64,
    // reward: f64

    // state_modifier: H256,
    // merkle_root: H256,

    // size: u64,
    // size_stripped: u64,
    // weight: u64,
}

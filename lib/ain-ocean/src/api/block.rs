use axum::{debug_handler, extract::Path, Json, routing::get, Router};
use serde::{Deserialize, Serialize};

use crate::api_paged_response::ApiPagedResponse;

#[derive(Deserialize)]
struct BlockId {
    id: String,
}

#[derive(Deserialize)]
struct BlockHash {
    hash: String,
}

#[derive(Deserialize)]
pub struct ListBlocksRequest {
    pub size: usize,
    pub next: Option<String>
}

#[debug_handler]
async fn list_blocks(Json(req): Json<ListBlocksRequest>) -> Json<ApiPagedResponse<Block>> {
    // TODO(): query from db
    // db::block::list(req).await...
    let blocks = vec![
        Block { id: "0".into() },
        Block { id: "1".into() },
        Block { id: "2".into() },
    ];
    
    Json(ApiPagedResponse
        ::of(
            blocks,
            req.size, 
            |block| block.clone().id
        )
    )
}

async fn get_block(Path(BlockId { id }): Path<BlockId>) -> String {
    format!("Details of block with id {}", id)
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

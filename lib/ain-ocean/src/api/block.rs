use axum::{extract::Path, routing::get, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct BlockId {
    id: String,
}

#[derive(Deserialize)]
struct BlockHash {
    hash: String,
}

async fn list_blocks() -> String {
    "List of blocks".to_string()
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

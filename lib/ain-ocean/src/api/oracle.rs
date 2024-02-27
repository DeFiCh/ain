use std::sync::Arc;

use axum::{routing::get, Router};
use defichain_rpc::{Client, RpcApi};
use super::path::Path;

async fn list_oracles() -> String {
    "List of oracles".to_string()
}

async fn get_price_feed(Path((oracle_id, key)): Path<(String, String)>) -> String {
    format!("Price feed for oracle ID {} and key {}", oracle_id, key)
}

async fn get_oracle_by_address(Path(address): Path<String>) -> String {
    format!("Oracle details for address {}", address)
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_oracles))
        .route("/:oracleId/:key/feed", get(get_price_feed))
        .route("/:address", get(get_oracle_by_address))
}

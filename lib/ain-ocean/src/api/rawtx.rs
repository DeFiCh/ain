use std::sync::Arc;

use axum::{
    routing::{get, post},
    Router,
};
use defichain_rpc::{Client, RpcApi};
use super::path::Path;

async fn send_rawtx() -> String {
    "Sending raw transaction".to_string()
}

async fn test_rawtx() -> String {
    "Testing raw transaction".to_string()
}

async fn get_rawtx(Path(txid): Path<String>) -> String {
    format!("Details of raw transaction with txid {}", txid)
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/send", post(send_rawtx))
        .route("/test", get(test_rawtx))
        .route("/:txid", get(get_rawtx))
}

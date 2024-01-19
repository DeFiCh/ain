use std::sync::Arc;

use axum::{
    extract::Path,
    routing::{get, post},
    Router,
};
use jsonrpsee_http_client::{ClientT, HttpClient};

async fn send_rawtx() -> String {
    "Sending raw transaction".to_string()
}

async fn test_rawtx() -> String {
    "Testing raw transaction".to_string()
}

async fn get_rawtx(Path(txid): Path<String>) -> String {
    format!("Details of raw transaction with txid {}", txid)
}

pub fn router(state: Arc<HttpClient>) -> Router {
    Router::new()
        .route("/send", post(send_rawtx))
        .route("/test", get(test_rawtx))
        .route("/:txid", get(get_rawtx))
}

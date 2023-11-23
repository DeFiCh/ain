use axum::{extract::Path, routing::get, Router};

async fn list_masternodes() -> String {
    "List of masternodes".to_string()
}

async fn get_masternode(Path(masternode_id): Path<String>) -> String {
    format!("Details of masternode with id {}", masternode_id)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
}

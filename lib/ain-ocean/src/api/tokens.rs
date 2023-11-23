use axum::{extract::Path, routing::get, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct TokenId {
    id: u32,
}

async fn list_tokens() -> String {
    "List of tokens".to_string()
}

async fn get_token(Path(TokenId { id }): Path<TokenId>) -> String {
    format!("Details of token with id {}", id)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_tokens))
        .route("/:id", get(get_token))
}

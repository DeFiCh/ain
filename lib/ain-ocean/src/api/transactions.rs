use axum::{extract::Path, routing::get, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct TransactionId {
    id: String,
}

async fn get_transaction(Path(TransactionId { id }): Path<TransactionId>) -> String {
    format!("Details of transaction with id {}", id)
}

async fn get_vins(Path(TransactionId { id }): Path<TransactionId>) -> String {
    format!("Vins for transaction with id {}", id)
}

async fn get_vouts(Path(TransactionId { id }): Path<TransactionId>) -> String {
    format!("Vouts for transaction with id {}", id)
}

pub fn router() -> Router {
    Router::new()
        .route("/:id", get(get_transaction))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
}

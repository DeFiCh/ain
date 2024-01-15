use axum::{extract::Path, routing::get, Json, Router};
use bitcoin::Txid;
use serde::Deserialize;

use crate::{
    model::{Transaction, TransactionVin, TransactionVout},
    repository::RepositoryOps,
    Result, SERVICES,
};
#[derive(Deserialize)]
struct TransactionId {
    id: Txid,
}

async fn get_transaction(
    Path(TransactionId { id }): Path<TransactionId>,
) -> Result<Json<Option<Transaction>>> {
    format!("Details of transaction with id {}", id);
    let transactions = SERVICES.transaction.by_id.get(&id)?;
    Ok(Json(transactions))
}

async fn get_vins(
    Path(TransactionId { id }): Path<TransactionId>,
) -> Result<Json<Option<TransactionVin>>> {
    format!("Vins for transaction with id {}", id);
    let transaction_vin = SERVICES.transaction.vin_by_id.get(&id)?;
    Ok(Json(transaction_vin))
}

async fn get_vouts(
    Path(TransactionId { id }): Path<TransactionId>,
) -> Result<Json<Option<TransactionVout>>> {
    format!("Vouts for transaction with id {}", id);
    let transaction_vout = SERVICES.transaction.vout_by_id.get(&id)?;
    Ok(Json(transaction_vout))
}

pub fn router(state: Arc<Client>) -> Router {
    Router::new()
        .route("/:id", get(get_transaction))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
}

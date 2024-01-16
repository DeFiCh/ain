use std::str::FromStr;

use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use bitcoin::Txid;
use serde::Deserialize;

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    model::{Block, Transaction, TransactionVin, TransactionVout},
    repository::RepositoryOps,
    Result, SERVICES,
};

#[derive(Deserialize)]
struct TransactionId {
    id: Txid,
}

async fn list_transaction_by_block_hash(
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<Transaction>>> {
    let transaction_list = SERVICES
        .transaction
        .by_block_hash
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (_, trx) = item?;
            let tx_id = Txid::from_str(&trx.txid)?;
            let b = SERVICES
                .transaction
                .by_id
                .get(&tx_id)?
                .ok_or("Missing block index")?;
            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(Json(ApiPagedResponse::of(
        transaction_list,
        query.size,
        |transaction_list| transaction_list.id.clone(),
    )))
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
        .route("/:block_hash", get(list_transaction_by_block_hash))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
}

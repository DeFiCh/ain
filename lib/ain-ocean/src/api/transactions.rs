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
    model::{Transaction, TransactionVin, TransactionVout},
    repository::RepositoryOps,
    services, Result,
};

#[derive(Deserialize)]
struct TransactionId {
    id: Txid,
}

async fn get_transaction(
    Path(TransactionId { id }): Path<TransactionId>,
) -> Result<Json<Option<Transaction>>> {
    format!("Details of transaction with id {}", id);
    let transactions = services.transaction.by_id.get(&id)?;
    Ok(Json(transactions))
}

async fn get_vins(
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<TransactionVin>>> {
    let transaction_list = services
        .transaction
        .vin_by_id
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (txid, id) = item?;
            let b = services
                .transaction
                .vin_by_id
                .get(&txid)?
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

//get list of vout transaction, by passing id which contains txhash + vout_idx
async fn get_vouts(
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<TransactionVout>>> {
    let transaction_list = services
        .transaction
        .vout_by_id
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (txid, id) = item?;
            let b = services
                .transaction
                .vout_by_id
                .get(&txid)?
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

pub fn router(services: Arc<Services>) -> Router {
    Router::new()
        .route("/:id", get(get_transaction))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
}

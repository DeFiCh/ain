use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Json, Router,
};
use bitcoin::Txid;
use serde::Deserialize;

use super::{query::PaginationQuery, response::ApiPagedResponse, AppContext};
use crate::{
    api::response::Response,
    error::ApiError,
    model::{Transaction, TransactionVin, TransactionVout},
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

#[derive(Deserialize)]
struct TransactionId {
    id: Txid,
}

#[ocean_endpoint]
async fn get_transaction(
    Path(TransactionId { id }): Path<TransactionId>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<Transaction>>> {
    format!("Details of transaction with id {}", id);
    let transactions = ctx.services.transaction.by_id.get(&id)?;
    Ok(Response::new(transactions))
}

#[ocean_endpoint]
async fn get_vins(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVin>> {
    let transaction_list = ctx
        .services
        .transaction
        .vin_by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (txid, id) = item?;
            let b = ctx
                .services
                .transaction
                .vin_by_id
                .get(&txid)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        transaction_list,
        query.size,
        |transaction_list| transaction_list.id.clone(),
    ))
}

//get list of vout transaction, by passing id which contains txhash + vout_idx
#[ocean_endpoint]
async fn get_vouts(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVout>> {
    let transaction_list = ctx
        .services
        .transaction
        .vout_by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (txid, id) = item?;
            let b = ctx
                .services
                .transaction
                .vout_by_id
                .get(&txid)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        transaction_list,
        query.size,
        |transaction_list| transaction_list.txid.to_string(),
    ))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/:id", get(get_transaction))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
        .layer(Extension(ctx))
}

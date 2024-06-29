use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Query, routing::get, Extension, Router};
use bitcoin::Txid;
use serde::Deserialize;

use super::{path::Path, query::PaginationQuery, response::ApiPagedResponse, AppContext};
use crate::{
    api::{common::Paginate, response::Response},
    error::ApiError,
    model::{Transaction, TransactionVin, TransactionVout},
    repository::{InitialKeyProvider, RepositoryOps, TransactionVinRepository},
    storage::SortOrder,
    Result,
};

#[derive(Deserialize)]
pub struct TransactionId {
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
    Path(TransactionId { id }): Path<TransactionId>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVin>> {
    let next = query
        .next
        .clone()
        .unwrap_or(TransactionVinRepository::initial_key(id));

    let list = ctx
        .services
        .transaction
        .vin_by_id
        .list(Some(next), SortOrder::Descending)?
        .paginate(&query)
        .take_while(|item| match item {
            Ok((_, vin)) => vin.txid == id,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            Ok(v)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(list, query.size, |each| {
        each.id.clone()
    }))
}

//get list of vout transaction, by passing id which contains txhash + vout_idx
#[ocean_endpoint]
async fn get_vouts(
    Path(TransactionId { id }): Path<TransactionId>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVout>> {
    let next = query.next.as_deref().unwrap_or("0").parse::<usize>()?;

    let list = ctx
        .services
        .transaction
        .vout_by_id
        .list(Some((id, next)), SortOrder::Ascending)?
        .paginate(&query)
        .take_while(|item| match item {
            Ok((_, vout)) => vout.txid == id,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            Ok(v)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(list, query.size, |each| {
        each.n.to_string()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/:id", get(get_transaction))
        .route("/:id/vins", get(get_vins))
        .route("/:id/vouts", get(get_vouts))
        .layer(Extension(ctx))
}

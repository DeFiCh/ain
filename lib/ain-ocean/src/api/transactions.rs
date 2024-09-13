use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Query, routing::get, Extension, Router};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::{path::Path, query::PaginationQuery, response::ApiPagedResponse, AppContext};
use crate::{
    api::{common::Paginate, response::Response},
    error::ApiError,
    model::{
        Transaction, TransactionVin, TransactionVinType, TransactionVinVout, TransactionVout,
        TransactionVoutScript,
    },
    storage::{
        InitialKeyProvider, RepositoryOps, SortOrder, TransactionVin as TransactionVinStorage,
    },
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
    let transactions = ctx.services.transaction.by_id.get(&id)?;
    Ok(Response::new(transactions))
}

#[derive(Debug, Serialize)]
struct TransactionVinResponse {
    pub id: String,
    pub txid: Txid,
    pub coinbase: Option<String>,
    pub vout: Option<TransactionVinVout>,
    pub script: Option<String>,
    pub tx_in_witness: Option<Vec<String>>,
    pub sequence: i64,
}

impl From<TransactionVin> for TransactionVinResponse {
    fn from(v: TransactionVin) -> Self {
        let (id, coinbase) = match v.r#type {
            TransactionVinType::Coinbase(coinbase) => (format!("{}00", v.txid), Some(coinbase)),
            TransactionVinType::Standard((txid, vout)) => {
                (format!("{}{}{:x}", v.txid, txid, vout), None)
            }
        };
        Self {
            id,
            txid: v.txid,
            coinbase,
            vout: v.vout,
            script: v.script,
            tx_in_witness: v.tx_in_witness,
            sequence: v.sequence,
        }
    }
}

#[ocean_endpoint]
async fn get_vins(
    Path(TransactionId { id }): Path<TransactionId>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVinResponse>> {
    let next = query
        .next
        .clone()
        .unwrap_or_else(|| TransactionVinStorage::initial_key(id));

    let list = ctx
        .services
        .transaction
        .vin_by_id
        .list(Some(next), SortOrder::Descending)?
        .paginate(&query)
        .filter_map(|item| match item {
            Ok((_, vin)) if vin.txid == id => Some(Ok(TransactionVinResponse::from(vin))),
            Ok(_) => None,
            Err(e) => Some(Err(e.into())),
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(list, query.size, |each| {
        each.id.clone()
    }))
}

#[derive(Debug, Serialize)]
struct TransactionVoutResponse {
    pub id: String,
    // pub vout: usize,
    pub txid: Txid,
    pub n: usize,
    pub value: String,
    pub token_id: Option<u32>,
    pub script: TransactionVoutScript,
}

impl From<TransactionVout> for TransactionVoutResponse {
    fn from(v: TransactionVout) -> Self {
        Self {
            id: format!("{}{:x}", v.txid, v.vout),
            txid: v.txid,
            n: v.n,
            value: v.value,
            token_id: v.token_id,
            script: v.script,
        }
    }
}

//get list of vout transaction, by passing id which contains txhash + vout_idx
#[ocean_endpoint]
async fn get_vouts(
    Path(TransactionId { id }): Path<TransactionId>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionVoutResponse>> {
    let next = query.next.as_deref().unwrap_or("0").parse::<usize>()?;

    let list = ctx
        .services
        .transaction
        .vout_by_id
        .list(Some((id, next)), SortOrder::Ascending)?
        .paginate(&query)
        .filter_map(|item| match item {
            Ok((_, vout)) if vout.txid == id => Some(Ok(TransactionVoutResponse::from(vout))),
            Ok(_) => None,
            Err(e) => Some(Err(e.into())),
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

use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::{BlockHash, Txid};
use rust_decimal::Decimal;
use serde::{Deserialize, Deserializer, Serialize};
use serde_with::serde_as;

use super::{
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::{ApiError, Error},
    model::{Block, BlockContext, Transaction},
    storage::{
        InitialKeyProvider, RepositoryOps, SecondaryIndex, SortOrder, TransactionByBlockHash,
    },
    Result,
};

pub enum HashOrHeight {
    Height(u32),
    Id(BlockHash),
}

impl<'de> Deserialize<'de> for HashOrHeight {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        if let Ok(height) = s.parse::<u32>() {
            Ok(Self::Height(height))
        } else if let Ok(id) = s.parse::<BlockHash>() {
            Ok(Self::Id(id))
        } else {
            Err(serde::de::Error::custom("Error parsing HashOrHeight"))
        }
    }
}

#[serde_as]
#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TransactionResponse {
    pub id: Txid,
    pub txid: Txid,
    pub order: usize, // tx order
    pub block: BlockContext,
    pub hash: String,
    pub version: u32,
    pub size: u64,
    pub v_size: u64,
    pub weight: u64,
    #[serde(with = "rust_decimal::serde::str")]
    pub total_vout_value: Decimal,
    pub lock_time: u64,
    pub vin_count: usize,
    pub vout_count: usize,
}

impl From<Transaction> for TransactionResponse {
    fn from(v: Transaction) -> Self {
        Self {
            id: v.id,
            txid: v.id,
            order: v.order,
            block: v.block,
            hash: v.hash,
            version: v.version,
            size: v.size,
            v_size: v.v_size,
            weight: v.weight,
            total_vout_value: v.total_vout_value,
            lock_time: v.lock_time,
            vin_count: v.vin_count,
            vout_count: v.vout_count,
        }
    }
}

#[ocean_endpoint]
async fn list_blocks(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<Block>> {
    let next = query
        .next
        .as_ref()
        .map(|q| {
            let height = q.parse::<u32>()?;
            Ok::<u32, Error>(height)
        })
        .transpose()?;

    let repository = &ctx.services.block.by_height;
    let blocks = repository
        .list(next, SortOrder::Descending)?
        .paginate(&query)
        .map(|e| repository.retrieve_primary_value(e))
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(blocks, query.size, |block| {
        block.clone().height
    }))
}

#[ocean_endpoint]
async fn get_block(
    Path(id): Path<HashOrHeight>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<Block>>> {
    let block = if let Some(id) = match id {
        HashOrHeight::Height(n) => ctx.services.block.by_height.get(&n)?,
        HashOrHeight::Id(id) => Some(id),
    } {
        ctx.services.block.by_id.get(&id)?
    } else {
        None
    };

    Ok(Response::new(block))
}

#[ocean_endpoint]
async fn get_transactions(
    Path(hash): Path<BlockHash>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TransactionResponse>> {
    let repository = &ctx.services.transaction.by_block_hash;

    let next = query
        .next
        .as_ref()
        .map_or(Ok(TransactionByBlockHash::initial_key(hash)), |q| {
            let height: usize = q.parse::<usize>()?;
            Ok::<(BlockHash, usize), Error>((hash, height))
        })?;

    let txs = repository
        .list(Some(next), SortOrder::Ascending)?
        .paginate(&query)
        .take_while(|item| match item {
            Ok(((h, _), _)) => h == &hash,
            _ => true,
        })
        .map(|el| repository.retrieve_primary_value(el))
        .map(|v| v.map(TransactionResponse::from))
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(txs, query.size, |tx| tx.order))
}

// Get highest indexed block
#[ocean_endpoint]
async fn get_highest(
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<Block>>> {
    let block = ctx.services.block.by_height.get_highest()?;

    Ok(Response::new(block))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_blocks))
        .route("/highest", get(get_highest))
        .route("/:id", get(get_block))
        .route("/:hash/transactions", get(get_transactions))
        .layer(Extension(ctx))
}

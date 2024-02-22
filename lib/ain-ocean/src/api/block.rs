use std::sync::Arc;

use ain_macros::ocean_endpoint;
use anyhow::format_err;
use axum::{extract::Path, routing::get, Extension, Router};
use bitcoin::BlockHash;
use serde::{Deserialize, Deserializer};

use super::{
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::{ApiError, Error},
    model::{Block, Transaction},
    repository::RepositoryOps,
    storage::SortOrder,
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
            Ok(HashOrHeight::Height(height))
        } else if let Ok(id) = s.parse::<BlockHash>() {
            Ok(HashOrHeight::Id(id))
        } else {
            Err(serde::de::Error::custom("Error parsing HashOrHeight"))
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
            let height = q
                .parse::<u32>()
                .map_err(|_| format_err!("Invalid height"))?;
            Ok::<u32, Error>(height)
        })
        .transpose()?;

    let blocks = ctx
        .services
        .block
        .by_height
        .list(next, SortOrder::Descending)?
        .paginate(&query)
        .map(|item| {
            let (_, id) = item?;
            let b = ctx
                .services
                .block
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
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
) -> Result<ApiPagedResponse<Transaction>> {
    let next = query.next.as_ref().map_or(Ok((hash, 0)), |q| {
        let height = q
            .parse::<usize>()
            .map_err(|_| format_err!("Invalid height"))?;
        Ok::<(BlockHash, usize), Error>((hash, height))
    })?;

    let txs = ctx
        .services
        .transaction
        .by_block_hash
        .list(Some(next), SortOrder::Ascending)?
        .paginate(&query)
        .take_while(|item| match item {
            Ok(((h, _), _)) => h == &hash,
            _ => true,
        })
        .map(|item| {
            let (_, id) = item?;
            let tx = ctx
                .services
                .transaction
                .by_id
                .get(&id)?
                .ok_or("Missing tx index")?;

            Ok(tx)
        })
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

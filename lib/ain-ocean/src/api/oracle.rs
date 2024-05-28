use std::{str::FromStr, sync::Arc, thread::current};

use ain_macros::ocean_endpoint;
use anyhow::anyhow;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;
use rust_decimal::{prelude::FromPrimitive, Decimal};
use rust_decimal_macros::dec;

use super::{
    common::split_key,
    query::{self, PaginationQuery},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::{ApiError, Error, NotFoundKind},
    model::{ApiResponseOraclePriceFeed, Oracle},
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

#[ocean_endpoint]
async fn list_oracles(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<Oracle>> {
    let oracles_result: Result<Vec<(Txid, Oracle)>> = ctx
        .services
        .oracle
        .by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (id, oracle) = item?;
            Ok((id, oracle))
        })
        .collect();
    let oracles = oracles_result?;
    let oracles: Vec<Oracle> = oracles.into_iter().map(|(_, oracle)| oracle).collect();

    Ok(ApiPagedResponse::of(oracles, query.size, |oracles| {
        oracles.id
    }))
}

#[ocean_endpoint]
async fn get_feed(
    Path((oracle_id, key)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ApiResponseOraclePriceFeed>> {
    let txid = Txid::from_str(&oracle_id)?;
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let key = (token.clone(), currency.clone(), txid.clone());

    let price_feed_list = ctx
        .services
        .oracle_price_feed
        .by_id
        .list(None, SortOrder::Descending)?
        .paginate(&query)
        .map(|res| res.expect("Error retrieving key"))
        .collect::<Vec<_>>();

    let mut oracle_price_feeds = Vec::new();

    for (_, feed) in &price_feed_list {
        let (token, currency, oracle_id, _) = &feed.id;
        if key.0.eq(token) && key.1.eq(currency) && key.2.eq(&oracle_id) {
            let amount_decimal = Decimal::from_i64(feed.amount).unwrap_or_default();
            let conversion_factor = Decimal::from_i32(100000000).unwrap_or_default();
            let amount = amount_decimal / conversion_factor;
            oracle_price_feeds.push(ApiResponseOraclePriceFeed {
                id: format!("{}-{}-{}-{}", token, currency, feed.oracle_id, feed.txid),
                key: format!("{}-{}-{}", token, currency, feed.oracle_id),
                sort: feed.sort.clone(),
                token: feed.token.clone(),
                currency: feed.currency.clone(),
                oracle_id: feed.oracle_id,
                txid: feed.txid,
                time: feed.time,
                amount: amount.to_string(),
                block: feed.block.clone(),
            });
        }
    }

    Ok(ApiPagedResponse::of(
        oracle_price_feeds,
        query.size,
        |price_feed| price_feed.sort.clone(),
    ))
}

#[ocean_endpoint]
async fn get_oracle_by_address(
    Path(address): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Oracle>> {
    format!("Oracle details for address {}", address);
    let (_, oracle) = ctx
        .services
        .oracle
        .by_id
        .list(None, SortOrder::Descending)?
        .filter_map(|item| {
            let (id, oracle) = item.ok()?;
            if oracle.owner_address == address {
                Some((id, oracle))
            } else {
                None
            }
        })
        .next()
        .ok_or(Error::NotFound(NotFoundKind::Oracle))?;
    Ok(Response::new(oracle))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_oracles))
        .route("/:oracleId/:key/feed", get(get_feed))
        .route("/:address", get(get_oracle_by_address))
        .layer(Extension(ctx))
}

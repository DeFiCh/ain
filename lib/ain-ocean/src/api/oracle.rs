use std::{str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use anyhow::anyhow;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;

use super::{
    common::split_key,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
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

    // Retrieve and filter oracle price feeds by key and check that they match the requested token and currency
    let oracle_price_feed = ctx
        .services
        .oracle_price_feed
        .by_key
        .list(
            Some((token.clone(), currency.clone(), txid)),
            SortOrder::Ascending,
        )?
        .filter_map(|result| result.ok())
        .filter(|(_, id)| id.0 == token && id.1 == currency && id.2 == txid)
        .take(query.size)
        .map(|(_, id)| {
            let b = ctx
                .services
                .oracle_price_feed
                .by_id
                .get(&id)?
                .ok_or("Missing price feed index")?;

            Ok(ApiResponseOraclePriceFeed {
                id: format!("{}-{}-{}-{}", token, currency, b.oracle_id, b.txid),
                key: format!("{}-{}-{}", token, currency, b.oracle_id),
                sort: b.sort,
                token: b.token,
                currency: b.currency,
                oracle_id: b.oracle_id,
                txid: b.txid,
                time: b.time,
                amount: b.amount.to_string(), // Convert i64 to String
                block: b.block,
            })
        })
        .filter_map(Result::ok)
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(oracle_price_feed, 5, |price_feed| {
        price_feed.sort.clone()
    }))
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

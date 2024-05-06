use std::{str::FromStr, sync::Arc, thread::current};

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
    query::{self, PaginationQuery},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::{ApiError, Error, NotFoundKind},
    indexer::oracle,
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
    println!("Token: {}, Currency: {}", token, currency);
    println!("txid: {:?}", txid.clone());
    let key =
        ctx.services
            .oracle_price_feed
            .by_key
            .get(&(token.clone(), currency.clone(), txid))?;
    println!("price_key: {:?}", key);
    let oracle_price_feed = ctx
        .services
        .oracle_price_feed
        .by_id
        .list(key, SortOrder::Descending)?
        .take(5)
        .map(|item| {
            let (_, oracle) = item?;
            println!("price_fedd_LIST: {:?}", oracle);
            if oracle.token.eq(&token) && oracle.currency.eq(&currency) {
                Ok(ApiResponseOraclePriceFeed {
                    id: format!(
                        "{}-{}-{}-{}",
                        token, currency, oracle.oracle_id, oracle.txid
                    ),
                    key: format!("{}-{}-{}", token, currency, oracle.oracle_id),
                    sort: oracle.sort,
                    token: oracle.token,
                    currency: oracle.currency,
                    oracle_id: oracle.oracle_id,
                    txid: oracle.txid,
                    time: oracle.time,
                    amount: oracle.amount.to_string(), // Convert i64 to String
                    block: oracle.block,
                })
            } else {
                return Err(Error::Other(anyhow!("Token or currency mismatch")));
            }
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

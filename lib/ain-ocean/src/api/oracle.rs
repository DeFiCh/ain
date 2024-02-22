use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;

use super::{
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::{Oracle, OraclePriceFeed},
    repository::RepositoryOps,
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
        .list(None)?
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
async fn get_price_feed(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceFeed>> {
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 3 {
                return Err("Invalid query format");
            }
            let oracle_id = parts[0].parse::<Txid>().map_err(|_| "Invalid oracle_id")?;
            let token = parts[1].parse::<String>().map_err(|_| "Invalid token")?;
            let currency = parts[2].parse::<String>().map_err(|_| "Invalid currency")?;
            Ok((token, currency, oracle_id))
        })
        .transpose()?;
    let oracle_price_feed = ctx
        .services
        .oracle_price_feed
        .by_key
        .list(next)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = ctx
                .services
                .oracle_price_feed
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;
    Ok(ApiPagedResponse::of(
        oracle_price_feed,
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
        .list(None)?
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
        .route("/:oracleId/:key/feed", get(get_price_feed))
        .route("/:address", get(get_oracle_by_address))
}

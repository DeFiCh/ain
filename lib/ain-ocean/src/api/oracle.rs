use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;

use super::{
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api_query::PaginationQuery,
    error::{ApiError, Error, NotFoundKind},
    model::{Oracle, OraclePriceFeed, OraclePriceFeedId, OraclePriceFeedkey},
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
    Path(oracle_id): Path<Txid>,
    Path(key): Path<OraclePriceFeedkey>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<OraclePriceFeed>> {
    let key_tuple = (key.0.clone(), key.1.clone(), key.2.clone(), oracle_id);
    let price_feed = ctx.services.oracle_price_feed.by_id.get(&key_tuple);

    match price_feed {
        Ok(Some(oracle)) => Ok(Response::new(oracle)),
        Ok(None) => Err(Error::NotFound(NotFoundKind::Oracle)),
        Err(err) => Err(err), // Propagate the error if there is any other error
    }
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

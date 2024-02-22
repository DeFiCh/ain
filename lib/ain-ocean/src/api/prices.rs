use std::sync::Arc;
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use serde::{Serialize,Deserialize};
use super::{
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind}, model::{OraclePriceActive, OracleTokenCurrency,PriceTicker,OraclePriceAggregated}, repository::RepositoryOps, Result
};

#[derive(Debug,Deserialize,Serialize)]
pub struct PriceKey {
    key: String,
}

#[derive(Deserialize)]
struct FeedWithInterval {
    key: String,
    interval: i64,
}
#[ocean_endpoint]
async fn list_prices(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceTicker>> {
    "List of prices".to_string();
    let prices = ctx
        .services
        .price_ticker
        .by_id
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (id, priceticker) = item?;
            Ok((id, priceticker))
        })
        .collect::<Result<Vec<_>>>()?;

    let prices: Vec<PriceTicker> = prices.into_iter().map(|(_, price_ticker)| price_ticker).collect();
  
    Ok(ApiPagedResponse::of(prices, query.size, |price| {
      price.sort.to_string()
    }))
}
#[ocean_endpoint]
async fn get_key(
    Path(price_key): Path<PriceKey>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<PriceTicker>> {
    if let Some(ticker_id) = ctx.services.price_ticker.by_key.get(&price_key)? {
        if let Some(price_ticker) = ctx.services.price_ticker.by_id.get(&ticker_id)? {
            Ok(Response::new(price_ticker))
        } else {
            Err(Error::NotFound(NotFoundKind::Oracle))
        }
    } else {
        Err(Error::NotFound(NotFoundKind::Oracle))
    }
}

#[ocean_endpoint]
async fn get_feed (
    Query(query): Query<PaginationQuery>,
    Path(price_key): Path<PriceKey>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregated>> {
   
    let aggregated = ctx.services
        .oracle_price_aggregated
        .by_key
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (key, id) = item?;
            let b = ctx.services
                .oracle_price_aggregated
                .by_id
                .get(&id)?
                .ok_or("Missing price_aggregated index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

        Ok(ApiPagedResponse::of(aggregated, query.size, |aggre| {
           aggre.sort.to_string()
        }))

}
#[ocean_endpoint]
async fn get_feed_active(
    Query(query): Query<PaginationQuery>,
    Path(key): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
)-> Result<ApiPagedResponse<OraclePriceActive>> {
    format!("Active feed for price with key {}", key);
    let price_active = ctx.services
    .oracle_price_active
    .by_key
    .list(None)?
    .take(query.size)
    .map(|item| {
        let (key, id) = item?;
        let b = ctx.services
            .oracle_price_active
            .by_id
            .get(&id)?
            .ok_or("Missing price_aggregated index")?;
        Ok(b)
    })
    .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(price_active, query.size, |price_active| {
       price_active.sort.to_string()
    }))
}
// #[ocean_endpoint]
// async fn get_feed_with_interval(
//     Path(FeedWithInterval { key, interval }): Path<FeedWithInterval>,
// ) -> String {
//     format!("Feed for price with key {} over interval {}", key, interval)
// }
#[ocean_endpoint]
async fn get_oracles(
    Query(query): Query<PaginationQuery>,
    Path(key): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
)-> Result<ApiPagedResponse<OracleTokenCurrency>> {
    format!("Oracles for price with key {}", key);
    let token_currency = ctx.services
    .oracle_token_currency
    .by_key
    .list(None)?
    .take(query.size)
    .map(|item| {
        let (key, id) = item?;
        let b = ctx.services
            .oracle_token_currency
            .by_id
            .get(&id)?
            .ok_or("Missing token-currency index")?;
        Ok(b)
    })
    .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(token_currency, query.size, |currecy| {
        currecy.oracle_id.to_string()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_prices))
        .route("/:key", get(get_key))
        .route("/:key/feed/active", get(get_feed_active))
        .route("/:key/feed", get(get_feed))
        // .route("/:key/feed/interval/:interval", get(get_feed_with_interval))
        .route("/:key/oracles", get(get_oracles))
}

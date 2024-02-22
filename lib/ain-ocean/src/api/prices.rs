use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use serde::{Deserialize, Serialize};

use super::{
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::{
        OracleIntervalSeconds, OraclePriceActive, OraclePriceAggregated,
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalAggregated,
        OracleTokenCurrency, OracleTokenCurrencyKey, PriceTicker,
    },
    repository::RepositoryOps,
    Result,
};

#[derive(Debug, Deserialize, Serialize)]
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

    let prices: Vec<PriceTicker> = prices
        .into_iter()
        .map(|(_, price_ticker)| price_ticker)
        .collect();

    Ok(ApiPagedResponse::of(prices, query.size, |price| {
        price.sort.to_string()
    }))
}
#[ocean_endpoint]
async fn get_price(
    Path(token): Path<String>,
    Path(curreny): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<PriceTicker>> {
    let price_ticker_id = (token, curreny);
    if let Some(price_ticker) = ctx.services.price_ticker.by_id.get(&price_ticker_id)? {
        Ok(Response::new(price_ticker))
    } else {
        Err(Error::NotFound(NotFoundKind::Oracle))
    }
}

#[ocean_endpoint]
async fn get_feed(
    Query(query): Query<PaginationQuery>,
    Path(price_key): Path<PriceKey>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregated>> {
    let aggregated = ctx
        .services
        .oracle_price_aggregated
        .by_key
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (key, id) = item?;
            let b = ctx
                .services
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
) -> Result<ApiPagedResponse<OraclePriceActive>> {
    format!("Active feed for price with key {}", key);
    let price_active = ctx
        .services
        .oracle_price_active
        .by_key
        .list(None)?
        .take(query.size)
        .map(|item| {
            let (key, id) = item?;
            let b = ctx
                .services
                .oracle_price_active
                .by_id
                .get(&id)?
                .ok_or("Missing price_aggregated index")?;
            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        price_active,
        query.size,
        |price_active| price_active.sort.to_string(),
    ))
}
#[ocean_endpoint]
async fn get_feed_with_interval(
    Query(query): Query<PaginationQuery>,
    Path(token): Path<String>,
    Path(currency): Path<String>,
    Path(interval): Path<OracleIntervalSeconds>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedInterval>> {
    let interval_time = interval.clone();
    let interval_key = Some((token, currency, interval_time));
    let items = ctx
        .services
        .oracle_price_aggregated_interval
        .by_key
        .list(interval_key)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let price_agrregated_interval = ctx
                .services
                .oracle_price_aggregated_interval
                .by_id
                .get(&id)?
                .ok_or("Missing oracle price aggregated interval index")?;
            Ok(price_agrregated_interval)
        })
        .collect::<Result<Vec<_>>>()?;

    let mapped: Vec<OraclePriceAggregatedInterval> = items
        .into_iter()
        .map(|item| {
            let start = item.block.median_time - (item.block.median_time % interval.clone() as i64);
            OraclePriceAggregatedInterval {
                id: item.id,
                key: item.key,
                sort: item.sort,
                token: item.token,
                currency: item.currency,
                aggregated: OraclePriceAggregatedIntervalAggregated {
                    amount: item.aggregated.amount,
                    weightage: item.aggregated.weightage,
                    count: item.aggregated.count,
                    oracles: item.aggregated.oracles,
                },
                block: item.block,
            }
        })
        .collect();

    Ok(ApiPagedResponse::of(mapped, query.size, |item| {
        item.sort.clone()
    }))
}

#[ocean_endpoint]
async fn get_oracles(
    Query(query): Query<PaginationQuery>,
    Path(key): Path<OracleTokenCurrencyKey>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OracleTokenCurrency>> {
    let items = ctx
        .services
        .oracle_token_currency
        .by_key
        .list(Some(key))?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = ctx
                .services
                .oracle_token_currency
                .by_id
                .get(&id)?
                .ok_or("Missing token-currency index")?;
            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(items, query.size, |item| {
        item.oracle_id.to_string()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_prices))
        .route("/:key", get(get_price))
        .route("/:key/feed/active", get(get_feed_active))
        .route("/:key/feed", get(get_feed))
        .route("/:key/feed/interval/:interval", get(get_feed_with_interval))
        .route("/:key/oracles", get(get_oracles))
}

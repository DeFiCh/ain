use std::sync::Arc;

use ain_macros::ocean_endpoint;
use anyhow::anyhow;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};

use super::{
    common::split_key,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::{
        OracleIntervalSeconds, OraclePriceActive, OraclePriceAggregated,
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalAggregated,
        OracleTokenCurrency, PriceTicker,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

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
        .list(None, SortOrder::Descending)?
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
    Path(key): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<PriceTicker>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let price_ticker_id = (token, currency);
    println!("price {:?}", price_ticker_id);
    if let Some(price_ticker) = ctx.services.price_ticker.by_id.get(&price_ticker_id)? {
        Ok(Response::new(price_ticker))
    } else {
        Err(Error::NotFound(NotFoundKind::Oracle))
    }
}

#[ocean_endpoint]
async fn get_feed(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregated>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let price_aggregated_key = (token, currency);
    let aggregated = ctx
        .services
        .oracle_price_aggregated
        .by_key
        .list(Some(price_aggregated_key), SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = ctx
                .services
                .oracle_price_aggregated
                .by_id
                .get(&id)?
                .ok_or("cannot find price_aggregated index for given token-currency")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(aggregated, query.size, |aggre| {
        aggre.sort.to_string()
    }))
}
#[ocean_endpoint]
async fn get_feed_active(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceActive>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let price_active_key = (token, currency);
    let price_active = ctx
        .services
        .oracle_price_active
        .by_key
        .list(Some(price_active_key), SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
            let b = ctx
                .services
                .oracle_price_active
                .by_id
                .get(&id)?
                .ok_or("Missing price active index")?;
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
    Path((key, interval)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedInterval>> {
    println!("the value {:?} {:?}", key, interval);
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let interval = match interval.as_str() {
        "900" => OracleIntervalSeconds::FifteenMinutes,
        "3600" => OracleIntervalSeconds::OneHour,
        "86400" => OracleIntervalSeconds::OneDay,
        _ => return Err(From::from("Invalid interval")),
    };
    let price_aggregated_interval = (token, currency, interval.clone());
    let items = ctx
        .services
        .oracle_price_aggregated_interval
        .by_key
        .list(Some(price_aggregated_interval), SortOrder::Descending)?
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
            let _start =
                item.block.median_time - (item.block.median_time % interval.clone() as i64);
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
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OracleTokenCurrency>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let oracle_token_currency_key = (token, currency);
    let items = ctx
        .services
        .oracle_token_currency
        .by_key
        .list(Some(oracle_token_currency_key), SortOrder::Descending)?
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
        .layer(Extension(ctx))
}

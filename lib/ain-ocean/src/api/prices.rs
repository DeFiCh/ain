use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};

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
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregated>> {
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }
            let token = parts[0].parse::<String>().map_err(|_| "Invalid token")?;
            let currency = parts[1].parse::<String>().map_err(|_| "Invalid currency")?;
            Ok((token, currency))
        })
        .transpose()?;
    let aggregated = ctx
        .services
        .oracle_price_aggregated
        .by_key
        .list(next, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (_, id) = item?;
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
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceActive>> {
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }
            let token = parts[0].parse::<String>().map_err(|_| "Invalid token")?;
            let currency = parts[1].parse::<String>().map_err(|_| "Invalid currency")?;
            Ok((token, currency))
        })
        .transpose()?;
    let price_active = ctx
        .services
        .oracle_price_active
        .by_key
        .list(next, SortOrder::Descending)?
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
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedInterval>> {
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 3 {
                return Err("Invalid query format");
            }
            let token = parts[0].parse::<String>().map_err(|_| "Invalid token")?;
            let currency = parts[1].parse::<String>().map_err(|_| "Invalid currency")?;
            let interval = match parts[2] {
                "900" => OracleIntervalSeconds::FifteenMinutes,
                "3600" => OracleIntervalSeconds::OneHour,
                "86400" => OracleIntervalSeconds::OneDay,
                _ => return Err("Invalid interval"),
            };
            Ok((token, currency, interval))
        })
        .transpose()?;
    let items = ctx
        .services
        .oracle_price_aggregated_interval
        .by_key
        .list(next.clone(), SortOrder::Descending)?
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
                item.block.median_time - (item.block.median_time % next.clone().unwrap().2 as i64);
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
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OracleTokenCurrency>> {
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }
            let token = parts[0].parse::<String>().map_err(|_| "Invalid token")?;
            let currency = parts[1].parse::<String>().map_err(|_| "Invalid currency")?;
            Ok((token, currency))
        })
        .transpose()?;
    let items = ctx
        .services
        .oracle_token_currency
        .by_key
        .list(next, SortOrder::Descending)?
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

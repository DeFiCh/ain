use std::{str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use ain_dftx::COIN;
use anyhow::{anyhow, Context};
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use indexmap::IndexSet;
use rust_decimal::Decimal;

use super::{
    common::split_key,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::{
        ApiResponseOraclePriceFeed, OracleIntervalSeconds, OraclePriceActive,
        OraclePriceAggregated, OraclePriceAggregatedAggregated, OraclePriceAggregatedApi,
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalAggregated,
        OracleTokenCurrency, PriceOracles, PriceTickerApi,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

#[ocean_endpoint]
async fn list_prices(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceTickerApi>> {
    let sorted_ids = ctx
        .services
        .price_ticker
        .by_key
        .list(None, SortOrder::Descending)?
        .map(|item| {
            let (_, id) = item?;
            Ok(id)
        })
        .collect::<Result<Vec<_>>>()?;

    // use IndexSet to rm dup without changing order
    let mut sorted_ids_set = IndexSet::new();
    for id in sorted_ids {
        sorted_ids_set.insert(id);
    }

    let prices = sorted_ids_set
        .into_iter()
        .take(query.size)
        .map(|id| {
            let price_ticker = ctx
                .services
                .price_ticker
                .by_id
                .get(&id)?
                .context("Missing price ticker index")?;

            let amount = Decimal::from_str(&price_ticker.price.aggregated.amount)? / Decimal::from(COIN);
            Ok(PriceTickerApi {
                id: format!("{}-{}", price_ticker.id.0, price_ticker.id.1),
                sort: price_ticker.sort,
                price: OraclePriceAggregatedApi {
                    id: format!(
                        "{}-{}-{}",
                        price_ticker.price.id.0, price_ticker.price.id.1, price_ticker.price.id.2
                    ),
                    key: format!("{}-{}", price_ticker.price.key.0, price_ticker.price.key.1),
                    sort: price_ticker.price.sort,
                    token: price_ticker.price.token,
                    currency: price_ticker.price.currency,
                    aggregated: OraclePriceAggregatedAggregated {
                        amount: amount.to_string(),
                        weightage: price_ticker.price.aggregated.weightage,
                        oracles: price_ticker.price.aggregated.oracles,
                    },
                    block: price_ticker.price.block,
                },
            })
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(prices, query.size, |price| {
        price.sort.to_string()
    }))
}
#[ocean_endpoint]
async fn get_price(
    Path(key): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<PriceTickerApi>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let price_ticker_id = (token, currency);
    if let Some(price_ticker) = ctx.services.price_ticker.by_id.get(&price_ticker_id)? {
        if price_ticker.price.token.eq(&price_ticker_id.0)
            && price_ticker.price.currency.eq(&price_ticker_id.1)
        {
            let amount = Decimal::from_str(&price_ticker.price.aggregated.amount)? / Decimal::from(COIN);
            let ticker = PriceTickerApi {
                id: format!("{}-{}", price_ticker.id.0, price_ticker.id.1),
                sort: price_ticker.sort,
                price: OraclePriceAggregatedApi {
                    id: format!(
                        "{}-{}-{}",
                        price_ticker.price.id.0, price_ticker.price.id.1, price_ticker.price.id.2
                    ),
                    key: format!("{}-{}", price_ticker.price.key.0, price_ticker.price.key.1),
                    sort: price_ticker.price.sort,
                    token: price_ticker.price.token,
                    currency: price_ticker.price.currency,
                    aggregated: OraclePriceAggregatedAggregated {
                        amount: amount.to_string(),
                        weightage: price_ticker.price.aggregated.weightage,
                        oracles: price_ticker.price.aggregated.oracles,
                    },
                    block: price_ticker.price.block,
                },
            };
            Ok(Response::new(ticker))
        } else {
            Err(Error::NotFound(NotFoundKind::Oracle))
        }
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
    let aggregated_key = (token, currency);
    let mut oracle_aggrigated = Vec::new();
    let aggregated = ctx
        .services
        .oracle_price_aggregated
        .by_id
        .list(None, SortOrder::Ascending)?
        .take(query.size)
        .map(|item| {
            let (_, price_aggrigated) = item?;
            Ok(price_aggrigated)
        })
        .collect::<Result<Vec<_>>>()?;

    for aggre in aggregated {
        let (token, currency, _) = &aggre.id;
        if aggregated_key.0.eq(token) && aggregated_key.1.eq(currency) {
            oracle_aggrigated.push(aggre);
        }
    }
    Ok(ApiPagedResponse::of(
        oracle_aggrigated,
        query.size,
        |aggre| aggre.sort.to_string(),
    ))
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
    let mut price_list = Vec::new();
    let price_active = ctx
        .services
        .oracle_price_active
        .by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (_, data) = item?;
            Ok(data)
        })
        .collect::<Result<Vec<_>>>()?;
    for active in price_active {
        let (token, currency, _) = &active.id;
        if price_active_key.0.eq(token) && price_active_key.1.eq(currency) {
            price_list.push(active);
        }
    }

    Ok(ApiPagedResponse::of(
        price_list,
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
    let mut feed_intervals = Vec::new();
    let price_aggregated_interval = (&token, &currency, interval.clone());
    let items = ctx
        .services
        .oracle_price_aggregated_interval
        .by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (_, data) = item?;
            Ok(data)
        })
        .collect::<Result<Vec<_>>>()?;

    for oracle_intervals in items {
        let (token, currency, _) = &oracle_intervals.key;
        if price_aggregated_interval.0.eq(token) && price_aggregated_interval.1.eq(currency) {
            feed_intervals.push(OraclePriceAggregatedInterval {
                id: oracle_intervals.id,
                key: oracle_intervals.key,
                sort: oracle_intervals.sort,
                token: oracle_intervals.token,
                currency: oracle_intervals.currency,
                aggregated: OraclePriceAggregatedIntervalAggregated {
                    amount: oracle_intervals.aggregated.amount,
                    weightage: oracle_intervals.aggregated.weightage,
                    count: oracle_intervals.aggregated.count,
                    oracles: oracle_intervals.aggregated.oracles,
                },
                block: oracle_intervals.block,
            });
        }
    }

    Ok(ApiPagedResponse::of(feed_intervals, query.size, |item| {
        item.sort.clone()
    }))
}

#[ocean_endpoint]
async fn get_oracles(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceOracles>> {
    let (token, currency) = match split_key(&key) {
        Ok((t, c)) => (t, c),
        Err(e) => return Err(Error::Other(anyhow!("Failed to split key: {}", e))),
    };
    let mut oracles_feed = Vec::new();
    let oracle_token_currency_key = (token, currency);
    let token_currency: Vec<OracleTokenCurrency> = ctx
        .services
        .oracle_token_currency
        .by_id
        .list(None, SortOrder::Ascending)?
        .take(query.size)
        .map(|item| {
            let (_, data) = item?;
            Ok(data)
        })
        .collect::<Result<Vec<_>>>()?;

    let price_feed = ctx
        .services
        .oracle_price_feed
        .by_id
        .list(None, SortOrder::Ascending)?
        .take(query.size)
        .map(|item| {
            let (_, data) = item?;
            Ok(data)
        })
        .collect::<Result<Vec<_>>>()?;

    for item in token_currency {
        let (token, currency, _) = &item.id;
        if oracle_token_currency_key.clone().0.eq(token)
            && oracle_token_currency_key.clone().1.eq(currency)
        {
            let mut oracleprice = None;
            for pricefeed in &price_feed {
                let (token, currency, _, _) = &pricefeed.id;
                if oracle_token_currency_key.clone().0.eq(token)
                    && oracle_token_currency_key.clone().1.eq(currency)
                {
                    oracleprice = Some(ApiResponseOraclePriceFeed {
                        id: format!(
                            "{}{}{}{}",
                            pricefeed.token,
                            pricefeed.currency,
                            pricefeed.oracle_id,
                            pricefeed.txid
                        ),
                        key: format!(
                            "{}{}{}",
                            pricefeed.token, pricefeed.currency, pricefeed.oracle_id
                        ),
                        sort: pricefeed.sort.clone(),
                        token: pricefeed.token.clone(),
                        currency: pricefeed.currency.clone(),
                        oracle_id: item.oracle_id,
                        txid: pricefeed.txid,
                        time: pricefeed.time,
                        amount: pricefeed.amount.to_string(),
                        block: pricefeed.block.clone(),
                    });
                }
            }
            oracles_feed.push(PriceOracles {
                id: format!("{}-{}-{}", item.id.0, item.id.1, item.id.2),
                key: format!("{}-{}", item.key.0, item.key.1),
                token: item.token,
                currency: item.currency,
                oracle_id: item.oracle_id.to_string(),
                feed: oracleprice,
                block: item.block,
                weightage: item.weightage,
            });
        }
    }
    Ok(ApiPagedResponse::of(oracles_feed, query.size, |item| {
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

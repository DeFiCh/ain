use std::{str::FromStr, sync::Arc};

use ain_dftx::{Currency, Token, Weightage, COIN};
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::{hashes::Hash, Txid};
use rust_decimal::{prelude::ToPrimitive, Decimal};
use serde::{Deserialize, Serialize};
use serde_with::skip_serializing_none;

use super::{
    common::{parse_price_ticker_sort, parse_token_currency},
    oracle::OraclePriceFeedResponse,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error},
    model::{
        BlockContext, OracleIntervalSeconds, OraclePriceActive,
        OraclePriceAggregatedIntervalAggregatedOracles, PriceTicker,
    },
    storage::{RepositoryOps, SortOrder},
    Result,
};

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedResponse {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: Token,
    pub currency: Currency,
    pub aggregated: OraclePriceAggregatedAggregatedResponse,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregatedResponse {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveNextOraclesResponse,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTickerResponse {
    pub id: String,   //token-currency
    pub sort: String, //count-height-token-currency
    pub price: OraclePriceAggregatedResponse,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextOraclesResponse {
    pub active: i32,
    pub total: i32,
}

impl From<((Token, Currency), PriceTicker)> for PriceTickerResponse {
    fn from(ticker: ((Token, Currency), PriceTicker)) -> Self {
        let token = ticker.0 .0;
        let currency = ticker.0 .1;
        let price_ticker = ticker.1;
        let amount = price_ticker.price.aggregated.amount / Decimal::from(COIN);
        Self {
            id: format!("{}-{}", token, currency),
            sort: format!(
                "{}{}{}-{}",
                hex::encode(price_ticker.price.aggregated.oracles.total.to_be_bytes()),
                hex::encode(price_ticker.price.block.height.to_be_bytes()),
                token.clone(),
                currency.clone(),
            ),
            price: OraclePriceAggregatedResponse {
                id: format!("{}-{}-{}", token, currency, price_ticker.price.block.height),
                key: format!("{}-{}", token, currency),
                sort: format!(
                    "{}{}",
                    hex::encode(price_ticker.price.block.median_time.to_be_bytes()),
                    hex::encode(price_ticker.price.block.height.to_be_bytes()),
                ),
                token,
                currency,
                aggregated: OraclePriceAggregatedAggregatedResponse {
                    amount: format!("{:.8}", amount),
                    weightage: price_ticker
                        .price
                        .aggregated
                        .weightage
                        .to_i32()
                        .unwrap_or_default(),
                    oracles: OraclePriceActiveNextOraclesResponse {
                        active: price_ticker
                            .price
                            .aggregated
                            .oracles
                            .active
                            .to_i32()
                            .unwrap_or_default(),
                        total: price_ticker.price.aggregated.oracles.total,
                    },
                },
                block: price_ticker.price.block,
            },
        }
    }
}

#[ocean_endpoint]
async fn list_prices(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceTickerResponse>> {
    let next = query
        .next
        .map(|item| {
            let id = parse_price_ticker_sort(&item)?;
            Ok::<([u8; 4], [u8; 4], Token, Currency), Error>(id)
        })
        .transpose()?;

    let prices = ctx
        .services
        .price_ticker
        .by_id
        .list(next, SortOrder::Descending)?
        .map(|item| {
            let ((_, _, token, currency), v) = item?;
            Ok(PriceTickerResponse::from(((token, currency), v)))
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
) -> Result<Response<Option<PriceTickerResponse>>> {
    let (token, currency) = parse_token_currency(&key)?;

    let price_ticker = ctx
        .services
        .price_ticker
        .by_id
        .list(
            Some(([0xffu8; 4], [0xffu8; 4], token.clone(), currency.clone())),
            SortOrder::Descending,
        )?
        .next()
        .transpose()?;

    let Some((_, price_ticker)) = price_ticker else {
        return Ok(Response::new(None));
    };

    let res = PriceTickerResponse::from(((token, currency), price_ticker));

    Ok(Response::new(Some(res)))
}

#[ocean_endpoint]
async fn get_feed(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedResponse>> {
    let (token, currency) = parse_token_currency(&key)?;
    let next = query
        .next
        .map(|q| {
            let median_time = &q[..16];
            let height = &q[16..];

            let median_time = median_time.parse::<i64>()?.to_be_bytes();
            let height = height.parse::<u32>()?.to_be_bytes();
            Ok::<([u8; 8], [u8; 4]), Error>((median_time, height))
        })
        .transpose()?
        .unwrap_or(([0xffu8; 8], [0xffu8; 4]));

    let repo = &ctx.services.oracle_price_aggregated;
    let id = (token.clone(), currency.clone(), next.0, next.1);
    let oracle_aggregated = repo
        .by_id
        .list(Some(id), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == token.clone() && k.1 == currency.clone(),
            _ => true,
        })
        .map(|item| {
            let (k, v) = item?;
            let res = OraclePriceAggregatedResponse {
                id: format!("{}-{}-{}", k.0, k.1, i64::from_be_bytes(k.2)),
                key: format!("{}-{}", k.0, k.1),
                sort: format!(
                    "{}{}",
                    hex::encode(v.block.median_time.to_be_bytes()),
                    hex::encode(v.block.height.to_be_bytes()),
                ),
                token: token.clone(),
                currency: currency.clone(),
                aggregated: OraclePriceAggregatedAggregatedResponse {
                    amount: format!("{:.8}", v.aggregated.amount),
                    weightage: v.aggregated.weightage.to_i32().unwrap_or_default(),
                    oracles: OraclePriceActiveNextOraclesResponse {
                        active: v.aggregated.oracles.active.to_i32().unwrap_or_default(),
                        total: v.aggregated.oracles.total,
                    },
                },
                block: v.block,
            };
            Ok(res)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        oracle_aggregated,
        query.size,
        |aggregated| aggregated.sort.clone(),
    ))
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextResponse {
    pub amount: String, // convert to logical amount
    pub weightage: Decimal,
    pub oracles: OraclePriceActiveNextOraclesResponse,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveResponse {
    pub id: String,   // token-currency-height
    pub key: String,  // token-currency
    pub sort: String, // height
    pub active: Option<OraclePriceActiveNextResponse>,
    pub next: Option<OraclePriceActiveNextResponse>,
    pub is_live: bool,
    pub block: BlockContext,
}

impl OraclePriceActiveResponse {
    fn from_with_id(token: &String, currency: &String, v: OraclePriceActive) -> Self {
        Self {
            id: format!("{}-{}-{}", token, currency, v.block.height),
            key: format!("{}-{}", token, currency),
            sort: hex::encode(v.block.height.to_be_bytes()).to_string(),
            active: v.active.map(|active| OraclePriceActiveNextResponse {
                amount: format!("{:.8}", active.amount / Decimal::from(COIN)),
                weightage: active.weightage,
                oracles: OraclePriceActiveNextOraclesResponse {
                    active: active.oracles.active.to_i32().unwrap_or_default(),
                    total: active.oracles.total,
                },
            }),
            next: v.next.map(|next| OraclePriceActiveNextResponse {
                amount: format!("{:.8}", next.amount / Decimal::from(COIN)),
                weightage: next.weightage,
                oracles: OraclePriceActiveNextOraclesResponse {
                    active: next.oracles.active.to_i32().unwrap_or_default(),
                    total: next.oracles.total,
                },
            }),
            is_live: v.is_live,
            block: v.block,
        }
    }
}

#[ocean_endpoint]
async fn get_feed_active(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceActiveResponse>> {
    let (token, currency) = parse_token_currency(&key)?;

    let next = query
        .next
        .map(|q| {
            let height = q.parse::<u32>()?.to_be_bytes();
            Ok::<[u8; 4], Error>(height)
        })
        .transpose()?
        .unwrap_or([0xffu8; 4]);

    let id = (token.clone(), currency.clone(), next);
    let price_active = ctx
        .services
        .oracle_price_active
        .by_id
        .list(Some(id), SortOrder::Descending)?
        .take_while(|item| match item {
            Ok(((t, c, _), _)) => t == &token && c == &currency,
            _ => true,
        })
        .take(query.size)
        .map(|item| {
            let ((token, currency, _), v) = item?;
            Ok(OraclePriceActiveResponse::from_with_id(
                &token, &currency, v,
            ))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(price_active, query.size, |price| {
        price.sort.to_string()
    }))
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalResponse {
    pub id: String,   // token-currency-interval-height
    pub key: String,  // token-currency-interval
    pub sort: String, // medianTime-height
    pub token: Token,
    pub currency: Currency,
    pub aggregated: OraclePriceAggregatedIntervalAggregatedResponse,
    pub block: BlockContext,

}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedResponse {
    #[serde(with = "rust_decimal::serde::str")]
    pub amount: Decimal,
    #[serde(with = "rust_decimal::serde::str")]
    pub weightage: Decimal,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
    /**
     * Aggregated interval time range in seconds.
     * - Interval that aggregated in seconds
     * - Start Time Inclusive
     * - End Time Exclusive
     */
    time: OraclePriceAggregatedIntervalTime,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalTime {
    interval: i64,
    start: i64,
    end: i64,
}

#[ocean_endpoint]
async fn get_feed_with_interval(
    Path((key, interval)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedIntervalResponse>> {
    let (token, currency) = parse_token_currency(&key)?;
    let interval = interval.parse::<i64>()?;

    let interval = match interval {
        900 => OracleIntervalSeconds::FifteenMinutes,
        3600 => OracleIntervalSeconds::OneHour,
        86400 => OracleIntervalSeconds::OneDay,
        _ => return Err(From::from("Invalid oracle interval")),
    };
    let interval = interval as u32;

    let next = query
        .next
        .map(|q| {
            let height = q.parse::<u32>()?.to_be_bytes();
            Ok::<[u8; 4], Error>(height)
        })
        .transpose()?
        .unwrap_or([0xffu8; 4]);

    let id = (token.clone(), currency.clone(), interval.to_string(), next);

    let items = ctx
        .services
        .oracle_price_aggregated_interval
        .by_id
        .list(Some(id), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok(((t, c, i, _), _)) => {
                t == &token.clone() && c == &currency.clone() && i == &interval.to_string()
            }
            _ => true,
        })
        .flatten()
        .collect::<Vec<_>>();

    let interval = interval as i64;
    let mut prices = Vec::new();
    for (id, item) in items {
        let start = item.block.median_time - (item.block.median_time % interval);
        let height = u32::from_be_bytes(id.3);

        let price = OraclePriceAggregatedIntervalResponse {
            id: format!("{}-{}-{}-{}", id.0, id.1, id.2, height),
            key: format!("{}-{}-{}", id.0, id.1, id.2),
            sort: hex::encode(item.block.height.to_be_bytes()).to_string(),
            token: token.clone(),
            currency: currency.clone(),
            aggregated: OraclePriceAggregatedIntervalAggregatedResponse {
                amount: item.aggregated.amount,
                weightage: item.aggregated.weightage,
                oracles: item.aggregated.oracles,
                count: item.aggregated.count,
                time: OraclePriceAggregatedIntervalTime {
                    interval,
                    start,
                    end: start + interval,
                },
            },
            block: item.block,
        };
        prices.push(price);
    }

    Ok(ApiPagedResponse::of(prices, query.size, |item| {
        item.sort.clone()
    }))
}

#[skip_serializing_none]
#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PriceOracleResponse {
    pub id: String,
    pub key: String,
    pub token: Token,
    pub currency: Currency,
    pub oracle_id: String,
    pub weightage: Weightage,
    pub feed: Option<OraclePriceFeedResponse>,
    pub block: BlockContext,
}

#[ocean_endpoint]
async fn list_price_oracles(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceOracleResponse>> {
    let (token, currency) = parse_token_currency(&key)?;

    let next = query
        .next
        .map(|q| {
            let oracle_id = Txid::from_str(&q)?;
            Ok::<Txid, Error>(oracle_id)
        })
        .transpose()?
        .unwrap_or(Txid::from_byte_array([0xffu8; 32]));

    let id = (token.clone(), currency.clone(), next);
    let token_currencies = ctx
        .services
        .oracle_token_currency
        .by_id
        .list(Some(id.clone()), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == id.0 && k.1 == id.1,
            _ => true,
        })
        .flatten()
        .collect::<Vec<_>>();

    let mut prices = Vec::new();
    for ((t, c, oracle_id), token_currency) in token_currencies {
        let feed = ctx
            .services
            .oracle_price_feed
            .by_id
            .list(
                Some((
                    token.clone(),
                    currency.clone(),
                    oracle_id,
                    [0xffu8; 4],
                    Txid::from_byte_array([0xffu8; 32]),
                )),
                SortOrder::Descending,
            )?
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == token && k.1 == currency && k.2 == oracle_id,
                _ => true,
            })
            .next()
            .transpose()?;

        prices.push(PriceOracleResponse {
            id: format!("{}-{}-{}", t, c, oracle_id),
            key: format!("{}-{}", t, c),
            token: t,
            currency: c,
            oracle_id: oracle_id.to_string(),
            weightage: token_currency.weightage,
            block: token_currency.block,
            feed: feed.map(|(id, f)| {
                let token = id.0;
                let currency = id.1;
                let oracle_id = id.2;
                let height = u32::from_be_bytes(id.3);
                let txid = id.4;
                OraclePriceFeedResponse {
                    id: format!("{}-{}-{}-{}", token, currency, oracle_id, txid),
                    key: format!("{}-{}-{}", token, currency, oracle_id),
                    sort: hex::encode(height.to_string() + &txid.to_string()),
                    token: token.clone(),
                    currency: currency.clone(),
                    oracle_id,
                    txid,
                    time: f.time,
                    amount: f.amount.to_string(),
                    block: f.block,
                }
            }),
        });
    }

    Ok(ApiPagedResponse::of(prices, query.size, |price| {
        price.oracle_id.to_string()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_prices))
        .route("/:key", get(get_price))
        .route("/:key/feed/active", get(get_feed_active))
        .route("/:key/feed", get(get_feed))
        .route("/:key/feed/interval/:interval", get(get_feed_with_interval))
        .route("/:key/oracles", get(list_price_oracles))
        .layer(Extension(ctx))
}

use std::sync::Arc;

use ain_dftx::{Currency, Token, Weightage, COIN};
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::{hashes::Hash, Txid};
use indexmap::IndexSet;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};
use snafu::OptionExt;

use super::{
    common::parse_token_currency,
    oracle::OraclePriceFeedResponse,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, OtherSnafu},
    model::{
        BlockContext, OracleIntervalSeconds, OraclePriceActive, OraclePriceActiveNextOracles,
        OraclePriceAggregatedIntervalAggregated, OracleTokenCurrency, PriceTicker,
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
    pub weightage: u32,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTickerResponse {
    pub id: String,   //token-currency
    pub sort: String, //count-height-token-currency
    pub price: OraclePriceAggregatedResponse,
}

impl From<((Token, Currency), PriceTicker)> for PriceTickerResponse {
    fn from(ticker: ((Token, Currency), PriceTicker)) -> Self {
        let token = ticker.0 .0;
        let currency = ticker.0 .1;
        let price_ticker = ticker.1;
        let amount = price_ticker.price.aggregated.amount / Decimal::from(COIN);
        Self {
            id: format!("{}-{}", token, currency),
            sort: price_ticker.sort,
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
                    amount: format!("{amount:.8}"),
                    weightage: price_ticker.price.aggregated.weightage,
                    oracles: price_ticker.price.aggregated.oracles,
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
                .context(OtherSnafu {
                    msg: "Missing price ticker index",
                })?;

            Ok(PriceTickerResponse::from((id, price_ticker)))
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
        .get(&(token.clone(), currency.clone()))?;

    let Some(price_ticker) = price_ticker else {
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

    let repo = &ctx.services.oracle_price_aggregated;
    let id = (token.to_string(), currency.to_string(), u32::MAX);
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
                id: format!("{}-{}-{}", k.0, k.1, k.2),
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
                    weightage: v.aggregated.weightage,
                    oracles: v.aggregated.oracles,
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

#[ocean_endpoint]
async fn get_feed_active(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceActive>> {
    let (token, currency) = parse_token_currency(&key)?;

    let key = (token, currency);
    let repo = &ctx.services.oracle_price_active;
    let price_active = ctx
        .services
        .oracle_price_active
        .by_key
        .list(Some(key), SortOrder::Descending)?
        .take(query.size)
        .flat_map(|item| {
            let (_, id) = item?;
            let item = repo.by_id.get(&id)?;
            Ok::<Option<OraclePriceActive>, Error>(item)
        })
        .flatten()
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(price_active, query.size, |price| {
        price.sort.to_string()
    }))
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalResponse {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: Token,
    pub currency: Currency,
    pub aggregated: OraclePriceAggregatedIntervalAggregated,
    pub block: BlockContext,
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

    let interval_type = match interval {
        900 => OracleIntervalSeconds::FifteenMinutes,
        3600 => OracleIntervalSeconds::OneHour,
        86400 => OracleIntervalSeconds::OneDay,
        _ => return Err(From::from("Invalid oracle interval")),
    };
    let key = (token, currency, interval_type);
    let repo = &ctx.services.oracle_price_aggregated_interval;

    let keys = repo
        .by_key
        .list(Some(key), SortOrder::Descending)?
        .take(query.size)
        .flatten()
        .collect::<Vec<_>>();

    let mut prices = Vec::new();
    for ((token, currency, _), id) in keys {
        let item = repo.by_id.get(&id)?;

        let Some(item) = item else { continue };

        let start = item.block.median_time - (item.block.median_time % interval);

        let price = OraclePriceAggregatedIntervalResponse {
            id: format!("{}-{}-{:?}", id.0, id.1, id.2),
            key: format!("{}-{}", id.0, id.1),
            sort: format!(
                "{}{}",
                hex::encode(item.block.median_time.to_be_bytes()),
                hex::encode(item.block.height.to_be_bytes()),
            ),
            token,
            currency,
            aggregated: OraclePriceAggregatedIntervalAggregated {
                amount: format!("{:.8}", item.aggregated.amount),
                weightage: item.aggregated.weightage,
                oracles: item.aggregated.oracles,
                count: item.aggregated.count,
            },
            block: item.block,
            time: OraclePriceAggregatedIntervalTime {
                interval,
                start,
                end: start + interval,
            },
        };
        prices.push(price);
    }

    Ok(ApiPagedResponse::of(prices, query.size, |item| {
        item.sort.clone()
    }))
}

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
async fn get_oracles(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceOracleResponse>> {
    let (token, currency) = parse_token_currency(&key)?;

    let id = (
        token.to_string(),
        currency.to_string(),
        Txid::from_byte_array([0xffu8; 32]),
    );
    let oracles = ctx
        .services
        .oracle_token_currency
        .by_id
        .list(Some(id.clone()), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == id.0 && k.1 == id.1,
            _ => true,
        })
        .flat_map(|item| {
            let (_, oracle) = item?;
            Ok::<OracleTokenCurrency, Error>(oracle)
        })
        .collect::<Vec<_>>();

    let mut prices = Vec::new();
    for oracle in oracles {
        let feeds = ctx
            .services
            .oracle_price_feed
            .by_id
            .list(
                Some((
                    token.to_string(),
                    currency.to_string(),
                    oracle.oracle_id,
                    Txid::from_byte_array([0xffu8; 32]),
                )),
                SortOrder::Descending,
            )?
            .take(1)
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == token && k.1 == currency && k.2 == oracle.oracle_id,
                _ => true,
            })
            .flatten()
            .collect::<Vec<_>>();

        let feed = feeds.first().cloned();

        prices.push(PriceOracleResponse {
            id: format!("{}-{}-{}", oracle.id.0, oracle.id.1, oracle.id.2),
            key: format!("{}-{}", oracle.key.0, oracle.key.1),
            token: oracle.token,
            currency: oracle.currency,
            oracle_id: oracle.oracle_id.to_string(),
            weightage: oracle.weightage,
            block: oracle.block,
            feed: feed.map(|(id, f)| {
                let oracle_txid = id.2;
                OraclePriceFeedResponse {
                    id: format!("{}-{}-{}-{}", token, currency, oracle_txid, f.txid),
                    key: format!("{}-{}-{}", token, currency, oracle_txid),
                    sort: hex::encode(f.block.height.to_string() + &f.txid.to_string()),
                    token: token.clone(),
                    currency: currency.clone(),
                    oracle_id: oracle_txid,
                    txid: f.txid,
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
        .route("/:key/oracles", get(get_oracles))
        .layer(Extension(ctx))
}

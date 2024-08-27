use std::sync::Arc;

use ain_dftx::COIN;
use ain_macros::ocean_endpoint;
use anyhow::Context;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::{hashes::Hash, Txid};
use indexmap::IndexSet;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::{
    oracle::OraclePriceFeedResponse,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error},
    model::{
        BlockContext, OracleIntervalSeconds, OraclePriceActive, OraclePriceActiveNextOracles,
        OraclePriceAggregated, OraclePriceAggregatedInterval, OracleTokenCurrency, PriceTicker,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedResponse {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedAggregatedResponse,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregatedResponse {
    pub amount: String,
    pub weightage: u8,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTickerResponse {
    pub id: String,   //token-currency
    pub sort: String, //count-height-token-currency
    pub price: OraclePriceAggregatedResponse,
}

impl From<PriceTicker> for PriceTickerResponse {
    fn from(price_ticker: PriceTicker) -> Self {
        let amount = price_ticker.price.aggregated.amount / Decimal::from(COIN);
        Self {
            id: format!("{}-{}", price_ticker.id.0, price_ticker.id.1),
            sort: price_ticker.sort,
            price: OraclePriceAggregatedResponse {
                id: format!(
                    "{}-{}-{}",
                    price_ticker.price.id.0, price_ticker.price.id.1, price_ticker.price.id.2
                ),
                key: format!("{}-{}", price_ticker.price.key.0, price_ticker.price.key.1),
                sort: price_ticker.price.sort,
                token: price_ticker.price.token,
                currency: price_ticker.price.currency,
                aggregated: OraclePriceAggregatedAggregatedResponse {
                    amount: format!("{:.8}", amount),
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
                .context("Missing price ticker index")?;

            Ok(PriceTickerResponse::from(price_ticker))
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
    let mut parts = key.split('-');
    let token = parts.next().context("Missing token")?;
    let currency = parts.next().context("Missing currency")?;

    let price_ticker = ctx
        .services
        .price_ticker
        .by_id
        .get(&(token.to_string(), currency.to_string()))?;

    let Some(price_ticker) = price_ticker else {
        return Ok(Response::new(None));
    };

    let res = PriceTickerResponse::from(price_ticker);

    Ok(Response::new(Some(res)))
}

#[ocean_endpoint]
async fn get_feed(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregated>> {
    let mut parts = key.split('-');
    let token = parts.next().context("Missing token")?;
    let currency = parts.next().context("Missing currency")?;

    let repo = &ctx.services.oracle_price_aggregated;
    let key = (token.to_string(), currency.to_string());
    let oracle_aggrigated = repo
        .by_key
        .list(Some(key), SortOrder::Descending)?
        .take(query.size)
        .flat_map(|item| {
            let (_, id) = item?;
            let item = repo.by_id.get(&id)?;
            Ok::<Option<OraclePriceAggregated>, Error>(item)
        })
        .flatten()
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(
        oracle_aggrigated,
        query.size,
        |aggrigated| aggrigated.sort.to_string(),
    ))
}

#[ocean_endpoint]
async fn get_feed_active(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceActive>> {
    let mut parts = key.split('-');
    let token = parts.next().context("Missing token")?;
    let currency = parts.next().context("Missing currency")?;

    let key = (token.to_string(), currency.to_string());
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

#[ocean_endpoint]
async fn get_feed_with_interval(
    Path((key, interval)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedInterval>> {
    let mut parts = key.split('-');
    let token = parts.next().context("Missing token")?;
    let currency = parts.next().context("Missing currency")?;

    let interval = match interval.as_str() {
        "900" => OracleIntervalSeconds::FifteenMinutes,
        "3600" => OracleIntervalSeconds::OneHour,
        "86400" => OracleIntervalSeconds::OneDay,
        _ => return Err(From::from("Invalid interval")),
    };
    let key = (token.to_string(), currency.to_string(), interval.clone());
    let repo = &ctx.services.oracle_price_aggregated_interval;
    let prices = repo
        .by_key
        .list(Some(key), SortOrder::Descending)?
        .take(query.size)
        .flat_map(|item| {
            let (_, id) = item?;
            let item = repo.by_id.get(&id)?;
            Ok::<Option<OraclePriceAggregatedInterval>, Error>(item)
        })
        .flatten()
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(prices, query.size, |item| {
        item.sort.clone()
    }))
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PriceOracleResponse {
    pub id: String,
    pub key: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub weightage: u8,
    pub feed: Option<OraclePriceFeedResponse>,
    pub block: BlockContext,
}

#[ocean_endpoint]
async fn get_oracles(
    Path(key): Path<String>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PriceOracleResponse>> {
    let mut parts = key.split('-');
    let token = parts.next().context("Missing token")?;
    let currency = parts.next().context("Missing currency")?;

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
                Ok((k, _)) => {
                    k.0 == token.to_string()
                        && k.1 == currency.to_string()
                        && k.2 == oracle.oracle_id
                }
                _ => true,
            })
            .map(|item| {
                let (_, data) = item?;
                Ok(data)
            })
            .collect::<Result<Vec<_>>>()?;

        let feed = feeds.first().cloned();

        prices.push(PriceOracleResponse {
            id: format!("{}-{}-{}", oracle.id.0, oracle.id.1, oracle.id.2),
            key: format!("{}-{}-{}", oracle.key.0, oracle.key.1, oracle.key.2),
            token: oracle.token,
            currency: oracle.currency,
            oracle_id: oracle.oracle_id.to_string(),
            weightage: oracle.weightage,
            block: oracle.block,
            feed: feed.map(|f| OraclePriceFeedResponse {
                id: format!("{}-{}-{}-{}", token, currency, f.oracle_id, f.txid),
                key: format!("{}-{}-{}", token, currency, f.oracle_id),
                sort: f.sort.clone(),
                token: f.token.clone(),
                currency: f.currency.clone(),
                oracle_id: f.oracle_id,
                txid: f.txid,
                time: f.time,
                amount: f.amount.to_string(),
                block: f.block.clone(),
            }),
        })
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

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
        OraclePriceAggregated, OraclePriceAggregatedInterval, OracleTokenCurrency, PriceTicker,
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
    pub weightage: Weightage,
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
    let (token, currency) = parse_token_currency(&key)?;

    let price_ticker = ctx.services.price_ticker.by_id.get(&(token, currency))?;

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
    let (token, currency) = parse_token_currency(&key)?;

    let repo = &ctx.services.oracle_price_aggregated;
    let id = (token.to_string(), currency.to_string(), u32::MAX);
    let oracle_aggregated = repo
        .by_id
        .list(Some(id), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == token && k.1 == currency,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            Ok(v)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        oracle_aggregated,
        query.size,
        |aggregated| aggregated.sort.to_string(),
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

#[ocean_endpoint]
async fn get_feed_with_interval(
    Path((key, interval)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceAggregatedInterval>> {
    let (token, currency) = parse_token_currency(&key)?;

    let interval = match interval.as_str() {
        "900" => OracleIntervalSeconds::FifteenMinutes,
        "3600" => OracleIntervalSeconds::OneHour,
        "86400" => OracleIntervalSeconds::OneDay,
        _ => return Err(From::from("Invalid interval")),
    };
    let key = (token, currency, interval);
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
        let feed = ctx
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
            .find(|item| matches!(item, Ok((k, _)) if k.0 == token && k.1 == currency && k.2 == oracle.oracle_id))
            .transpose()?
            .map(|(_, data)| data);

        prices.push(PriceOracleResponse {
            id: format!("{}-{}-{}", oracle.id.0, oracle.id.1, oracle.id.2),
            key: format!("{}-{}", oracle.key.0, oracle.key.1),
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
                block: f.block,
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

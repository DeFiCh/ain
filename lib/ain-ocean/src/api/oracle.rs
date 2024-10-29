use std::{str::FromStr, sync::Arc};

use ain_dftx::{Currency, Token, Weightage, COIN};
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::{
    common::parse_token_currency,
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::ApiError,
    model::{BlockContext, Oracle, PriceFeed},
    storage::{RepositoryOps, SortOrder},
    Result,
};

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleResponse {
    pub id: String,
    pub owner_address: String,
    pub weightage: Weightage,
    pub price_feeds: Vec<PriceFeed>,
    pub block: BlockContext,
}

impl OracleResponse {
    fn from_with_id(id: String, v: Oracle) -> Self {
        Self {
            id,
            owner_address: v.owner_address,
            weightage: v.weightage,
            price_feeds: v.price_feeds,
            block: v.block,
        }
    }
}

#[ocean_endpoint]
async fn list_oracles(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OracleResponse>> {
    let oracles = ctx
        .services
        .oracle
        .by_id
        .list(None, SortOrder::Descending)?
        .take(query.size)
        .map(|item| {
            let (id, v) = item?;
            Ok(OracleResponse::from_with_id(id.to_string(), v))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(oracles, query.size, |oracle| {
        oracle.id.clone()
    }))
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeedResponse {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: Token,
    pub currency: Currency,
    pub oracle_id: Txid,
    pub txid: Txid,
    pub time: i32,
    pub amount: String,
    pub block: BlockContext,
}

#[ocean_endpoint]
async fn get_feed(
    Path((oracle_id, key)): Path<(String, String)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<OraclePriceFeedResponse>> {
    let txid = Txid::from_str(&oracle_id)?;

    let (token, currency) = parse_token_currency(&key)?;

    let key = (token, currency, txid);

    let price_feed_list = ctx
        .services
        .oracle_price_feed
        .by_id
        .list(None, SortOrder::Descending)?
        .paginate(&query)
        .flatten()
        .collect::<Vec<_>>();

    let mut oracle_price_feeds = Vec::new();

    for ((token, currency, oracle_id, txid), feed) in &price_feed_list {
        if key.0.eq(token) && key.1.eq(currency) && key.2.eq(oracle_id) {
            let amount = Decimal::from(feed.amount) / Decimal::from(COIN);
            oracle_price_feeds.push(OraclePriceFeedResponse {
                id: format!("{}-{}-{}-{}", token, currency, oracle_id, txid),
                key: format!("{}-{}-{}", token, currency, oracle_id),
                sort: hex::encode(feed.block.height.to_string() + &txid.to_string()),
                token: token.to_owned(),
                currency: currency.to_owned(),
                oracle_id: oracle_id.to_owned(),
                txid: *txid,
                time: feed.time,
                amount: amount.normalize().to_string(),
                block: feed.block.clone(),
            });
        }
    }

    Ok(ApiPagedResponse::of(
        oracle_price_feeds,
        query.size,
        |price_feed| price_feed.sort.clone(),
    ))
}

#[ocean_endpoint]
async fn get_oracle_by_address(
    Path(address): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<OracleResponse>>> {
    let oracle = ctx
        .services
        .oracle
        .by_id
        .list(None, SortOrder::Descending)?
        .flatten()
        .filter_map(|(id, oracle)| {
            if oracle.owner_address == address {
                let res = OracleResponse::from_with_id(id.to_string(), oracle);
                return Some(res);
            }
            None
        })
        .next();

    Ok(Response::new(oracle))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_oracles))
        .route("/:oracleId/:key/feed", get(get_feed))
        .route("/:address", get(get_oracle_by_address))
        .layer(Extension(ctx))
}

use std::sync::Arc;

use anyhow::format_err;
use cached::proc_macro::cached;
use defichain_rpc::{
    defichain_rpc_json::{
        poolpair::{PoolPairInfo, PoolPairsResult},
        token::TokenInfo,
    },
    json::poolpair::PoolPairPagination,
    PoolPairRPC, TokenRPC,
};

use super::AppContext;
use crate::Result;

#[cached(
    result = true,
    key = "String",
    convert = r#"{ format!("gettoken{symbol}") }"#
)]
pub async fn get_token_cached(ctx: &Arc<AppContext>, symbol: &str) -> Result<(String, TokenInfo)> {
    let token = ctx
        .client
        .get_token(symbol)
        .await?
        .0
        .into_iter()
        .next()
        .ok_or(format_err!("Error getting token info"))?;
    Ok(token)
}

#[cached(
    result = true,
    key = "String",
    convert = r#"{ format!("getpoolpair{id}") }"#
)]
pub async fn get_pool_pair_cached(
    ctx: &Arc<AppContext>,
    id: String,
) -> Result<(String, PoolPairInfo)> {
    let pool_pair = ctx
        .client
        .get_pool_pair(id, Some(true))
        .await?
        .0
        .into_iter()
        .next()
        .ok_or(format_err!("Error getting pool pair info"))?;

    Ok(pool_pair)
}

// #[cached(
//     result = true,
//     key = "String",
//     convert = r#"{ format!("listpoolpairs") }"#
// )]
pub async fn list_pool_pairs_cached(ctx: &Arc<AppContext>) -> Result<PoolPairsResult> {
    let pool_pairs = ctx
        .client
        .list_pool_pairs(
            Some(PoolPairPagination {
                start: 0,
                including_start: true,
                limit: 1000,
            }),
            Some(true),
        )
        .await?;
    Ok(pool_pairs)
}

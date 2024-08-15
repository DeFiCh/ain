use std::{collections::HashMap, sync::Arc};

use cached::proc_macro::cached;
use defichain_rpc::{
    json::{
        loan::LoanSchemeResult,
        poolpair::{PoolPairInfo, PoolPairPagination, PoolPairsResult},
        token::{TokenInfo, TokenPagination, TokenResult},
    },
    jsonrpc_async::error::{Error as JsonRpcError, RpcError},
    Error, LoanRPC, MasternodeRPC, PoolPairRPC, TokenRPC,
};

use super::AppContext;
use crate::Result;

#[cached(
    result = true,
    time = 600,
    key = "String",
    convert = r#"{ format!("gettoken{symbol}") }"#
)]
pub async fn get_token_cached(
    ctx: &Arc<AppContext>,
    symbol: &str,
) -> Result<Option<(String, TokenInfo)>> {
    let res = ctx.client.get_token(symbol).await;

    let is_err = res.as_ref().is_err_and(|err| {
        // allow `Token not found` err
        err.to_string()
            != Error::JsonRpc(JsonRpcError::Rpc(RpcError {
                code: -5,
                message: "Token not found".to_string(),
                data: None,
            }))
            .to_string()
    });
    if is_err {
        return Err(res.unwrap_err().into());
    };

    let res = res.ok();

    let token = if let Some(res) = res {
        res.0.into_iter().next()
    } else {
        None
    };

    Ok(token)
}

pub async fn list_token_cached(ctx: &Arc<AppContext>) -> Result<TokenResult> {
    let tokens = ctx
        .client
        .list_tokens(
            Some(TokenPagination {
                start: 0,
                including_start: true,
                limit: 1000,
            }),
            Some(true),
        )
        .await?;

    Ok(tokens)
}

#[cached(
    result = true,
    time = 600,
    key = "String",
    convert = r#"{ format!("getpoolpair{id}") }"#
)]
pub async fn get_pool_pair_cached(
    ctx: &Arc<AppContext>,
    id: String,
) -> Result<Option<(String, PoolPairInfo)>> {
    let res = ctx.client.get_pool_pair(id, Some(true)).await;

    let is_err = res.as_ref().is_err_and(|err| {
        // allow `Pool not found` err
        err.to_string()
            != Error::JsonRpc(JsonRpcError::Rpc(RpcError {
                code: -5,
                message: "Pool not found".to_string(),
                data: None,
            }))
            .to_string()
    });
    if is_err {
        return Err(res.unwrap_err().into());
    };

    let res = res.ok();

    let pool_pair = if let Some(res) = res {
        res.0.into_iter().next()
    } else {
        None
    };

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

#[cached(result = true, time = 600, key = "String", convert = r#"{ format!("gov{id}") }"#)]
pub async fn get_gov_cached(
    ctx: &Arc<AppContext>,
    id: String,
) -> Result<HashMap<String, serde_json::Value>> {
    let gov = ctx.client.get_gov(id).await?;
    Ok(gov)
}

#[cached(
    result = true,
    time = 600,
    key = "String",
    convert = r#"{ format!("getloanscheme{id}") }"#
)]
pub async fn get_loan_scheme_cached(ctx: &Arc<AppContext>, id: String) -> Result<LoanSchemeResult> {
    let loan_scheme = ctx.client.get_loan_scheme(id).await?;
    Ok(loan_scheme)
}

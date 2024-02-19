use std::sync::Arc;

use anyhow::format_err;
use cached::proc_macro::cached;
use defichain_rpc::{defichain_rpc_json::token::TokenInfo, TokenRPC};

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

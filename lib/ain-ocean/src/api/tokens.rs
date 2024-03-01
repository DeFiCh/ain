use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{
    json::token::{TokenInfo, TokenResult},
    RpcApi,
};
use serde::Serialize;
use serde_json::json;
use serde_with::{serde_as, DisplayFromStr};

use super::{
    common::parse_display_symbol,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{error::ApiError, Result};

#[derive(Serialize, Debug, Clone, Default)]
pub struct TxHeight {
    tx: String,
    height: i64,
}

#[serde_as]
#[derive(Serialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct TokenData {
    id: String,
    symbol: String,
    symbol_key: String,
    name: String,
    decimal: u8,
    #[serde_as(as = "DisplayFromStr")]
    limit: i64,
    mintable: bool,
    tradeable: bool,
    #[serde(rename = "isDAT")]
    is_dat: bool,
    #[serde(rename = "isLPS")]
    is_lps: bool,
    is_loan_token: bool,
    finalized: bool,
    minted: String,
    creation: TxHeight,
    destruction: TxHeight,
    display_symbol: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    collateral_address: Option<String>,
}

impl TokenData {
    pub fn from_with_id(id: String, token: TokenInfo) -> Self {
        let display_symbol = parse_display_symbol(&token);
        Self {
            id,
            symbol: token.symbol,
            display_symbol,
            symbol_key: token.symbol_key,
            name: token.name,
            decimal: token.decimal,
            limit: token.limit,
            mintable: token.mintable,
            tradeable: token.tradeable,
            is_dat: token.is_dat,
            is_lps: token.is_lps,
            is_loan_token: token.is_loan_token,
            finalized: token.finalized,
            minted: token.minted.to_string(),
            creation: TxHeight {
                height: token.creation_height,
                tx: token.creation_tx,
            },
            destruction: TxHeight {
                height: token.destruction_height,
                tx: token.destruction_tx,
            },
            collateral_address: token.collateral_address.and_then(|addr| {
                if addr.is_empty() {
                    None
                } else {
                    Some(addr)
                }
            }),
        }
    }
}

#[ocean_endpoint]
async fn list_tokens(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<TokenData>> {
    let tokens: TokenResult = ctx.client.call(
        "listtokens",
        &[
            json!({
            "limit": query.size,
            "start": query.next.as_ref().and_then(|n| n.parse::<u32>().ok()).unwrap_or_default(),
            "including_start": query.next.is_none()
                }),
            true.into(),
        ],
    ).await?;

    let res = tokens
        .0
        .into_iter()
        .map(|(k, v)| TokenData::from_with_id(k, v))
        .collect::<Vec<_>>();
    Ok(ApiPagedResponse::of(res, query.size, |token| {
        token.id.clone()
    }))
}

#[ocean_endpoint]
async fn get_token(
    Path(id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<TokenData>>> {
    let mut v: TokenResult = ctx.client.call("gettoken", &[id.as_str().into()]).await?;

    let res =
        v.0.remove(&id)
            .map(|token| TokenData::from_with_id(id, token));

    Ok(Response::new(res))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_tokens))
        .route("/:id", get(get_token))
        .layer(Extension(ctx))
}

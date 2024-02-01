use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Path, routing::get, Extension, Router};
use defichain_rpc::{
    json::token::{TokenInfo, TokenResult},
    RpcApi,
};
use serde::Serialize;
use serde_json::json;

use super::{
    common::parse_display_symbol,
    response::{ApiPagedResponse, Response},
};
use crate::{
    api_query::{PaginationQuery, Query},
    error::ApiError,
    Result, Services,
};

#[derive(Serialize, Debug, Clone, Default)]
pub struct TxHeight {
    tx: String,
    height: i64,
}

#[derive(Serialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct TokenData {
    id: u32,
    symbol: String,
    symbol_key: String,
    name: String,
    decimal: u8,
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
    fn from_with_id(id: u32, token: TokenInfo) -> Self {
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
    Extension(services): Extension<Arc<Services>>,
) -> Result<ApiPagedResponse<TokenData>> {
    let tokens: TokenResult = services.client.call(
        "listtokens",
        &[
            json!({
            "limit": query.size,
            "start": query.next.as_ref().and_then(|n| n.parse::<u32>().ok()).unwrap_or_default(),
            "including_start": query.next.is_none()
                }),
            true.into(),
        ],
    )?;

    let res = tokens
        .0
        .into_iter()
        .map(|(k, v)| TokenData::from_with_id(k, v))
        .collect::<Vec<_>>();
    Ok(ApiPagedResponse::of(res, query.size, |token| token.id))
}

#[ocean_endpoint]
async fn get_token(
    Path(id): Path<u32>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<Response<Option<TokenData>>> {
    let mut v: TokenResult = services.client.call("gettoken", &[id.into()])?;

    let res = if let Some(token) = v.0.remove(&id) {
        Some(TokenData::from_with_id(id, token))
    } else {
        None
    };

    Ok(Response::new(res))
}

pub fn router(services: Arc<Services>) -> Router {
    Router::new()
        .route("/", get(list_tokens))
        .route("/:id", get(get_token))
        .layer(Extension(services))
}

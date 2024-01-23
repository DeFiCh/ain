use std::{collections::HashMap, sync::Arc};

use ain_macros::ocean_endpoint;
use anyhow::format_err;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Json, Router,
};
use bitcoincore_rpc::{Client, RpcApi};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::{
    api_paged_response::ApiPagedResponse, api_query::PaginationQuery, error::ApiError, Result,
};

// #[derive(Serialize, Deserialize, Debug, Clone)]
// #[serde(rename_all = "camelCase")]
// struct TokenData {
//     id: String,
//     symbol: String,
//     display_symbol: String,
//     symbol_key: String,
//     name: String,
//     decimal: i64,
//     limit: i64
//     mintable: bool,
//     tradeable: bool,
//     #[serde(rename = "isDAT")]
//     is_dat: bool,
//     #[serde(rename = "isLPS")]
//     is_lps: bool,
//     is_loan_token: bool,
//     finalized: bool,
//     minted: i64
//     creation: {
//       tx: String
//       height: number
//     }
//     destruction: {
//       tx: String
//       height: number
//     }
//     collateralAddress?: String
//   }

#[derive(Debug, Serialize, Deserialize)]
pub struct TokenResult(HashMap<String, TokenInfo>);

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TokenInfo {
    symbol: String,
    symbol_key: String,
    name: String,
    decimal: i64,
    limit: i64,
    mintable: bool,
    tradeable: bool,
    #[serde(rename = "isDAT")]
    is_dat: bool,
    #[serde(rename = "isLPS")]
    is_lps: bool,
    is_loan_token: bool,
    finalized: bool,
    minted: f64,
    creation_tx: String,
    creation_height: i64,
    destruction_tx: String,
    destruction_height: i64,
    collateral_address: String,
}

#[ocean_endpoint]
async fn list_tokens(
    Query(query): Query<PaginationQuery>,
    Extension(client): Extension<Arc<Client>>,
) -> Result<Json<ApiPagedResponse<Value>>> {
    let tokens: HashMap<String, Value> =
        client.call("listtokens", &[json!({"limit": query.size }), true.into()])?;

    println!("tokens : {:?}", tokens);
    let tokens = tokens.into_iter().map(|v| v.1).collect::<Vec<_>>();
    Ok(Json(ApiPagedResponse::of(tokens, query.size, |token| {
        token["name"].to_string()
    })))
}

#[ocean_endpoint]
async fn get_token(
    Path(id): Path<u32>,
    Extension(client): Extension<Arc<Client>>,
) -> Result<Json<Value>> {
    let v: Value = client.call("gettoken", &[id.into()])?;
    Ok(Json(v))
}

pub fn router(state: Arc<Client>) -> Router {
    Router::new()
        .route("/", get(list_tokens))
        .route("/:id", get(get_token))
        .layer(Extension(state))
}
